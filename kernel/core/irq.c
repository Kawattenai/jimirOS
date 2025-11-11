#include <kernel/idt.h>
#include <kernel/pic.h>
#include <kernel/stdio.h>
#include <kernel/keyboard.h>
#include <kernel/pit.h>
#include <kernel/ports.h>
#include <kernel/sched.h>
#include <kernel/process.h>

/* Forward declare stubs from irq.S */
extern void irq0();
extern void irq1();
/* ... and so on ... */

static void timer_handler(struct registers* regs) {
    pit_on_tick();
    sched_tick();
    
    /* Poll USB devices */
    extern void usb_poll(void);
    usb_poll();
    
    process_schedule(regs);  // Schedule user processes
}

static void keyboard_handler(void) {
    uint8_t scancode = inb(0x60);
    keyboard_on_scancode(scancode);
}

/**
 * @brief This is the C-level handler called from irq_common_stub
 *
 * --- THIS IS THE FIX ---
 * The signature is changed from 'struct registers regs'
 * to 'struct registers* regs'.
 */
void irq_handler(struct registers* regs) {
    /* The int_num is the IDT vector (32-47). We must subtract 32
       to get the actual IRQ number (0-15) for the PIC. */
    uint8_t irq_num = regs->int_num - 32;

    /* Handle the specific IRQ */
    /* We now use '->' (pointer) instead of '.' (value) */
    switch (irq_num) {
        case 0: /* IRQ 0: Timer */
            timer_handler(regs);
            break;
        case 1: /* IRQ 1: Keyboard */
            keyboard_handler();
            break;
        default:
            printf("Unhandled IRQ: %d\n", irq_num);
    }
    /* Acknowledge the interrupt by sending EOI to the PIC */
    pic_send_eoi(irq_num);
}

/**
 * @brief Installs the IRQ handlers into the IDT.
 */
void irq_install(void) {
    /* (This function should be called from idt_initialize) */
    
    /* 0x08 is Kernel Code Segment */
    /* 0x8E is 32-bit Interrupt Gate */
    idt_set_entry(32, (uint32_t)irq0, 0x08, 0x8E);
    idt_set_entry(33, (uint32_t)irq1, 0x08, 0x8E);
    /* ... and so on ... */

    /* Enable (unmask) Timer (IRQ 0) and Keyboard (IRQ 1) */
    /* 0xFC = 11111100 (unmask 0 and 1) */
    outb(PIC1_DATA, inb(PIC1_DATA) & 0xFC);
}
