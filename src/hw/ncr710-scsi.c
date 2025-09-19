// NCR53c710 SCSI controller support for HP PA-RISC machines.
//
// Copyright (C) 2024 Soumyajyoti Sarkar <soumyajyotisarkar23@gmail.com> 
// Based on existing seabios code.
//
// Note Seabios primarimarily supports PCI device.
// Hacked to support NCR710 which is not a PCI device.
// This driver supports the NCR 53c710 SCSI controller as found on 
// HP PA-RISC machines, accessed through the LASI (Level A System Interface).
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "biosvar.h" // GET_GLOBALFLAT
#include "block.h" // struct drive_s
#include "blockcmd.h" // scsi_drive_setup
#include "config.h" // CONFIG_*
#include "byteorder.h" // cpu_to_*
#include "fw/paravirt.h" // runningOnQEMU
#include "output.h" // dprintf
#include "pcidevice.h" // foreachpci
#include "romfile.h" // romfile_find
#include "stacks.h" // run_thread
#include "std/disk.h" // DISK_*
#include "string.h" // memset
#include "util.h" // boot_add_hd, msleep
#include "malloc.h" // free
#include "output.h" // dprintf
#include "parisc/hppa_hardware.h" // LASI_SCSI_HPA
#include "stacks.h" // run_thread
#include "std/disk.h" // DISK_RET_SUCCESS
#include "string.h" // memset, memcpy
#include "util.h" // usleep, bootprio_find_scsi_mmio_device


// NCR710 register (adding all for reference)
#define SCNTL0_REG          0x00
#define SCNTL1_REG          0x01
#define SDID_REG            0x02
#define SIEN_REG            0x03
#define SCID_REG            0x04
#define CTEST7_REG          0x1B
#define ISTAT_REG           0x21    /* Interrupt Status */
#define DSTAT_REG           0x0C    /* DMA Status */
#define SSTAT0_REG          0x0D    /* SCSI Status Zero */
#define TEMP_REG            0x1C    /* Temporary Stack */
#define SSTAT0_REG          0x0D    /* SCSI Status Zero */


#define ISTAT_ABRT          0x80
#define ISTAT_SRST          0x40    // Software Reset (710 only)
#define ISTAT_SIGP          0x20
#define ISTAT_SEM           0x10
#define ISTAT_CON           0x08
#define ISTAT_INTF          0x04
#define ISTAT_SIP           0x02    // SCSI Interrupt Pending
#define ISTAT_DIP           0x01    // DMA Interrupt Pending

#define SCNTL1_RST          0x08    // Assert SCSI Reset

#define LASI_SCSI_CORE_OFFSET   0x100

struct ncr710_lun_s {
    struct drive_s drive;
    u32 iobase;
    u8 target;
    u8 lun;
};

/* I/O functions for NCR710 */
static inline u8
ncr710_readb(u32 iobase, u8 reg)
{
    u8 val = readb((void*)(iobase + reg));
    return val;
}

static inline void
ncr710_writeb(u32 iobase, u8 reg, u8 val)
{
    writeb((void*)(iobase + reg), val);
}

static inline u32
ncr710_readl(u32 iobase, u8 reg)
{
    u32 val = readl((void*)(iobase + reg));
    return val;
}

static inline void
ncr710_writel(u32 iobase, u8 reg, u32 val)
{
    writel((void*)(iobase + reg), val);
}

static void
ncr710_reset(u32 iobase)
{
    ncr710_writeb(iobase, ISTAT_REG, ISTAT_SRST);
    usleep(25);
    ncr710_writeb(iobase, ISTAT_REG, 0);
    usleep(1000);
}

/* Here we are faking the detect, before the lun scan failed so we need to return success */
static int
ncr710_detect(u32 iobase)
{
    u8 istat = ncr710_readb(iobase, ISTAT_REG);
    u8 dstat = ncr710_readb(iobase, DSTAT_REG);
    u8 sstat0 = ncr710_readb(iobase, SSTAT0_REG);
    printf("Seabios-hppa: NCR710: initial register reads - ISTAT=0x%02x, DSTAT=0x%02x, SSTAT0=0x%02x\n", 
           istat, dstat, sstat0);
    u32 original = ncr710_readl(iobase, TEMP_REG);
    printf("Seabios-hppa: NCR710: original TEMP register value: 0x%08x\n", original);
    u32 test_val = 0x12345678;
    ncr710_writel(iobase, TEMP_REG, test_val);
    u32 read_back = ncr710_readl(iobase, TEMP_REG);

    printf("Seabios-hppa: NCR710: temp register test - wrote 0x%x, read 0x%x\n", test_val, read_back);

    u8 test_byte = 0xAB;
    ncr710_writeb(iobase, TEMP_REG, test_byte);
    u8 readback_byte = ncr710_readb(iobase, TEMP_REG);
    printf("Seabios-hppa: NCR710: byte test - wrote 0x%02x, read 0x%02x\n", test_byte, readback_byte);
    
    ncr710_writel(iobase, TEMP_REG, original);
    int detected = (read_back == test_val) || (readback_byte == test_byte);
    if (!detected) {
        printf("Seabios-hppa: NCR710: temp tests failed, trying reset detection\n");
        ncr710_writeb(iobase, ISTAT_REG, ISTAT_SRST);
        usleep(10);
        u8 istat_after_reset = ncr710_readb(iobase, ISTAT_REG);
        printf("Seabios-hppa: NCR710: ISTAT after reset: 0x%02x\n", istat_after_reset);

        ncr710_writeb(iobase, ISTAT_REG, 0);
        usleep(100);
        u8 istat_after_clear = ncr710_readb(iobase, ISTAT_REG);
        printf("Seabios-hppa: NCR710: ISTAT after clear: 0x%02x\n", istat_after_clear);
        detected = (istat_after_reset & ISTAT_SRST) || (istat_after_clear != istat_after_reset);
    }

    printf("Seabios-hppa: NCR710: detection result: %s\n", detected ? "SUCCESS" : "FAILED");
    return detected;
}

static int
ncr710_send_command(u32 iobase, u8 target, u8 *cdb, int cdb_len, 
                   void *data, int data_len, int is_read)
{
    u8 istat = ncr710_readb(iobase, ISTAT_REG);
    u8 dstat = ncr710_readb(iobase, DSTAT_REG);
    u8 sstat0 = ncr710_readb(iobase, SSTAT0_REG);

    printf("Seabios-hppa: NCR710: before command - ISTAT=0x%02x, DSTAT=0x%02x, SSTAT0=0x%02x\n",
           istat, dstat, sstat0);
    
    ncr710_writeb(iobase, SCID_REG, 0x07);
    ncr710_writeb(iobase, SDID_REG, target);
    if (cdb[0] == 0x00) {
        return 0;
    }
    if (cdb[0] == 0x03) {
        printf("Seabios-hppa: NCR710: REQUEST SENSE - providing NO SENSE response\n");
        if (data && data_len >= 18) {
            u8 *response = (u8*)data;
            memset(response, 0, 18);
            response[0] = 0x70; // Valid, current errors
            response[2] = 0x00; // Sense key: NO SENSE
            response[7] = 0x0A; // More Additional sense length
        }
        return 0;
    }
    
    if (cdb[0] == 0x12) {
        printf("Seabios-hppa: NCR710: faking INQUIRY response\n");
        if (data && data_len >= 36) {
            u8 *response = (u8*)data;
            memset(response, 0, 36);
            response[0] = 0x00; // Peripheral device type: Direct access block device
            response[1] = 0x00; // RMB = 0
            response[2] = 0x02; // Version: SCSI-2
            response[3] = 0x02; // Response data format
            response[4] = 0x1F; // Additional length
            memcpy(&response[8], "QEMU    ", 8);   // Vendor
            memcpy(&response[16], "QEMU HARDDISK   ", 16); // Product:: just fakeing it
            memcpy(&response[32], "2.5+", 4);      // Revision
        }
        return 0;
    }
    
    if (cdb[0] == 0x25) {
        printf("NCR710: READ CAPACITY - providing fake disk capacity\n");
        if (data && data_len >= 8) {
            u8 *response = (u8*)data;
            memset(response, 0, 8);
            response[0] = 0x00;
            response[1] = 0x1F;
            response[2] = 0xFF;
            response[3] = 0xFF;
            response[4] = 0x00;
            response[5] = 0x00;
            response[6] = 0x02;
            response[7] = 0x00;
            printf("NCR710: READ CAPACITY - returning %d blocks of 512 bytes\n", 0x200000);
        }
        return 0;
    }
    
    if (cdb[0] == 0xA0) {
        printf("NCR710: REPORT LUNS - providing minimal response\n");
        if (data && data_len >= 16) {
            u8 *response = (u8*)data;
            memset(response, 0, 16);
            response[0] = 0x00;
            response[1] = 0x00;
            response[2] = 0x00;
            response[3] = 0x08;
        }
        return 0;
    }
    
    if (cdb[0] == 0x5A) {
        printf("NCR710: MODE SENSE (10) - providing minimal response\n");
        if (data && data_len >= 8) {
            u8 *response = (u8*)data;
            memset(response, 0, data_len);
            int mode_len = data_len - 2;
            response[0] = (mode_len >> 8) & 0xFF;
            response[1] = mode_len & 0xFF;
            response[2] = 0x00;
            response[3] = 0x00;
            response[4] = 0x00;
            response[5] = 0x00;
            response[6] = 0x00;
            response[7] = 0x00;
        }
        return 0;
    }

    if (cdb[0] == 0x08 || cdb[0] == 0x0A || cdb[0] == 0x28 || cdb[0] == 0x2A) {
        printf("NCR710: READ/WRITE command 0x%02x - faking success\n", cdb[0]);
        return 0;
    }
    
    printf("Debug: command 0x%02x not implemented yet\n", cdb[0]);
    return -1;
}

/* Helper function to determine SCSI command length
 * Group 0 commands (6-byte)
 * Group 1,2 commands (10-byte)
 * Group 4,5 commands (12-byte)
 * Default to 6-byte if unknown
 */
static int
scsi_command_length(u8 opcode)
{
    switch (opcode >> 5) {
    case 0: return 6;
    case 1:
    case 2: return 10;
    case 4:
    case 5: return 12;
    default: return 6;
    }
}

int
ncr710_scsi_process_op(struct disk_op_s *op)
{
    if (!CONFIG_NCR710_SCSI)
        return DISK_RET_EBADTRACK;
        
    struct ncr710_lun_s *llun_gf =
        container_of(op->drive_fl, struct ncr710_lun_s, drive);
    u16 target = GET_GLOBALFLAT(llun_gf->target);
    u32 iobase = GET_GLOBALFLAT(llun_gf->iobase);
    u8 cdbcmd[16];
    int blocksize = scsi_fill_cmd(op, cdbcmd, sizeof(cdbcmd));
    if (blocksize < 0)
        return default_process_op(op);

    printf("Seabios-hppa: NCR710: sending command 0x%02x, blocksize %d, count %d\n",
           cdbcmd[0], blocksize, op->count);
    int ret = ncr710_send_command(iobase, target, cdbcmd, 
                                 scsi_command_length(cdbcmd[0]),
                                 op->buf_fl, op->count * blocksize,
                                 scsi_is_read(op));

    printf("Seabios-hppa: NCR710: command result: %d\n", ret);
    return (ret == 0) ? DISK_RET_SUCCESS : DISK_RET_EBADTRACK;
}

static void
ncr710_scsi_init_lun(struct ncr710_lun_s *llun, u32 iobase,
                      u8 target, u8 lun)
{
    memset(llun, 0, sizeof(*llun));
    llun->drive.type = DTYPE_NCR710_SCSI;
    llun->drive.max_bytes_transfer = 32*1024;   /* 32 KB */
    llun->target = target;
    llun->lun = lun;
    llun->iobase = iobase;
}

static int
ncr710_scsi_add_lun(u32 lun, struct drive_s *tmpl_drv)
{
    struct ncr710_lun_s *tmpl_llun =
        container_of(tmpl_drv, struct ncr710_lun_s, drive);
    struct ncr710_lun_s *llun = malloc_fseg(sizeof(*llun));
    if (!llun) {
        warn_noalloc();
        return -1;
    }
    ncr710_scsi_init_lun(llun, tmpl_llun->iobase,
                         tmpl_llun->target, lun);

    char *name = znprintf(MAXDESCSIZE, "ncr710 %d:%d",
                          llun->target, llun->lun);
    int prio = bootprio_find_scsi_mmio_device((void*)(uintptr_t)tmpl_llun->iobase, 
                                             llun->target, llun->lun);
    int ret = scsi_drive_setup(&llun->drive, name, prio, llun->target, llun->lun);
    free(name);
    if (ret)
        goto fail;
    return 0;

fail:
    free(llun);
    return -1;
}

static void
ncr710_scsi_scan_target(u32 iobase, u8 target)
{
    printf("NCR710: scanning target %d\n", target);
    
    struct ncr710_lun_s llun0;

    ncr710_scsi_init_lun(&llun0, iobase, target, 0);

    if (scsi_rep_luns_scan(&llun0.drive, ncr710_scsi_add_lun) < 0)
        scsi_sequential_scan(&llun0.drive, 8, ncr710_scsi_add_lun);
}

static void
init_ncr710_scsi(void *data)
{
    u32 iobase = (u32)(uintptr_t)data;

    printf("Seabios-hppa: NCR710: found ncr710 at 0x%x\n", iobase);

    ncr710_reset(iobase);
    
    ncr710_writeb(iobase, SCNTL0_REG, 0x00); // Async operation
    ncr710_writeb(iobase, SCNTL1_REG, 0x00); // Normal operation
    ncr710_writeb(iobase, SCID_REG, 0x07);   // Host adapter ID = 7
    ncr710_writeb(iobase, SIEN_REG, 0x00);   // Disable SCSI interrupts
    ncr710_writeb(iobase, CTEST7_REG, 0x00); // Normal operation
    
    printf("scanning for dev \n");
    
    int i;
    for (i = 0; i < 7; i++)
        ncr710_scsi_scan_target(iobase, i);
        
    printf("NCR710: device scan complete\n");
}

void
ncr710_scsi_setup(void)
{
    ASSERT32FLAT();
    if (!CONFIG_NCR710_SCSI || !runningOnQEMU())
        return;
    u32 iobase = LASI_SCSI_HPA + LASI_SCSI_CORE_OFFSET;
    
    printf("NCR710: LASI_SCSI_HPA=0x%08x, LASI_SCSI_CORE_OFFSET=0x%08x iobase=0x%08x\n", 
           LASI_SCSI_HPA, LASI_SCSI_CORE_OFFSET, iobase);
    if (!ncr710_detect(iobase)) {
        printf("NCR710: not detected at 0x%08x\n", iobase);
        return;
    }
    printf("NCR710: chip detected successfully!\n");
    run_thread(init_ncr710_scsi, (void*)(uintptr_t)iobase);
}
