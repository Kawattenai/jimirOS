/* kernel/gdt.c */
#include <kernel/gdt.h>

/* Forward declaration for the assembly function we will create. */
/* This function will load our GDT. */
extern void gdt_load(struct GdtPtr* gdt_ptr);
extern void tss_load(uint16_t selector);

#define GDT_ENTRIES 6

struct GdtEntry gdt[GDT_ENTRIES];
struct GdtPtr   gdt_ptr;

static struct TssEntry tss_entry;

/* Helper function to create a GDT entry */
void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[index].base_low    = (base & 0xFFFF);
    gdt[index].base_middle = (base >> 16) & 0xFF;
    gdt[index].base_high   = (base >> 24) & 0xFF;

    gdt[index].limit_low   = (limit & 0xFFFF);
    gdt[index].granularity = (limit >> 16) & 0x0F;

    gdt[index].granularity |= gran & 0xF0;
    gdt[index].access      = access;
}

void gdt_initialize(void) {
    // Set up the GDT pointer
    gdt_ptr.limit = (sizeof(struct GdtEntry) * GDT_ENTRIES) - 1;
    gdt_ptr.base  = (uint32_t)&gdt; // Address of the GDT array

    // 1. NULL Descriptor (required)
    gdt_set_entry(0, 0, 0, 0, 0);

    // 2. Kernel Code Segment (Ring 0)
    // Access: 0x9A (Present, Ring 0, Code, Executable, Read/Write)
    // Gran:   0xCF (4K Page Granularity, 32-bit)
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    // 3. Kernel Data Segment (Ring 0)
    // Access: 0x92 (Present, Ring 0, Data, Read/Write)
    // Gran:   0xCF (4K Page Granularity, 32-bit)
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    // 4. User Code Segment (Ring 3)
    // Access: 0xFA (Present, Ring 3, Code, Readable)
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);

    // 5. User Data Segment (Ring 3)
    // Access: 0xF2 (Present, Ring 3, Data, Read/Write)
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    // 6. TSS (32-bit Available TSS, type=0x9, access=0x89)
    uint32_t base = (uint32_t)&tss_entry;
    uint32_t limit = sizeof(struct TssEntry) - 1;
    gdt_set_entry(5, base, limit, 0x89, 0x00);

    // Initialize TSS minimal fields
    for (unsigned i=0;i<sizeof(struct TssEntry)/4;i++) ((uint32_t*)&tss_entry)[i]=0;
    tss_entry.ss0 = KERNEL_DS;
    tss_entry.cs  = USER_CS;
    tss_entry.ss  = USER_DS;
    tss_entry.ds  = USER_DS;
    tss_entry.es  = USER_DS;
    tss_entry.fs  = USER_DS;
    tss_entry.gs  = USER_DS;
    tss_entry.iomap_base = sizeof(struct TssEntry);

    // Load the GDT
    gdt_load(&gdt_ptr);

    // Load the TSS
    __asm__ volatile ("ltr %%ax" :: "a"(5<<3));
}

void tss_set_kernel_stack(uint32_t esp0) {
    tss_entry.esp0 = esp0;
}