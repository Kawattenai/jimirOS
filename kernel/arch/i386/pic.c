#include <kernel/ports.h>
#include <kernel/pic.h>   /* <-- ADD THIS */

/* --- REMOVE THESE LINES (they are now in pic.h) --- */
/*
#define PIC1        0x20
#define PIC2        0xA0
#define PIC1_CMD    PIC1
#define PIC1_DATA   (PIC1+1)
#define PIC2_CMD    PIC2
#define PIC2_DATA   (PIC2+1)
#define PIC_EOI     0x20
*/
/* --- END OF REMOVAL --- */


/* These are still needed locally for init */
#define ICW1_ICW4   0x01
#define ICW1_INIT   0x10
#define ICW4_8086   0x01

/*
 * Remaps the PIC to use IDT offsets 32-47.
 */
void pic_remap(void) {
    uint8_t a1, a2;

    a1 = inb(PIC1_DATA);
    a2 = inb(PIC2_DATA);

    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);

    outb(PIC1_DATA, 32); 
    outb(PIC2_DATA, 40); 

    outb(PIC1_DATA, 4);
    outb(PIC2_DATA, 2);

    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);
    }
    outb(PIC1_CMD, PIC_EOI);
}