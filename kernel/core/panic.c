#include <kernel/panic.h>
#include <kernel/stdio.h>
#include <stdarg.h>

noreturn void panic(const char* fmt, ...) {
    printf("\n--- KERNEL PANIC ---\n");
    if (fmt) {
        va_list ap; va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }
    printf("\nSystem halted.\n");
    for(;;) {
        __asm__ volatile("cli; hlt");
    }
}
