#include <kernel/stdio.h>
#include <stdint.h>

/* Simple global canary; in a real system use a randomized value. */
uintptr_t __stack_chk_guard = 0x595e9fbdU;

void __attribute__((noreturn)) __stack_chk_fail(void) {
    printf("\n[KERNEL] stack smashing detected. Halting.\n");
    for(;;){ __asm__ volatile("cli; hlt"); }
}
