#include <kernel/block.h>
#include <kernel/ata.h>
#include <kernel/ahci.h>
#include <kernel/stdio.h>

enum {
    BLOCK_DRIVER_NONE = 0,
    BLOCK_DRIVER_AHCI,
    BLOCK_DRIVER_ATA,
};

static int g_block_ready = 0;
static int g_block_driver = BLOCK_DRIVER_NONE;

int block_init(void) {
    if (g_block_ready) return 0;

    if (ahci_init() == 0) {
        g_block_driver = BLOCK_DRIVER_AHCI;
        g_block_ready = 1;
        return 0;
    }

    if (ata_init() == 0) {
        g_block_driver = BLOCK_DRIVER_ATA;
        g_block_ready = 1;
        return 0;
    }

    printf("block: no storage controller detected\n");
    return -1;
}

int block_is_ready(void) {
    return g_block_ready;
}

int block_read(uint32_t lba, uint8_t count, void* buffer) {
    if (!g_block_ready) return -1;
    switch (g_block_driver) {
        case BLOCK_DRIVER_AHCI:
            return ahci_read(lba, count, buffer);
        case BLOCK_DRIVER_ATA:
            return ata_read_sectors(lba, count, buffer);
        default:
            return -1;
    }
}

int block_write(uint32_t lba, uint8_t count, const void* buffer) {
    if (!g_block_ready) return -1;
    switch (g_block_driver) {
        case BLOCK_DRIVER_AHCI:
            return ahci_write(lba, count, buffer);
        case BLOCK_DRIVER_ATA:
            return ata_write_sectors(lba, count, buffer);
        default:
            return -1;
    }
}
