#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/tty.h>

/* We must include vga.h from its *new* location */
#include "vga.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define SCROLLBACK_LINES 256

/*
 * --- THIS IS THE FIX ---
 * The VGA buffer is *physically* at 0xB8000.
 * Our boot-time page table mapped Virtual 0-4MB to Physical 0-4MB,
 * AND Virtual 0xC0000000-0xC0400000 to Physical 0-4MB.
 *
 * Therefore, we can access the VGA buffer at *two* addresses:
 * 1. 0xB8000 (low identity map)
 * 2. 0xC00B8000 (high kernel map)
 *
 * Since our kernel is running in the higher half, we *must*
 * use the high address.
 */
static uint16_t* const VGA_MEMORY = (uint16_t*) 0xC00B8000;

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static uint16_t* terminal_buffer;

static uint16_t terminal_screen[VGA_WIDTH * VGA_HEIGHT];
static uint16_t scrollback[SCROLLBACK_LINES][VGA_WIDTH];
static size_t scrollback_head;
static size_t scrollback_count;
static size_t display_offset;

static void terminal_render(void);
static void scrollback_push_line(const uint16_t* line);
static size_t scrollback_base_index(void);

void terminal_initialize(void) {
	terminal_row = 0;
	terminal_column = 0;
	terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_buffer = VGA_MEMORY;
    scrollback_head = 0;
    scrollback_count = 0;
    display_offset = 0;
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_screen[index] = vga_entry(' ', terminal_color);
        }
    }
    terminal_render();
}

/* ... The rest of tty.c (terminal_setcolor, terminal_putentryat, etc.)
   remains exactly the same. It will now use the 0xC00B8000 pointer. ... */

void terminal_setcolor(uint8_t color) {
	terminal_color = color;
}

void terminal_putentryat(unsigned char c, uint8_t color, size_t x, size_t y) {
    const size_t index = y * VGA_WIDTH + x;
    terminal_screen[index] = vga_entry(c, color);
    if (display_offset == 0) {
        terminal_buffer[index] = terminal_screen[index];
    }
}

/*
 * This function must be updated to handle scrolling
 * and newlines, as we discussed previously.
 */
static void scrollback_push_line(const uint16_t* line) {
    memcpy(scrollback[scrollback_head], line, VGA_WIDTH * sizeof(uint16_t));
    scrollback_head = (scrollback_head + 1) % SCROLLBACK_LINES;
    if (scrollback_count < SCROLLBACK_LINES) {
        scrollback_count++;
    }
}

static size_t scrollback_base_index(void) {
    if (scrollback_count == 0) {
        return 0;
    }
    return (scrollback_head + SCROLLBACK_LINES - scrollback_count) % SCROLLBACK_LINES;
}

static void terminal_render(void) {
    if (display_offset > scrollback_count) {
        display_offset = scrollback_count;
    }
    const size_t total_lines = scrollback_count + VGA_HEIGHT;
    size_t start_line = 0;
    if (total_lines > VGA_HEIGHT) {
        start_line = total_lines - VGA_HEIGHT - display_offset;
    }
    const size_t base = scrollback_base_index();
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        size_t line_index = start_line + y;
        uint16_t* dest = &terminal_buffer[y * VGA_WIDTH];
        if (line_index < scrollback_count) {
            size_t idx = (base + line_index) % SCROLLBACK_LINES;
            memcpy(dest, scrollback[idx], VGA_WIDTH * sizeof(uint16_t));
        } else {
            size_t screen_line = line_index - scrollback_count;
            if (screen_line < VGA_HEIGHT) {
                memcpy(dest, &terminal_screen[screen_line * VGA_WIDTH], VGA_WIDTH * sizeof(uint16_t));
            } else {
                for (size_t x = 0; x < VGA_WIDTH; x++) {
                    dest[x] = vga_entry(' ', terminal_color);
                }
            }
        }
    }
}

static void terminal_scroll_line(void) {
    scrollback_push_line(&terminal_screen[0]);
    memmove(terminal_screen,
            terminal_screen + VGA_WIDTH,
            (VGA_HEIGHT - 1) * VGA_WIDTH * sizeof(uint16_t));
    const size_t last_row_index = (VGA_HEIGHT - 1) * VGA_WIDTH;
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        terminal_screen[last_row_index + x] = vga_entry(' ', terminal_color);
    }
    terminal_row = VGA_HEIGHT - 1;
    terminal_column = 0;
    if (display_offset < scrollback_count) {
        display_offset++;
        if (display_offset > scrollback_count) {
            display_offset = scrollback_count;
        }
    }
    terminal_render();
}

void terminal_putchar(char c) {
    unsigned char uc = c;

    if (display_offset > 0) {
        terminal_scroll_to_bottom();
    }

    if (uc == '\b') {
        if (terminal_column > 0) {
            terminal_column--;
            terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
        } else if (terminal_row > 0) {
            terminal_row--;
            terminal_column = VGA_WIDTH - 1;
            terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
        }
        return;
    }

    if (uc == '\r') {
        terminal_column = 0;
        return;
    }

    if (uc == '\n') {
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) {
            terminal_scroll_line();
        }
        return;
    }

    terminal_putentryat(uc, terminal_color, terminal_column, terminal_row);
    
    if (++terminal_column == VGA_WIDTH) {
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) {
            terminal_scroll_line();
        }
    }
}

void terminal_write(const char* data, size_t size) {
	for (size_t i = 0; i < size; i++)
		terminal_putchar(data[i]);
}

void terminal_writestring(const char* data) {
	terminal_write(data, strlen(data));
}

void terminal_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_screen[index] = vga_entry(' ', terminal_color);
        }
    }
    terminal_row = 0;
    terminal_column = 0;
    scrollback_head = 0;
    scrollback_count = 0;
    display_offset = 0;
    terminal_render();
}

void terminal_scroll_view(int delta) {
    if (delta > 0) {
        size_t step = (size_t)delta;
        size_t max = scrollback_count;
        if (display_offset + step > max) {
            display_offset = max;
        } else {
            display_offset += step;
        }
    } else if (delta < 0) {
        size_t step = (size_t)(-delta);
        if (display_offset <= step) {
            display_offset = 0;
        } else {
            display_offset -= step;
        }
    }
    terminal_render();
}

void terminal_scroll_to_bottom(void) {
    display_offset = 0;
    terminal_render();
}

size_t terminal_get_scroll_offset(void) {
    return display_offset;
}
