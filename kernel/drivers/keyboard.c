#include <kernel/keyboard.h>
#include <kernel/stdio.h>
#include <kernel/ports.h>
#include <kernel/serial.h>

#define KBD_BUF_SIZE 128
static volatile uint16_t buf[KBD_BUF_SIZE];
static volatile uint8_t head = 0, tail = 0;
static volatile int shift = 0;
static volatile int ctrl = 0;
static volatile int alt = 0;
static volatile int e0 = 0;
static volatile int scroll_override_up = 0;
static volatile int scroll_override_down = 0;

static const char keymap[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',  0,'\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0,' ',
};

static const char keymap_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
    'A','S','D','F','G','H','J','K','L',':','\"','~', 0,'|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0,' ',
};

static inline int buf_empty(void){ return head==tail; }
static inline int buf_full(void){ return (uint8_t)(head+1)==tail; }

void keyboard_init(void) {
    head = tail = 0; shift = ctrl = alt = 0; e0 = 0;
    
    while (inb(0x64) & 0x02);
    
    outb(0x64, 0x60);
    while (inb(0x64) & 0x02);
    outb(0x60, 0x45);
}

void keyboard_on_scancode(uint8_t sc) {
    if (sc == 0xE0) { 
        e0 = 1;
        return;
    }
    
    int release = (sc & 0x80) ? 1 : 0; 
    sc &= 0x7F;
    
    if (e0) {
        e0 = 0;
        switch (sc) {
            case 0x1D: ctrl = !release; return;
            case 0x38: alt = !release; return;
        }
        if (release) {
            if (sc == 0x48) scroll_override_up = 0;
            if (sc == 0x50) scroll_override_down = 0;
            return;
        }
        uint16_t code = 0;
        switch (sc) {
            case 0x48: {
                int use_scroll = scroll_override_up || ctrl || alt;
                if (ctrl || alt) scroll_override_up = 1;
                code = use_scroll ? KEY_SCROLL_UP : KEY_UP;
                break;
            }
            case 0x50: {
                int use_scroll = scroll_override_down || ctrl || alt;
                if (ctrl || alt) scroll_override_down = 1;
                code = use_scroll ? KEY_SCROLL_DOWN : KEY_DOWN;
                break;
            }
            case 0x4B: code = KEY_LEFT; break;
            case 0x4D: code = KEY_RIGHT; break;
            case 0x47: code = KEY_HOME; break;
            case 0x4F: code = KEY_END; break;
            case 0x53: code = KEY_DELETE; break;
            case 0x49: code = KEY_PAGE_UP; break;
            case 0x51: code = KEY_PAGE_DOWN; break;
            default: break;
        }
        if (code && !buf_full()) { buf[head] = code; head = (uint8_t)(head+1); }
        return;
    }
    
    switch (sc) {
        case 42: case 54: shift = !release; return;
        case 29: ctrl = !release; return;
        case 56: alt = !release; return;
    }

    if (release) return;

    char ch = 0;
    if (sc < 128) {
        ch = shift ? keymap_shift[sc] : keymap[sc];
    }
    if (!ch) return;
    if (!buf_full()) { buf[head] = (uint16_t)(uint8_t)ch; head = (uint8_t)(head+1); }
}

int kbd_getch(void) {
    if (buf_empty()) return -1;
    uint16_t v = buf[tail]; tail = (uint8_t)(tail+1);
    return (int)v;
}
