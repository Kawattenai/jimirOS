#ifndef _KERNEL_PIC_H
#define _KERNEL_PIC_H

/* --- ADD THESE DEFINITIONS --- */
#define PIC1        0x20        /* IO base address for master PIC */
#define PIC2        0xA0        /* IO base address for slave PIC */
#define PIC1_CMD    PIC1
#define PIC1_DATA   (PIC1+1)
#define PIC2_CMD    PIC2
#define PIC2_DATA   (PIC2+1)

#define PIC_EOI     0x20        /* End-of-interrupt command code */
/* --- END OF ADDITIONS --- */

void pic_remap(void);
void pic_send_eoi(uint8_t irq);

#endif
