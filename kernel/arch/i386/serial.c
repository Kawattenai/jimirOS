#include <kernel/serial.h>
#include <kernel/ports.h>

static int serial_is_transmit_empty(void) {
    return inb(COM1_PORT + 5) & 0x20; // LSR bit 5: THR empty
}

static int serial_has_data(void) {
    return inb(COM1_PORT + 5) & 0x01; // LSR bit 0: data ready
}

void serial_init(void) {
    // Disable interrupts
    outb(COM1_PORT + 1, 0x00);
    // Enable DLAB
    outb(COM1_PORT + 3, 0x80);
    // Set baud rate divisor for 115200/baud. For 38400: divisor=3.
    outb(COM1_PORT + 0, 0x03); // low byte (3)
    outb(COM1_PORT + 1, 0x00); // high byte
    // 8 bits, no parity, one stop, disable DLAB
    outb(COM1_PORT + 3, 0x03);
    // Enable FIFO, clear them, 14-byte threshold
    outb(COM1_PORT + 2, 0xC7);
    // IRQs disabled, RTS/DSR set
    outb(COM1_PORT + 4, 0x0B);
}

void serial_putchar(char c) {
    if (c == '\n') {
        // Convert LF to CRLF for terminals
        while (!serial_is_transmit_empty()) {}
        outb(COM1_PORT, '\r');
    }
    while (!serial_is_transmit_empty()) {}
    outb(COM1_PORT, (uint8_t)c);
}

void serial_writestring(const char* s) {
    if (!s) return;
    while (*s) {
        serial_putchar(*s++);
    }
}

int serial_available(void) {
    return serial_has_data();
}

int serial_getchar(void) {
    if (!serial_has_data()) {
        return -1;
    }
    return (int)inb(COM1_PORT);
}
