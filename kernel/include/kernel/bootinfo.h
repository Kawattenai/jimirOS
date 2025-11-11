#ifndef _KERNEL_BOOTINFO_H
#define _KERNEL_BOOTINFO_H

#include <stdint.h>

void bootinfo_set_mb(uint32_t mb_high_addr);
int  bootinfo_first_module(void** start, uint32_t* size);
int  bootinfo_module_count(void);
int  bootinfo_get_module(int index, void** start, uint32_t* size, const char** name);

#endif
