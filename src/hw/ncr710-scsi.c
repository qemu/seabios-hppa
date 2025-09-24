// NCR 53c710 SCSI definitions
//
// Copyright (C) Soumyajyotii Ssarkar <soumyajyotisarkar23@gmail.com> 2025 QEMU project
//
// Based on the lsi-scsi.c, but hacked to not support PCI device.
// This is the bios side for the LASI's NCR53C710 SCSI Controller for QEMU.
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "biosvar.h" // GET_GLOBALFLAT
#include "block.h" // struct drive_s
#include "blockcmd.h" // scsi_drive_setup
#include "config.h" // CONFIG_*
#include "fw/paravirt.h" // runningOnQEMU
#include "malloc.h" // free
#include "output.h" // dprintf
#include "parisc/hppa_hardware.h" // LASI_SCSI_HPA
#include "stacks.h" // run_thread
#include "std/disk.h" // DISK_RET_SUCCESS
#include "string.h" // memset
#include "util.h" // usleep

#define LASI_SCSI_CORE_OFFSET 0x100

#define NCR_REG_SCNTL0    0x00
#define NCR_REG_SCNTL1    0x01
#define NCR_REG_DSA       0x10
#define NCR_REG_DBC       0x24
#define NCR_REG_DCMD      0x27
#define NCR_REG_DNAD      0x28
#define NCR_REG_DSPS      0x30
#define NCR_REG_SCRATCH   0x34
#define NCR_REG_DMODE     0x38

#define NCR_REG_SCID      0x04
#define NCR_REG_SXFER     0x05
#define NCR_REG_DSTAT     0x0C
#define NCR_REG_SSTAT0    0x0D
#define NCR_DSTAT_DFE     0x80  // DMA FIFO empty
#define NCR_REG_SSTAT1    0x0E
#define NCR_REG_ISTAT     0x21
#define NCR_REG_CTEST8    0x22

#define NCR_REG_DSP0      0x2C
#define NCR_REG_DSP1      0x2D
#define NCR_REG_DSP2      0x2E
#define NCR_REG_DSP3      0x2F
#define NCR_REG_DCNTL     0x3B
#define NCR_REG_DIEN      0x39  // DMA Interrupt Enable
#define NCR_REG_SIEN0     0x03  // SCSI Interrupt Enable 0
#define NCR_DSTAT_SIR     0x04  // SCRIPTS interrupt
#define NCR_ISTAT_DIP     0x01  // DMA interrupt pending
#define NCR_ISTAT_RST     0x40  // Software reset
#define NCR_ISTAT_SIP     0x02  // SCSI interrupt pending

#define NCR710_CHIP_REV   0x02

struct ncr_lun_s {
    struct drive_s drive;
    u32 iobase;
    u8 target;
    u8 lun;
};

static void
ncr710_reset(u32 iobase)
{
    outb(NCR_ISTAT_RST, iobase + NCR_REG_ISTAT);
    usleep(25000);
    outb(0, iobase + NCR_REG_ISTAT);
    usleep(5000);

    // Minimal configuration
    outb(0x07, iobase + NCR_REG_SCID);     // Host ID = 7
    outb(0x00, iobase + NCR_REG_SXFER);    // Async transfers
    outb(0x40, iobase + NCR_REG_DCNTL);    // Enable SCRIPTS
    outb(0x00, iobase + NCR_REG_DIEN);     // Disable DMA interrupts
    outb(0x00, iobase + NCR_REG_SIEN0);    // Disable SCSI interrupts
}

int
ncr710_scsi_process_op(struct disk_op_s *op)
{
    if (!CONFIG_NCR710_SCSI)
        return DISK_RET_EBADTRACK;
    struct ncr_lun_s *llun_gf =
        container_of(op->drive_fl, struct ncr_lun_s, drive);
    u16 target = GET_GLOBALFLAT(llun_gf->target);
    u16 lun = GET_GLOBALFLAT(llun_gf->lun);
    u8 cdbcmd[16];
    int blocksize = scsi_fill_cmd(op, cdbcmd, sizeof(cdbcmd));
    if (blocksize < 0)
        return default_process_op(op);
    u32 iobase = GET_GLOBALFLAT(llun_gf->iobase);
    u32 dma = ((scsi_is_read(op) ? 0x01000000 : 0x00000000) |
               (op->count * blocksize));
    u8 status = 0xff;
    u8 msgin = 0xff;

    u32 script[] = {
        /* select target, send scsi command */
        0x40000000 | target << 16,  // select target (offset 0)
        0x00000000,                 // (no alternative address) (offset 4)

        0x02000010,                 // scsi command (offset 8)
        (u32)MAKE_FLATPTR(GET_SEG(SS), cdbcmd), // (offset 12)

        /* flexible data transfer - handle both DI and MI phases */
        0x80070000,                 // jump if IS message in phase (offset 16)
        0x00000000,                 // jump address - will be filled below (offset 20)

        /* data in phase */
        dma,                        // dma data (offset 24)
        (u32)op->buf_fl,           // (offset 28)

        /* status phase */
        0x03000001,                 // status (offset 32)
        (u32)MAKE_FLATPTR(GET_SEG(SS), &status), // (offset 36)

        /* message in phase */
        0x07000001,                 // msgin (offset 40)
        (u32)MAKE_FLATPTR(GET_SEG(SS), &msgin), // (offset 44)
        0x98080000,                 // dma irq (offset 48)
        0x00000401,                 // success code (offset 52)
    };
    u32 dsp = (u32)MAKE_FLATPTR(GET_SEG(SS), &script);

    // Calculate jump address to MESSAGE IN phase (offset:: 40 from script start)
    script[5] = dsp + 40;


    outb((dsp >> 24) & 0xff, iobase + NCR_REG_DSP0);
    outb((dsp >> 16) & 0xff, iobase + NCR_REG_DSP1);
    outb((dsp >>  8) & 0xff, iobase + NCR_REG_DSP2);
    outb(dsp         & 0xff, iobase + NCR_REG_DSP3);

    printf("NCR710: Script started, DSP=0x%08x\n", dsp);

    // We poll for DSTAT.SIR to be set, which happens when script completes.
    // I think this is better than relying on interrupts.
    int poll_count = 0;
    for (;;) {
        poll_count++;
        u8 dstat = inb(iobase + NCR_REG_DSTAT);

        if (dstat & NCR_DSTAT_SIR) {
            u8 dsps_bytes[4];
            dsps_bytes[0] = inb(iobase + NCR_REG_DSPS + 3);
            dsps_bytes[1] = inb(iobase + NCR_REG_DSPS + 2);
            dsps_bytes[2] = inb(iobase + NCR_REG_DSPS + 1);
            dsps_bytes[3] = inb(iobase + NCR_REG_DSPS + 0);

            u32 dsps = (dsps_bytes[3] << 24) | (dsps_bytes[2] << 16) |
                       (dsps_bytes[1] << 8) | dsps_bytes[0];

            printf("NCR710: SCRIPTS interrupt (poll_count=%d), DSTAT=0x%02x, DSPS=0x%08x\n", poll_count, dstat, dsps);

            if (dsps == 0x00000401) {
                printf("NCR710: Command completed successfully! (target=%d, lun=%d)\n", target, lun);
                return DISK_RET_SUCCESS;
            } else {
                printf("NCR710: Unexpected SCRIPTS interrupt code 0x%08x\n", dsps);
                goto fail;
            }
        }

        // Check for SCSI errors
        u8 sstat0 = inb(iobase + NCR_REG_SSTAT0);
        u8 sstat1 = inb(iobase + NCR_REG_SSTAT1);

        if ((sstat0 & ~0x80) || (sstat1 & ~0x04)) {
            printf("NCR710: SCSI error, SSTAT0=0x%02x, SSTAT1=0x%02x\n", sstat0, sstat1);
            goto fail;
        }

        if (sstat1 & 0x04) {
            printf("NCR710: Target selection failed (no device at target %d)\n", target);
            goto fail; // This is expected for non-existent targets
        }        // Check for DMA errors
        if (dstat & 0x80) { // DMA FIFO empty or other DMA errors
            printf("NCR710: DMA error, DSTAT=0x%02x\n", dstat);
            goto fail;
        }

        // Continue polling
        usleep(5);
    }

fail:
    return DISK_RET_EBADTRACK;
}

static int
ncr710_detect_controller(u32 iobase)
{
    printf("NCR710: Starting controller detection at 0x%x\n", iobase);

    // Read various registers to detect the chip
    u8 ctest8 = inb(iobase + NCR_REG_CTEST8);
    u8 istat = inb(iobase + NCR_REG_ISTAT);
    u8 dstat = inb(iobase + NCR_REG_DSTAT);
    printf("NCR710: CTEST8=0x%02x, ISTAT=0x%02x, DSTAT=0x%02x\n", ctest8, istat, dstat);

    u32 temp_reg = iobase + 0x1C; // TEMP register

    // Save original values byte by byte
    u8 original_temp[4];
    original_temp[0] = inb(temp_reg);
    original_temp[1] = inb(temp_reg + 1);
    original_temp[2] = inb(temp_reg + 2);
    original_temp[3] = inb(temp_reg + 3);
    printf("NCR710: Original TEMP register: 0x%02x%02x%02x%02x\n",
           original_temp[3], original_temp[2], original_temp[1], original_temp[0]);

    // Write test pattern byte by byte (either way we only writing zeros)
    outb(0x12, temp_reg);      // TEMP+0
    outb(0x34, temp_reg + 1);  // TEMP+1
    outb(0x56, temp_reg + 2);  // TEMP+2
    outb(0x78, temp_reg + 3);  // TEMP+3

    u8 read_back[4];
    read_back[0] = inb(temp_reg);
    read_back[1] = inb(temp_reg + 1);
    read_back[2] = inb(temp_reg + 2);
    read_back[3] = inb(temp_reg + 3);
    printf("NCR710: TEMP test - wrote 0x12345678, read bytes: 0x%02x 0x%02x 0x%02x 0x%02x\n",
           read_back[0], read_back[1], read_back[2], read_back[3]);

    outb(original_temp[0], temp_reg);
    outb(original_temp[1], temp_reg + 1);
    outb(original_temp[2], temp_reg + 2);
    outb(original_temp[3], temp_reg + 3);

    if (read_back[0] == 0x12 && read_back[1] == 0x34 &&
        read_back[2] == 0x56 && read_back[3] == 0x78) {
        printf("NCR710: Controller detected successfully at 0x%x\n", iobase);
        return 0;
    }
    printf("NCR710: Controller detection failed at 0x%x\n", iobase);
    return -1;
}

static void
ncr710_scsi_init_lun(struct ncr_lun_s *nlun, u32 iobase, u8 target, u8 lun)
{
    memset(nlun, 0, sizeof(*nlun));
    nlun->drive.type = DTYPE_NCR710_SCSI;
    nlun->drive.cntl_id = 0;
    nlun->target = target;
    nlun->lun = lun;
    nlun->iobase = iobase;
}

static int
ncr710_scsi_add_lun(u32 lun, struct drive_s *tmpl_drv)
{
    struct ncr_lun_s *tmpl_nlun = container_of(tmpl_drv, struct ncr_lun_s, drive);
    struct ncr_lun_s *nlun = malloc_fseg(sizeof(*nlun));
    if (!nlun) {
        warn_noalloc();
        return -1;
    }

    ncr710_scsi_init_lun(nlun, tmpl_nlun->iobase, tmpl_nlun->target, lun);

    char *name = znprintf(MAXDESCSIZE, "ncr710 %d:%d", nlun->target, nlun->lun);
    int prio = bootprio_find_scsi_device(NULL, nlun->target, nlun->lun);
    int ret = scsi_drive_setup(&nlun->drive, name, prio, nlun->target, nlun->lun);
    free(name);

    if (ret) {
        free(nlun);
        return -1;
    }
    return 0;
}

static void
ncr710_scsi_scan_target(u32 iobase, u8 target)
{
    printf("NCR710: Starting scan of target %d\n", target);
    struct ncr_lun_s nlun0;
    ncr710_scsi_init_lun(&nlun0, iobase, target, 0);

    printf("NCR710: Trying scsi_rep_luns_scan for target %d\n", target);
    if (scsi_rep_luns_scan(&nlun0.drive, ncr710_scsi_add_lun) < 0) {
        printf("NCR710: scsi_rep_luns_scan failed, trying scsi_sequential_scan for target %d\n", target);
        scsi_sequential_scan(&nlun0.drive, 8, ncr710_scsi_add_lun);
    }
    printf("NCR710: Finished scanning target %d\n", target);
}

static void
init_ncr710_scsi(u32 base_addr)
{
    u32 iobase = base_addr + LASI_SCSI_CORE_OFFSET;
    dprintf(1, "NCR710: Base addr=0x%x, Core offset=0x%x, IO base=0x%x\n",
            base_addr, LASI_SCSI_CORE_OFFSET, iobase);

    ncr710_reset(iobase);

    if (ncr710_detect_controller(iobase) < 0) {
        dprintf(1, "NCR710: Controller not found at 0x%x\n", iobase);
        return;
    }

    dprintf(1, "NCR710: Found controller at 0x%x\n", iobase);

    int i;
    for (i = 0; i < 7; i++) {
        dprintf(1, "NCR710: Scanning target %d\n", i);
        ncr710_scsi_scan_target(iobase, i);
    }
}

void
ncr710_scsi_setup(void)
{
    ASSERT32FLAT();
    if (!CONFIG_NCR710_SCSI || !runningOnQEMU())
        return;
    printf("Initializing NCR 53c710 SCSI controllers\n");
    init_ncr710_scsi(LASI_SCSI_HPA);
}