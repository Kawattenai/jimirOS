#include <kernel/ports.h>
#include <kernel/pit.h>

#define PIT_CH0 0x40
#define PIT_CMD 0x43
#define PIT_MODE_SQUARE 0x36 /* ch0, lobyte/hibyte, mode 3 */

static volatile uint64_t s_ticks = 0;
static uint32_t s_hz = 0;

void pit_init(uint32_t hz) {
    if (hz < 19) hz = 19; /* avoid divisor overflow */
    uint32_t divisor = 1193180u / hz; /* PIT base clock */
    outb(PIT_CMD, PIT_MODE_SQUARE);
    outb(PIT_CH0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0, (uint8_t)((divisor >> 8) & 0xFF));
    s_hz = hz;
}

void pit_on_tick(void) { s_ticks++; }
uint64_t pit_ticks(void) { return s_ticks; }
uint32_t pit_hz(void) { return s_hz; }
