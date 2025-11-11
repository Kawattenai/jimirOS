#ifndef _KERNEL_PMM_H
#define _KERNEL_PMM_H

#include <stdint.h>
#include <stddef.h>

void pmm_init(uint32_t multiboot_info_addr_high);
uint32_t pmm_total_frames(void);
uint32_t pmm_free_frames(void);
uint32_t pmm_alloc_frame(void);
/* Allocate a physical frame below a max physical address (e.g., 4 MiB) */
uint32_t pmm_alloc_frame_below(uint32_t max_phys);
void pmm_free_frame(uint32_t frame_phys);

#endif
