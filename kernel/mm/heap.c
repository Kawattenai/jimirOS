#include <kernel/kmalloc.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <string.h>

#define PAGE_SIZE 4096u

static uint8_t* heap_cur;
static uint8_t* heap_end;

static void map_more(size_t bytes) {
    size_t need = (size_t)(heap_cur + bytes - heap_end);
    if ((intptr_t)need <= 0) return;
    /* Map pages [heap_end, heap_end+need) */
    uint32_t v = (uint32_t)heap_end;
    size_t pages = (need + PAGE_SIZE - 1) / PAGE_SIZE;
    for (size_t i = 0; i < pages; ++i, v += PAGE_SIZE) {
        uint32_t phys = pmm_alloc_frame();
        if (!phys) break; /* OOM */
        vmm_map(v, phys, PAGE_PRESENT | PAGE_WRITE);
    }
    heap_end = (uint8_t*)(((uintptr_t)heap_end + pages*PAGE_SIZE));
}

void kmalloc_init(void* base, size_t size) {
    heap_cur = (uint8_t*)base;
    heap_end = (uint8_t*)base;
    map_more(size);
}

void* kmalloc(size_t sz) {
    if (sz == 0) return NULL;
    /* 16-byte align */
    uintptr_t p = (uintptr_t)heap_cur;
    p = (p + 15) & ~((uintptr_t)15);
    uint8_t* ret = (uint8_t*)p;
    size_t bump = (size_t)(ret + sz - heap_cur);
    map_more(bump);
    heap_cur = ret + sz;
    return ret;
}

void* kcalloc(size_t n, size_t sz) {
    size_t total = n * sz;
    void* p = kmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void* krealloc(void* ptr, size_t sz) {
    if (!ptr) return kmalloc(sz);
    /* naive: allocate new and copy small header is not tracked; just allocate new */
    void* p = kmalloc(sz);
    return p; /* caller must copy if desired */
}

void kfree(void* p) {
    (void)p; /* bump allocator: no free */
}
