#include <kernel/ata.h>
#include <kernel/ports.h>
#include <kernel/stdio.h>

#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CTRL    0x3F6

#define ATA_REG_DATA        0x00
#define ATA_REG_ERROR       0x01
#define ATA_REG_FEATURES    0x01
#define ATA_REG_SECCOUNT0   0x02
#define ATA_REG_LBA0        0x03
#define ATA_REG_LBA1        0x04
#define ATA_REG_LBA2        0x05
#define ATA_REG_HDDEVSEL    0x06
#define ATA_REG_COMMAND     0x07
#define ATA_REG_STATUS      0x07

#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF   0x20
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30

static inline void ata_io_delay(void) {
    inb(ATA_PRIMARY_CTRL);
    inb(ATA_PRIMARY_CTRL);
    inb(ATA_PRIMARY_CTRL);
    inb(ATA_PRIMARY_CTRL);
}

static int ata_wait_busy(void) {
    for (int i = 0; i < 100000; ++i) {
        uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) return 0;
        ata_io_delay();
    }
    printf("ata: timeout waiting for not busy\n");
    return -1;
}

static int ata_wait_drq(void) {
    for (int i = 0; i < 100000; ++i) {
        uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) { printf("ata: error status=0x%x\n", status); return -1; }
        if (status & ATA_SR_DRQ) return 0;
        ata_io_delay();
    }
    printf("ata: timeout waiting for DRQ\n");
    return -1;
}

int ata_init(void) {
    outb(ATA_PRIMARY_CTRL, 0x02); /* disable IRQ (nIEN) */
    ata_io_delay();
    if (ata_wait_busy() != 0) return -1;
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xE0);
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA0, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA1, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA2, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, 0xEC);
    if (ata_wait_busy() != 0) return -1;
    if (ata_wait_drq() != 0) return -1;
    for (int i = 0; i < 256; ++i) {
        (void)inw(ATA_PRIMARY_IO + ATA_REG_DATA);
    }
    printf("ata: primary master initialized\n");
    return 0;
}

int ata_read_sectors(uint32_t lba, uint8_t count, void* buffer) {
    if (!count) return 0;
    if (ata_wait_busy() != 0) return -1;
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, count);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA0, (uint8_t)(lba));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA1, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA2, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);

    uint16_t* buf16 = (uint16_t*)buffer;
    for (uint8_t s = 0; s < count; ++s) {
        if (ata_wait_drq() != 0) return -1;
        for (int i = 0; i < 256; ++i) {
            buf16[s * 256 + i] = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
        }
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const void* buffer) {
    if (!count) return 0;
    if (ata_wait_busy() != 0) return -1;
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, count);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA0, (uint8_t)(lba));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA1, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA2, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);

    const uint16_t* buf16 = (const uint16_t*)buffer;
    for (uint8_t s = 0; s < count; ++s) {
        if (ata_wait_drq() != 0) return -1;
        for (int i = 0; i < 256; ++i) {
            outw(ATA_PRIMARY_IO + ATA_REG_DATA, buf16[s * 256 + i]);
        }
    }
    if (ata_wait_busy() != 0) return -1;
    return 0;
}
