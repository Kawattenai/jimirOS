#ifndef _KERNEL_AHCI_H
#define _KERNEL_AHCI_H

#include <stdint.h>

int ahci_init(void);
int ahci_read(uint32_t lba, uint8_t count, void* buffer);
int ahci_write(uint32_t lba, uint8_t count, const void* buffer);

#endif
