#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/stdio.h>

#define PAGE_SIZE 4096u
#define PD_ENTRIES 1024
#define PT_ENTRIES 1024

static inline uint32_t read_cr3(void) {
    uint32_t cr3; __asm__ volatile("mov %%cr3,%0":"=r"(cr3)); return cr3; }

static inline void invlpg(uint32_t v) { __asm__ volatile("invlpg (%0)"::"r"(v):"memory"); }

static uint32_t* pd_ptr(void) {
    return (uint32_t*)read_cr3();
}

static uint32_t* get_pt(uint32_t* pd, uint32_t v, int create, uint32_t flags) {
    uint32_t pd_idx = (v >> 22) & 0x3FF;
    uint32_t pde = pd[pd_idx];
    if (!(pde & PAGE_PRESENT)) {
        if (!create) return 0;
        uint32_t pt_phys = pmm_alloc_frame_below(0x01000000u);
        if (!pt_phys) {
            return 0;
        }
        uint32_t* pt = (uint32_t*)pt_phys;
        for (int i = 0; i < PT_ENTRIES; ++i) pt[i] = 0;
        pd[pd_idx] = pt_phys | (flags & (PAGE_WRITE|PAGE_USER)) | PAGE_PRESENT;
        return (uint32_t*)pt_phys;
    }
    if ((flags & PAGE_USER) && !(pde & PAGE_USER)) {
        pd[pd_idx] |= PAGE_USER;
    }
    if ((flags & PAGE_WRITE) && !(pde & PAGE_WRITE)) {
        pd[pd_idx] |= PAGE_WRITE;
    }
    return (uint32_t*)(pde & ~0xFFFu);
}

void vmm_init(void) {
}

int vmm_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t* pd = pd_ptr();
    uint32_t* pt = get_pt(pd, virt, 1, flags);
    if (!pt) return -1;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;
    uint32_t entry = (phys & ~0xFFFu) | (flags & (PAGE_WRITE|PAGE_USER)) | PAGE_PRESENT;
    pt[pt_idx] = entry;
    invlpg(virt);
    return 0;
}

int vmm_unmap(uint32_t virt) {
    uint32_t* pd = pd_ptr();
    uint32_t pd_idx = (virt >> 22) & 0x3FF;
    uint32_t pde = pd[pd_idx];
    if (!(pde & PAGE_PRESENT)) return 0;
    uint32_t* pt = (uint32_t*)(pde & ~0xFFFu);
    uint32_t pt_idx = (virt >> 12) & 0x3FF;
    pt[pt_idx] = 0;
    invlpg(virt);
    return 0;
}

uint32_t vmm_resolve(uint32_t virt) {
    uint32_t* pd = pd_ptr();
    uint32_t pd_idx = (virt >> 22) & 0x3FF;
    uint32_t pde = pd[pd_idx];
    if (!(pde & PAGE_PRESENT)) return 0;
    uint32_t* pt = (uint32_t*)(pde & ~0xFFFu);
    uint32_t pt_idx = (virt >> 12) & 0x3FF;
    uint32_t pte = pt[pt_idx];
    if (!(pte & PAGE_PRESENT)) return 0;
    return (pte & ~0xFFFu) | (virt & 0xFFFu);
}
