#ifndef _KERNEL_VMM_H
#define _KERNEL_VMM_H

#include <stdint.h>

#define PAGE_PRESENT 0x001
#define PAGE_WRITE   0x002
#define PAGE_USER    0x004

void vmm_init(void);
int  vmm_map(uint32_t virt, uint32_t phys, uint32_t flags);
int  vmm_unmap(uint32_t virt);
uint32_t vmm_resolve(uint32_t virt);

#endif
