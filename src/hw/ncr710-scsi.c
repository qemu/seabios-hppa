// NCR 53c710 SCSI controller support for PA-RISC
//
// Copyright (C) Soumyajyotii Ssarkar <soumyajyotisarkar23@gmail.com> 2025 QEMU project
//
// based on esp-scsi.c
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
#include "pcidevice.h" // foreachpci
#include "pci_ids.h" // PCI_DEVICE_ID
#include "pci_regs.h" // PCI_VENDOR_ID
#include "stacks.h" // run_thread
#include "std/disk.h" // DISK_RET_SUCCESS
#include "string.h" // memset
#include "util.h" // usleep

// bios-can access the same registers as the kernel?
#define NCR_REG_SCNTL0    0x00
#define NCR_REG_SCNTL1    0x01
#define NCR_REG_SDID      0x02
#define NCR_REG_SIEN      0x03
#define NCR_REG_SCID      0x04
#define NCR_REG_SXFER     0x05
#define NCR_REG_SODL      0x06
#define NCR_REG_SOCL      0x07
#define NCR_REG_SFBR      0x08
#define NCR_REG_SIDL      0x09
#define NCR_REG_SBDL      0x0A
#define NCR_REG_SBCL      0x0B
#define NCR_REG_DSTAT     0x0C
#define NCR_REG_SSTAT0    0x0D
#define NCR_REG_SSTAT1    0x0E
#define NCR_REG_SSTAT2    0x0F
#define NCR_REG_DSA       0x10
#define NCR_REG_CTEST0    0x14
#define NCR_REG_CTEST1    0x15
#define NCR_REG_CTEST2    0x16
#define NCR_REG_CTEST3    0x17
#define NCR_REG_CTEST4    0x18
#define NCR_REG_CTEST5    0x19
#define NCR_REG_CTEST6    0x1A
#define NCR_REG_CTEST7    0x1B
#define NCR_REG_TEMP      0x1C
#define NCR_REG_DFIFO     0x20
#define NCR_REG_ISTAT     0x21
#define NCR_REG_CTEST8    0x22
#define NCR_REG_LCRC      0x23
#define NCR_REG_DBC       0x24
#define NCR_REG_DCMD      0x27
#define NCR_REG_DNAD      0x28
#define NCR_REG_DSP       0x2C
#define NCR_REG_DSPS      0x30
#define NCR_REG_SCRATCH   0x34
#define NCR_REG_DMODE     0x38
#define NCR_REG_DIEN      0x39
#define NCR_REG_DWT       0x3A
#define NCR_REG_DCNTL     0x3B
#define NCR_REG_ADDER     0x3C

#define SCNTL0_ARB_SIMPLE   0x00    // Simple arbitration
#define SCNTL0_ARB_FULL     0xC0    // Full arbitration
#define SCNTL0_START        0x20    // Start Sequence
#define SCNTL0_WATN         0x10    // Select with ATN
#define SCNTL0_EPC          0x08    // Enable Parity Checking
#define SCNTL0_EPG          0x04    // Enable Parity Generation
#define SCNTL0_AAP          0x02    // Assert ATN on Parity Error
#define SCNTL0_TRG          0x01    // Target Mode

#define SCNTL1_EXC          0x80    // Extra Clock Cycle
#define SCNTL1_ADB          0x40    // Assert Data Bus
#define SCNTL1_DHP          0x20    // Disable Halt on Parity Error
#define SCNTL1_CON          0x10    // Connected
#define SCNTL1_RST          0x08    // SCSI Reset
#define SCNTL1_AESP         0x04    // Assert Even SCSI Parity
#define SCNTL1_IARB         0x02    // Immediate Arbitration
#define SCNTL1_SST          0x01    // Start SCSI Transfer

// Status bits
#define NCR_DSTAT_DFE     0x80  // DMA FIFO empty
#define NCR_DSTAT_MDPE    0x40  // Master Data Parity Error
#define NCR_DSTAT_BF      0x20  // Bus Fault
#define NCR_DSTAT_ABRT    0x10  // Aborted
#define NCR_DSTAT_SSI     0x08  // Script Step Interrupt
#define NCR_DSTAT_SIR     0x04  // Script Interrupt Received
#define NCR_DSTAT_WTD     0x02  // Watchdog Timeout
#define NCR_DSTAT_IID     0x01  // Illegal Instruction Detected

#define NCR_ISTAT_ABRT    0x80  // Abort operation
#define NCR_ISTAT_RST     0x40  // Software reset
#define NCR_ISTAT_SIGP    0x20  // Signal process
#define NCR_ISTAT_SEM     0x10  // Semaphore
#define NCR_ISTAT_CON     0x08  // Connected
#define NCR_ISTAT_INTF    0x04  // Interrupt on the fly
#define NCR_ISTAT_SIP     0x02  // SCSI interrupt pending
#define NCR_ISTAT_DIP     0x01  // DMA interrupt pending

#define NCR_SSTAT0_ILF    0x80  // Input Latch Full
#define NCR_SSTAT0_ORF    0x40  // Output Register Full
#define NCR_SSTAT0_OLF    0x20  // Output Latch Full
#define NCR_SSTAT0_AIP    0x10  // Arbitration In Progress
#define NCR_SSTAT0_LOA    0x08  // Lost Arbitration
#define NCR_SSTAT0_WOA    0x04  // Won Arbitration
#define NCR_SSTAT0_RST    0x02  // SCSI Reset Signal
#define NCR_SSTAT0_SDP    0x01  // SCSI Data Parity

#define NCR710_CHIP_REV   0x02  // NCR53C710 revision 2

struct ncr_lun_s {
    struct drive_s drive;
    u32 iobase;
    u8 target;
    u8 lun;
};

static void
ncr710_scsi_reset(u32 iobase)
{
    // Software reset via ISTAT register
    outb(NCR_ISTAT_RST, iobase + NCR_REG_ISTAT);  // Set RST bit
    usleep(25000);  // Wait for reset to complete (25ms)
    outb(0, iobase + NCR_REG_ISTAT);              // Clear RST bit
    usleep(5000);   // Wait for chip to stabilize (5ms)

    // Initialize essential registers after reset
    outb(0x00, iobase + NCR_REG_DMODE);   // Basic DMA mode
    outb(0x00, iobase + NCR_REG_DCNTL);   // Basic DMA control
    outb(0x07, iobase + NCR_REG_SCID);    // Set our SCSI ID to 7
    outb(0x00, iobase + NCR_REG_SXFER);   // Async transfers
}

static int
ncr710_wait_for_status(u32 iobase, u8 mask, u8 value, int timeout_us)
{
    int i;
    for (i = 0; i < timeout_us; i += 10) {
        u8 status = inb(iobase + NCR_REG_SSTAT0);
        if ((status & mask) == value) {
            return 0;  // Success
        }
        usleep(10);
    }
    return -1;  // Timeout
}

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
    int i;

    // Clear any pending interrupts
    inb(iobase + NCR_REG_DSTAT);
    inb(iobase + NCR_REG_SSTAT0);

    // Step 1: Arbitrate for the bus
    outb(SCNTL0_ARB_SIMPLE | SCNTL0_EPC | SCNTL0_EPG, iobase + NCR_REG_SCNTL0);

    // Wait for arbitration to complete
    if (ncr710_wait_for_status(iobase, NCR_SSTAT0_WOA, NCR_SSTAT0_WOA, 1000) < 0) {
        dprintf(1, "NCR710: Arbitration failed\n");
        return DISK_RET_ENOTREADY;
    }

    // Step 2: Select target
    outb(1 << target, iobase + NCR_REG_SDID);  // Select target
    outb(SCNTL0_ARB_SIMPLE | SCNTL0_EPC | SCNTL0_EPG | SCNTL0_START | SCNTL0_WATN,
         iobase + NCR_REG_SCNTL0);

    // Wait for selection to complete
    if (ncr710_wait_for_status(iobase, NCR_ISTAT_CON, NCR_ISTAT_CON, 5000) < 0) {
        dprintf(1, "NCR710: Target selection failed\n");
        return DISK_RET_ENOTREADY;
    }

    // Step 3: Send SCSI command
    // Send IDENTIFY message first
    outb(0x80 | lun, iobase + NCR_REG_SODL);  // IDENTIFY message with LUN
    outb(SCNTL1_SST, iobase + NCR_REG_SCNTL1);  // Start transfer

    // Wait for message to be sent
    usleep(100);

    // Send CDB command bytes
    for (i = 0; i < 16 && i < sizeof(cdbcmd); i++) {
        outb(cdbcmd[i], iobase + NCR_REG_SODL);
        outb(SCNTL1_SST, iobase + NCR_REG_SCNTL1);
        usleep(50);  // Small delay between bytes
    }

    // Step 4: Data transfer if needed
    if (op->count && blocksize) {
        u32 bytes = op->count * blocksize;
        u32 buf = (u32)op->buf_fl;

        // Set up DMA address (32-bit little endian)
        outb(buf & 0xFF, iobase + NCR_REG_DNAD);
        outb((buf >> 8) & 0xFF, iobase + NCR_REG_DNAD + 1);
        outb((buf >> 16) & 0xFF, iobase + NCR_REG_DNAD + 2);
        outb((buf >> 24) & 0xFF, iobase + NCR_REG_DNAD + 3);

        // Set up DMA byte count (24-bit little endian)
        outb(bytes & 0xFF, iobase + NCR_REG_DBC);
        outb((bytes >> 8) & 0xFF, iobase + NCR_REG_DBC + 1);
        outb((bytes >> 16) & 0xFF, iobase + NCR_REG_DBC + 2);

        // Start DMA transfer
        if (scsi_is_read(op)) {
            outb(0x01, iobase + NCR_REG_DCMD);  // DMA read command
        } else {
            outb(0x00, iobase + NCR_REG_DCMD);  // DMA write command
        }

        // Wait for DMA completion
        for (i = 0; i < 10000; i++) {
            u8 dstat = inb(iobase + NCR_REG_DSTAT);
            u8 istat = inb(iobase + NCR_REG_ISTAT);

            if (dstat & NCR_DSTAT_DFE) {
                break;  // DMA FIFO empty - transfer complete
            }
            if (istat & (NCR_ISTAT_SIP | NCR_ISTAT_DIP)) {
                break;  // Interrupt pending
            }
            usleep(10);
        }

        if (i >= 10000) {
            dprintf(1, "NCR710: DMA timeout\n");
            return DISK_RET_ETIMEOUT;
        }
    }

    // Step 5: Get status and message
    u8 status = inb(iobase + NCR_REG_SFBR);  // Read status
    u8 msg = inb(iobase + NCR_REG_SFBR);     // Read message (usually COMMAND COMPLETE)

    // Step 6: Disconnect
    outb(0, iobase + NCR_REG_SCNTL0);  // Clear all control bits

    printf("NCR710: Command complete, status=0x%02x, msg=0x%02x\n", status, msg);

    if (status == 0) {
        return DISK_RET_SUCCESS;
    }

    printf("NCR710: Command failed with status 0x%02x\n", status);
    return DISK_RET_EBADTRACK;
}

static void
ncr710_scsi_init_lun(struct ncr_lun_s *nlun, u32 iobase, u8 target, u8 lun)
{
    memset(nlun, 0, sizeof(*nlun));
    nlun->drive.type = DTYPE_NCR710_SCSI;
    nlun->drive.cntl_id = 0; // Non-PCI device
    nlun->target = target;
    nlun->lun = lun;
    nlun->iobase = iobase;
}

static int
ncr710_scsi_add_lun(u32 lun, struct drive_s *tmpl_drv)
{
    struct ncr_lun_s *tmpl_nlun =
        container_of(tmpl_drv, struct ncr_lun_s, drive);

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

    if (ret)
        goto fail;

    return 0;

fail:
    free(nlun);
    return -1;
}

static void
ncr710_scsi_scan_target(u32 iobase, u8 target)
{
    struct ncr_lun_s nlun0;

    ncr710_scsi_init_lun(&nlun0, iobase, target, 0);

    if (scsi_rep_luns_scan(&nlun0.drive, ncr710_scsi_add_lun) < 0)
        scsi_sequential_scan(&nlun0.drive, 8, ncr710_scsi_add_lun);
}

static int
ncr710_detect_controller(u32 iobase)
{
    // Check if controller is present by reading CTEST8 (chip revision)
    u8 ctest8 = inb(iobase + NCR_REG_CTEST8);
    dprintf(3, "NCR710 CTEST8 (chip revision): 0x%02x\n", ctest8);

    // NCR53C710 should return revision 2
    if ((ctest8 & 0xF0) != (NCR710_CHIP_REV << 4)) {
        dprintf(1, "NCR710: Wrong chip revision, expected 0x2x, got 0x%02x\n", ctest8);
        return -1;
    }

    // Verify SCID register can be written/read
    u8 saved_scid = inb(iobase + NCR_REG_SCID);
    outb(0x07, iobase + NCR_REG_SCID);
    u8 scid = inb(iobase + NCR_REG_SCID);
    outb(saved_scid, iobase + NCR_REG_SCID);  // Restore original value

    if (scid != 0x07) {
        dprintf(1, "NCR710: SCID register test failed, expected 0x07, got 0x%02x\n", scid);
        return -1;
    }

    // Verify CTEST0 is accessible
    u8 ctest0 = inb(iobase + NCR_REG_CTEST0);
    dprintf(3, "NCR710 CTEST0: 0x%02x\n", ctest0);

    return 0;  // Controller detected successfully
}

static void
init_ncr710_scsi(u32 iobase)
{
    dprintf(1, "Checking for NCR53c710 SCSI controller at address 0x%x\n", iobase);

    // Reset the controller first
    ncr710_scsi_reset(iobase);

    // Detect if controller is present and working
    if (ncr710_detect_controller(iobase) < 0) {
        dprintf(1, "NCR710: Controller not detected at 0x%x\n", iobase);
        return;
    }

    printf("Found NCR53c710 SCSI controller at address 0x%x\n", iobase);

    // Configure controller for operation
    outb(0x00, iobase + NCR_REG_SXFER);   // Async transfers initially
    outb(SCNTL1_EXC, iobase + NCR_REG_SCNTL1);  // Enable extra clock cycle
    outb(0x07, iobase + NCR_REG_SCID);    // Set controller SCSI ID to 7

    // Scan for attached devices (targets 0-6, skip 7 which is our ID)
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
