#ifndef KERNEL_BLOCK_H
#define KERNEL_BLOCK_H

#include <stdint.h>

int block_init(void);
int block_is_ready(void);
int block_read(uint32_t lba, uint8_t count, void* buffer);
int block_write(uint32_t lba, uint8_t count, const void* buffer);

#endif
