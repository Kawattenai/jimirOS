#ifndef _KERNEL_PANIC_H
#define _KERNEL_PANIC_H

#include <stdnoreturn.h>

noreturn void panic(const char* fmt, ...);

#define assert(x) do { if (!(x)) panic("Assertion failed: %s", #x); } while (0)

#endif
