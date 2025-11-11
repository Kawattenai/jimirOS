#include <kernel/pmm.h>
#include <kernel/multiboot.h>
#include <kernel/stdio.h>
#include <kernel/panic.h>

#define FRAME_SIZE 4096u
#define MAX_MEMORY_BYTES (256u * 1024u * 1024u) /* 256 MiB cap for bitmap */
#define MAX_FRAMES (MAX_MEMORY_BYTES / FRAME_SIZE)

static uint32_t total_frames = 0;
static uint32_t free_frames_cnt = 0;
static uint32_t bitmap[MAX_FRAMES / 32]; /* 1 bit per frame */

extern uint32_t kernel_phys_start; /* from linker */
extern uint32_t kernel_phys_end;   /* from linker */
extern uint32_t boot_start;        /* low bootstrap start */
extern uint32_t boot_end;          /* low bootstrap end */

static inline void bm_set(uint32_t idx)   { bitmap[idx >> 5] |=  (1u << (idx & 31)); }
static inline void bm_clear(uint32_t idx) { bitmap[idx >> 5] &= ~(1u << (idx & 31)); }
static inline int  bm_test(uint32_t idx)  { return (bitmap[idx >> 5] >> (idx & 31)) & 1u; }

static void reserve_region(uint32_t start_phys, uint32_t end_phys) {
    if (end_phys <= start_phys) return;
    uint32_t start_frame = start_phys / FRAME_SIZE;
    uint32_t end_frame   = (end_phys + FRAME_SIZE - 1) / FRAME_SIZE;
    if (end_frame > total_frames) end_frame = total_frames;
    for (uint32_t f = start_frame; f < end_frame; ++f) {
        if (!bm_test(f)) { bm_set(f); if (free_frames_cnt) free_frames_cnt--; }
    }
}

void pmm_init(uint32_t multiboot_info_addr_high) {
    /* Zero bitmap */
    for (size_t i = 0; i < (MAX_FRAMES / 32); ++i) bitmap[i] = 0;

    multiboot_info_t* mb = (multiboot_info_t*)multiboot_info_addr_high;

    /* Determine physical memory upper bound using mmap if present */
    uint64_t max_addr = 0;
    if (mb->flags & (1u << 6)) {
        uint32_t mmap_base = mb->mmap_addr + 0xC0000000u; /* higher-half alias */
        uint32_t mmap_end  = mmap_base + mb->mmap_length;
        for (uint32_t p = mmap_base; p < mmap_end;) {
            multiboot_mmap_entry_t* e = (multiboot_mmap_entry_t*)p;
            uint64_t e_end = e->addr + e->len;
            if (e_end > max_addr) max_addr = e_end;
            p += e->size + 4; /* next */
        }
    } else {
        /* Fallback on mem_upper from multiboot (in KiB) */
        max_addr = (uint64_t)(mb->mem_upper + 1024u) * 1024u; /* add 1MiB low */
    }

    if (max_addr > MAX_MEMORY_BYTES) max_addr = MAX_MEMORY_BYTES;
    total_frames = (uint32_t)(max_addr / FRAME_SIZE);
    free_frames_cnt = total_frames;

    /* Mark all non-available areas as used via mmap */
    if (mb->flags & (1u << 6)) {
        uint32_t mmap_base = mb->mmap_addr + 0xC0000000u;
        uint32_t mmap_end  = mmap_base + mb->mmap_length;
        for (uint32_t p = mmap_base; p < mmap_end;) {
            multiboot_mmap_entry_t* e = (multiboot_mmap_entry_t*)p;
            if (e->type != 1 /* available */) {
                reserve_region((uint32_t)e->addr, (uint32_t)(e->addr + e->len));
            }
            p += e->size + 4;
        }
    }

    /* Reserve critical regions: 0..1MiB, low bootstrap, kernel image, VGA text */
    reserve_region(0, 0x100000);
    reserve_region((uint32_t)&boot_start, (uint32_t)&boot_end);
    reserve_region((uint32_t)&kernel_phys_start, (uint32_t)&kernel_phys_end);
    reserve_region(0xB8000, 0xB8000 + 0x1000);

    /* Mark multiboot modules (e.g. initial ramdisk / rootfs) as reserved so
       they are never handed out as general-purpose frames. Otherwise userland
       writes after fork()/exec can corrupt the module image. */
    if ((mb->flags & (1u << 3)) && mb->mods_count) {
        multiboot_module_t* mods = (multiboot_module_t*)(mb->mods_addr + 0xC0000000u);
        for (uint32_t i = 0; i < mb->mods_count; ++i) {
            reserve_region(mods[i].mod_start, mods[i].mod_end);
        }
    }

    /* Print totals without floats to avoid unsupported format specifiers */
    uint32_t mib = (total_frames * FRAME_SIZE) / (1024u*1024u);
    printf("PMM: total=%u frames (%u MiB), free=%u\n", total_frames, mib, free_frames_cnt);
}

uint32_t pmm_total_frames(void) { return total_frames; }
uint32_t pmm_free_frames(void)  { return free_frames_cnt; }

uint32_t pmm_alloc_frame(void) {
    for (uint32_t idx = 0; idx < total_frames; ++idx) {
        if (!bm_test(idx)) { bm_set(idx); if (free_frames_cnt) free_frames_cnt--; return idx * FRAME_SIZE; }
    }
    return 0; /* OOM */
}

uint32_t pmm_alloc_frame_below(uint32_t max_phys) {
    uint32_t max_idx = max_phys / FRAME_SIZE;
    if (max_idx > total_frames) max_idx = total_frames;
    for (uint32_t idx = 0; idx < max_idx; ++idx) {
        if (!bm_test(idx)) {
            bm_set(idx);
            if (free_frames_cnt) free_frames_cnt--;
            return idx * FRAME_SIZE;
        }
    }
    return 0; /* none available below threshold */
}

void pmm_free_frame(uint32_t frame_phys) {
    uint32_t idx = frame_phys / FRAME_SIZE;
    if (idx >= total_frames) return;
    if (bm_test(idx)) { bm_clear(idx); free_frames_cnt++; }
}
