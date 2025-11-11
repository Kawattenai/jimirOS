#ifndef _KERNEL_GDT_H
#define _KERNEL_GDT_H

#include <stdint.h>

/* GDT Entry Structure (8 bytes) */
struct GdtEntry {
    uint16_t limit_low;     // The lower 16 bits of the limit.
    uint16_t base_low;      // The lower 16 bits of the base.
    uint8_t  base_middle;   // The next 8 bits of the base.
    uint8_t  access;        // Access flags (determines ring, type).
    uint8_t  granularity;   // Granularity (limit, flags)
    uint8_t  base_high;     // The last 8 bits of the base.
} __attribute__((packed)); // 'packed' prevents compiler padding.

/* GDT Pointer Structure (6 bytes) */
struct GdtPtr {
    uint16_t limit;         // The size of the GDT (limit - 1).
    uint32_t base;          // The linear address of the GDT.
} __attribute__((packed));

/* Segment selectors */
#define KERNEL_CS 0x08
#define KERNEL_DS 0x10
#define USER_CS   0x1B
#define USER_DS   0x23

/* 32-bit TSS structure */
struct TssEntry {
    uint32_t prev_tss;
    uint32_t esp0; uint32_t ss0;
    uint32_t esp1; uint32_t ss1;
    uint32_t esp2; uint32_t ss2;
    uint32_t cr3;  uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es; uint32_t cs; uint32_t ss; uint32_t ds; uint32_t fs; uint32_t gs;
    uint32_t ldt;
    uint16_t trap; uint16_t iomap_base;
} __attribute__((packed));

/* Set esp0 for ring transitions */
void tss_set_kernel_stack(uint32_t esp0);

/**
 * @brief Initializes and loads the GDT.
 */
void gdt_initialize(void);

#endif