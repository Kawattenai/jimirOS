#ifndef KERNEL_ATA_H
#define KERNEL_ATA_H

#include <stdint.h>

int ata_init(void);
int ata_read_sectors(uint32_t lba, uint8_t count, void* buffer);
int ata_write_sectors(uint32_t lba, uint8_t count, const void* buffer);

#endif
