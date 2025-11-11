#ifndef _KERNEL_SERIAL_H
#define _KERNEL_SERIAL_H

#include <stdint.h>

#define COM1_PORT 0x3F8

void serial_init(void);
void serial_putchar(char c);
void serial_writestring(const char* s);
int  serial_available(void);
int  serial_getchar(void); /* returns -1 if no data */

#endif
