#ifndef _KERNEL_KEYBOARD_H
#define _KERNEL_KEYBOARD_H

#include <stdint.h>
#include <stddef.h>

enum {
	KEY_NONE   = -1,
	KEY_LEFT   = 0x100,
	KEY_RIGHT  = 0x101,
	KEY_UP     = 0x102,
	KEY_DOWN   = 0x103,
	KEY_HOME   = 0x104,
	KEY_END    = 0x105,
	KEY_DELETE = 0x106,
	KEY_PAGE_UP   = 0x107,
	KEY_PAGE_DOWN = 0x108,
	KEY_SCROLL_UP   = 0x109,
	KEY_SCROLL_DOWN = 0x10A,
};

void keyboard_init(void);
void keyboard_on_scancode(uint8_t sc);
int  kbd_getch(void);      /* returns -1 if none; ASCII or KEY_* above */

#endif
