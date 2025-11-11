#include <kernel/system.h>
#include <kernel/ports.h>

void cpu_halt(void) {
    __asm__ volatile ("cli; hlt");
}

void cpu_reboot(void) {
    /* Try keyboard controller reset */
    /* Wait until input buffer is empty */
    while (inb(0x64) & 0x02) { }
    outb(0x64, 0xFE);
    /* Fallback: triple fault */
    __asm__ volatile ("lidt (0); int3");
    for(;;) { __asm__ volatile ("hlt"); }
}
