/* include/kernel/stdio.h */
#ifndef _KERNEL_STDIO_H
#define _KERNEL_STDIO_H

/* You must include <stdarg.h> to use va_list */
#include <stdarg.h>

/**
 * The public-facing kernel printf function.
 */
int printf(const char* format, ...);

/**
 * The core vprintf implementation.
 */
int vprintf(const char* format, va_list args);

#endif