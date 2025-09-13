// NCR 53c710 SCSI definitions
//
// Copyright (C) Soumyajyotii Ssarkar <soumyajyotisarkar23@gmail.com> 2025 QEMU project
//
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

//Unused registers - might delete later;
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

// Status bits we actually need
//DSP
#define NCR_REG_DSP0      0x2C
#define NCR_REG_DSP1      0x2D
#define NCR_REG_DSP2      0x2E
#define NCR_REG_DSP3      0x2F
#define NCR_REG_DCNTL     0x3B
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

    // Basic configuration
    outb(0x07, iobase + NCR_REG_SCID);     // Host ID = 7
    outb(0x00, iobase + NCR_REG_SXFER);    // Async transfers
    outb(0x40, iobase + NCR_REG_DCNTL);    // Enable SCRIPTS
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
    u8 msgout[] = {
        0x80 | lun,                 // select lun
        0x08,
    };
    u8 status = 0xff;
    u8 msgin_tmp[2];
    u8 msgin = 0xff;

    u32 script[] = {
        /* select target, send scsi command */
        0x40000000 | target << 16,  // select target
        0x00000000,
        0x06000001,                 // msgout
        (u32)MAKE_FLATPTR(GET_SEG(SS), &msgout),
        0x02000010,                 // scsi command
        (u32)MAKE_FLATPTR(GET_SEG(SS), cdbcmd),

        /* handle disconnect */
        0x87820000,                 // phase == msgin ?
        0x00000018,
        0x07000002,                 // msgin
        (u32)MAKE_FLATPTR(GET_SEG(SS), &msgin_tmp),
        0x50000000,                 // re-select
        0x00000000,
        0x07000002,                 // msgin
        (u32)MAKE_FLATPTR(GET_SEG(SS), &msgin_tmp),

        /* dma data, get status, raise irq */
        dma,                        // dma data
        (u32)op->buf_fl,
        0x03000001,                 // status
        (u32)MAKE_FLATPTR(GET_SEG(SS), &status),
        0x07000001,                 // msgin
        (u32)MAKE_FLATPTR(GET_SEG(SS), &msgin),
        0x98080000,                 // dma irq
        0x00000000,
    };
    u32 dsp = (u32)MAKE_FLATPTR(GET_SEG(SS), &script);
    

    outb(dsp         & 0xff, iobase + NCR_REG_DSP0);
    outb((dsp >>  8) & 0xff, iobase + NCR_REG_DSP1);
    outb((dsp >> 16) & 0xff, iobase + NCR_REG_DSP2);
    outb((dsp >> 24) & 0xff, iobase + NCR_REG_DSP3);

    for (;;) {
        u8 istat = inb(iobase + NCR_REG_ISTAT);
        u8 dstat = inb(iobase + NCR_REG_DSTAT);
        // Check for SCSI interrupt pending
        if (istat & NCR_ISTAT_SIP) {
            u8 sstat0 = inb(iobase + NCR_REG_SSTAT0);
            u8 sstat1 = inb(iobase + NCR_REG_SSTAT1);
            if (sstat0 || sstat1) {
                goto fail;
            }
        }

        // Check for DMA interrupt (SCRIPTS completion)
        if (istat & NCR_ISTAT_DIP) {
            if (dstat & NCR_DSTAT_SIR) {
                // Check for success completion code 0x401
                u32 dsps = inl(iobase + NCR_REG_DSPS);
                if (dsps == 0x00000401) {
                    break; // Success!
                }
            }
            if (dstat & 0x80) {
                goto fail;
            }
        }
        usleep(5);
    }

    if (msgin == 0 && status == 0) {
        return DISK_RET_SUCCESS;
    }

fail:
    return DISK_RET_EBADTRACK;
}

static int
ncr710_detect_controller(u32 iobase)
{
    u8 ctest8 = inb(iobase + NCR_REG_CTEST8);
    return ((ctest8 & 0xF0) == (NCR710_CHIP_REV << 4)) ? 0 : -1;
}

static void
ncr710_scsi_init_lun(struct ncr_lun_s *nlun, u32 iobase, u8 target, u8 lun)
{
    memset(nlun, 0, sizeof(*nlun));
    nlun->drive.type = DTYPE_NCR710_SCSI;
    nlun->drive.cntl_id = 0; //Not a PCI Device
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
    struct ncr_lun_s nlun0;
    ncr710_scsi_init_lun(&nlun0, iobase, target, 0);

    if (scsi_rep_luns_scan(&nlun0.drive, ncr710_scsi_add_lun) < 0)
        scsi_sequential_scan(&nlun0.drive, 8, ncr710_scsi_add_lun);
}

static void
init_ncr710_scsi(u32 iobase)
{
    dprintf(1, "Checking for NCR53c710 at 0x%x\n", iobase);

    ncr710_reset(iobase);

    if (ncr710_detect_controller(iobase) < 0) {
        dprintf(1, "NCR710: Controller not found\n");
        return;
    }

    dprintf(1, "Found NCR53c710 at 0x%x\n", iobase);

    // Scan for devices (targets 0-6, skip 7 which is host)
    int i;
    for (i = 0; i < 7; i++) {
        printf("NCR710: Scanning target %d\n", i);
        ncr710_scsi_scan_target(iobase, i);
    }
}

// This will be called during PA-RISC specific initialization
void
ncr710_scsi_setup(void)
{
    ASSERT32FLAT();
    if (!CONFIG_NCR710_SCSI || !runningOnQEMU())
        return;

    dprintf(3, "Initializing NCR 53c710 SCSI controllers\n");

    // Initialize the LASI SCSI controller (NCR 53c710)
    init_ncr710_scsi(LASI_SCSI_HPA);

    // SCSI on DINO chip is a NCR 53c720
    init_ncr710_scsi(DINO_SCSI_HPA);
}
