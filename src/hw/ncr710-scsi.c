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

#define NCR_REG_SCNTL0    0x00
#define NCR_REG_SCNTL1    0x01
#define NCR_REG_SCID      0x04
#define NCR_REG_SXFER     0x05
#define NCR_REG_DSTAT     0x0C
#define NCR_REG_SSTAT0    0x0D
#define NCR_REG_SSTAT1    0x0E
#define NCR_REG_DSA       0x10
#define NCR_REG_ISTAT     0x21
#define NCR_REG_CTEST8    0x22
#define NCR_REG_DBC       0x24
#define NCR_REG_DCMD      0x27
#define NCR_REG_DNAD      0x28
#define NCR_REG_DSP       0x2C
#define NCR_REG_DSPS      0x30
#define NCR_REG_SCRATCH   0x34
#define NCR_REG_DMODE     0x38
#define NCR_REG_DCNTL     0x3B

// Status bits we actually need
#define NCR_DSTAT_DFE     0x80  // DMA FIFO empty
#define NCR_DSTAT_SIR     0x04  // SCRIPTS interrupt
#define NCR_ISTAT_RST     0x40  // Software reset
#define NCR_ISTAT_SIP     0x02  // SCSI interrupt pending
#define NCR_ISTAT_DIP     0x01  // DMA interrupt pending

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

// static int
// ncr710_wait_interrupt(u32 iobase, int timeout_ms)
// {
//     int i;
//     for (i = 0; i < timeout_ms; i++) {
//         u8 istat = inb(iobase + NCR_REG_ISTAT);
//         if (istat & (NCR_ISTAT_SIP | NCR_ISTAT_DIP)) {
//             // Clear interrupt by reading status
//             inb(iobase + NCR_REG_DSTAT);
//             inb(iobase + NCR_REG_SSTAT0);
//             return 0;
//         }
//         usleep(1000);
//     }
//     return -1;  // Timeout
// }

int
ncr710_scsi_process_op(struct disk_op_s *op)
{
    if (!CONFIG_NCR710_SCSI)
        return DISK_RET_EBADTRACK;
    struct ncr_lun_s *nlun_gf =
        container_of(op->drive_fl, struct ncr_lun_s, drive);
    u16 target = GET_GLOBALFLAT(nlun_gf->target);
    u16 lun = GET_GLOBALFLAT(nlun_gf->lun);
    u8 cdbcmd[16];
    int blocksize = scsi_fill_cmd(op, cdbcmd, sizeof(cdbcmd));
    if (blocksize < 0)
        return default_process_op(op);
    u32 iobase = GET_GLOBALFLAT(nlun_gf->iobase);
    
    u8 status = 0xff;
    u8 msgin = 0xff;
    u8 msgout[] = {
        0x80 | lun,                 // identify message with LUN
    };
    
    u32 data_size = op->count * blocksize;
    
    u32 script[] = {
        0x40000000 | (target << 16), // SELECT target 
        0x00000000,  
        0x0e000001, 
        (u32)MAKE_FLATPTR(GET_SEG(SS), msgout), // MOVE 1, msgout, WHEN MSG_OUT
        
        0x02000000 | sizeof(cdbcmd), 
        (u32)MAKE_FLATPTR(GET_SEG(SS), cdbcmd), // MOVE cdb_size, cdbcmd, WHEN COMMAND
        
        data_size > 0 ? (scsi_is_read(op) ? 0x01000000 : 0x00000000) | data_size : 0x80080000, 
        data_size > 0 ? (u32)op->buf_fl : 0x00000000,
        // Get status
        0x03000001,
        (u32)MAKE_FLATPTR(GET_SEG(SS), &status), // MOVE 1, status, WHEN STATUS
        
        // Get message in (command complete)
        0x07000001, 
        (u32)MAKE_FLATPTR(GET_SEG(SS), &msgin), // MOVE 1, msgin, WHEN MSG_IN
        0x98080000, 0x00000401,     // INT 0x401 (GOOD_STATUS_AFTER_STATUS)
    };
    
    // Make sure we have proper memory alignment and endianness
    u32 *script_ptr = malloc_tmp(sizeof(script));
    if (!script_ptr) {
        dprintf(1, "NCR710: Failed to allocate SCRIPTS memory\n");
        goto fail;
    }
    
    memcpy(script_ptr, script, sizeof(script));
    
    // Clear any pending interrupts
    inb(iobase + NCR_REG_DSTAT);
    inb(iobase + NCR_REG_SSTAT0);
    inb(iobase + NCR_REG_SSTAT1);
    
    // Reset the controller to clear any previous state
    outb(NCR_ISTAT_RST, iobase + NCR_REG_ISTAT);
    usleep(1000);
    outb(0, iobase + NCR_REG_ISTAT);
    usleep(1000);
    
    // Set up NCR710 registers for SCRIPTS execution
    outl(0, iobase + NCR_REG_DSA);                              // Data Structure Address
    outl((u32)MAKE_FLATPTR(GET_SEG(SS), script_ptr), iobase + NCR_REG_DSP); // SCRIPTS Pointer
    
    dprintf(3, "NCR710: Starting SCRIPTS at 0x%x for target %d\n", 
            (u32)MAKE_FLATPTR(GET_SEG(SS), script_ptr), target);
    
    outb(0x01, iobase + NCR_REG_DCNTL);     // Start SCRIPTS

    int timeout = 5000; // 5 seconds
    while (timeout-- > 0) {
        u8 istat = inb(iobase + NCR_REG_ISTAT);
        if (istat & (NCR_ISTAT_SIP | NCR_ISTAT_DIP)) {
            u8 dstat = inb(iobase + NCR_REG_DSTAT);
            u8 sstat0 = inb(iobase + NCR_REG_SSTAT0);
            u8 sstat1 = inb(iobase + NCR_REG_SSTAT1);
            
            dprintf(3, "NCR710: Interrupt - ISTAT=0x%x DSTAT=0x%x SSTAT0=0x%x SSTAT1=0x%x\n",
                    istat, dstat, sstat0, sstat1);
            
            if (dstat & NCR_DSTAT_SIR) {
                // SCRIPTS interrupt
                u32 dsps = inl(iobase + NCR_REG_DSPS);
                dprintf(3, "NCR710: SCRIPTS interrupt 0x%x, status=0x%x, msgin=0x%x\n", 
                        dsps, status, msgin);
                
                if (dsps == 0x401) {  // GOOD_STATUS_AFTER_STATUS
                    if (status == 0x00) {
                        free(script_ptr);
                        return DISK_RET_SUCCESS;
                    }
                }
            }
            
            if (sstat0 || sstat1) {
                dprintf(1, "NCR710: SCSI error - SSTAT0=0x%x SSTAT1=0x%x\n", sstat0, sstat1);
                break;
            }
            
            // Handle other interrupt conditions
            break;
        }
        usleep(1000);
    }
    
    if (timeout <= 0) {
        dprintf(1, "NCR710: Command timeout\n");
    }
    
    free(script_ptr);

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
