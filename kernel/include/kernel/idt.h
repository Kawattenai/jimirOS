#ifndef _KERNEL_IDT_H
#define _KERNEL_IDT_H

#include <stdint.h>

/**
 * @brief An entry in the Interrupt Descriptor Table.
 * Total size: 8 bytes.
 */
struct IdtEntry {
    uint16_t base_low;      // Lower 16 bits of handler function address
    uint16_t selector;      // Kernel Code Segment selector (from your GDT)
    uint8_t  zero;          // Must be zero
    uint8_t  flags;         // Type and attributes (e.g., 0x8E for 32-bit trap gate)
    uint16_t base_high;     // Upper 16 bits of handler function address
} __attribute__((packed));

/**
 * @brief A pointer to the IDT, used by the 'lidt' instruction.
 * Total size: 6 bytes.
 */
struct IdtPtr {
    uint16_t limit;         // Size of IDT (limit - 1)
    uint32_t base;          // Linear address of the IDT
} __attribute__((packed));

/**
 * @brief Represents the CPU state saved on the stack during an interrupt.
 * This structure is read by the C-level interrupt handlers.
 */
struct registers {
    uint32_t ds;                                    // Data segment selector
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; // Pushed by pusha
    uint32_t int_num, err_code;                     // Pushed by our ISR/IRQ stub
    uint32_t eip, cs, eflags, useresp, ss;          // Pushed by the CPU automatically
} __attribute__((packed));

/**
 * @brief Initializes and loads the IDT with ISR (exception) handlers.
 */
void idt_initialize(void);

/**
 * @brief A helper function to set a single entry in the IDT.
 *
 * @param index The IDT vector number (0-255).
 * @param base The 32-bit address of the interrupt handler function.
 * @param selector The kernel code segment selector (0x08).
 * @param flags The type and attributes byte (e.g., 0x8E).
 */
void idt_set_entry(int index, uint32_t base, uint16_t selector, uint8_t flags);

#endif