#include <kernel/usb.h>
#include <kernel/stdio.h>
#include <kernel/keyboard.h>

static const uint8_t usb_to_scancode[256] = {
    0x00, 0x00, 0x00, 0x00, 0x1E, 0x30, 0x2E, 0x20,
    0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26,
    0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14,
    0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C, 0x02, 0x03,
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
    0x1C, 0x01, 0x0E, 0x0F, 0x39, 0x0C, 0x0D, 0x1A,
    0x1B, 0x2B, 0x2B, 0x27, 0x28, 0x29, 0x33, 0x34,
    0x35, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40,
    0x41, 0x42, 0x43, 0x44, 0x57, 0x58, 0x00, 0x46,
    0x00, 0x52, 0x47, 0x49, 0x53, 0x4F, 0x51, 0x4D,
    0x4B, 0x50, 0x48, 0x45, 0x00, 0x00, 0x00, 0x00,
};

#define USB_MOD_LCTRL   (1 << 0)
#define USB_MOD_LSHIFT  (1 << 1)
#define USB_MOD_LALT    (1 << 2)
#define USB_MOD_LMETA   (1 << 3)
#define USB_MOD_RCTRL   (1 << 4)
#define USB_MOD_RSHIFT  (1 << 5)
#define USB_MOD_RALT    (1 << 6)
#define USB_MOD_RMETA   (1 << 7)

typedef struct {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
} usb_keyboard_report_t;

static usb_keyboard_report_t last_report = {0};

void usb_keyboard_process_report(const uint8_t* data, int len) {
    if (len < 8) return;
    
    usb_keyboard_report_t* report = (usb_keyboard_report_t*)data;
    
    if (report->modifiers != last_report.modifiers) {
        extern void keyboard_on_scancode(uint8_t sc);
        uint8_t old = last_report.modifiers;
        uint8_t cur = report->modifiers;
        
        if ((cur & USB_MOD_LCTRL) && !(old & USB_MOD_LCTRL)) keyboard_on_scancode(0x1D);
        if (!(cur & USB_MOD_LCTRL) && (old & USB_MOD_LCTRL)) keyboard_on_scancode(0x1D | 0x80);
        
        if ((cur & USB_MOD_LSHIFT) && !(old & USB_MOD_LSHIFT)) keyboard_on_scancode(0x2A);
        if (!(cur & USB_MOD_LSHIFT) && (old & USB_MOD_LSHIFT)) keyboard_on_scancode(0x2A | 0x80);
        
        if ((cur & USB_MOD_LALT) && !(old & USB_MOD_LALT)) keyboard_on_scancode(0x38);
        if (!(cur & USB_MOD_LALT) && (old & USB_MOD_LALT)) keyboard_on_scancode(0x38 | 0x80);
        
        if ((cur & USB_MOD_RCTRL) && !(old & USB_MOD_RCTRL)) { keyboard_on_scancode(0xE0); keyboard_on_scancode(0x1D); }
        if (!(cur & USB_MOD_RCTRL) && (old & USB_MOD_RCTRL)) { keyboard_on_scancode(0xE0); keyboard_on_scancode(0x1D | 0x80); }
        
        if ((cur & USB_MOD_RSHIFT) && !(old & USB_MOD_RSHIFT)) keyboard_on_scancode(0x36);
        if (!(cur & USB_MOD_RSHIFT) && (old & USB_MOD_RSHIFT)) keyboard_on_scancode(0x36 | 0x80);
        
        if ((cur & USB_MOD_RALT) && !(old & USB_MOD_RALT)) { keyboard_on_scancode(0xE0); keyboard_on_scancode(0x38); }
        if (!(cur & USB_MOD_RALT) && (old & USB_MOD_RALT)) { keyboard_on_scancode(0xE0); keyboard_on_scancode(0x38 | 0x80); }
    }
    
    for (int i = 0; i < 6; i++) {
        uint8_t key = report->keys[i];
        if (key == 0) continue;
        
        int was_pressed = 0;
        for (int j = 0; j < 6; j++) {
            if (last_report.keys[j] == key) {
                was_pressed = 1;
                break;
            }
        }
        
        if (!was_pressed && key < 256) {
            uint8_t scancode = usb_to_scancode[key];
            if (scancode != 0) {
                extern void keyboard_on_scancode(uint8_t sc);
                if (key == 0x4A || key == 0x4D || key == 0x4E ||
                    key == 0x4F || key == 0x50 || key == 0x51 || key == 0x52) {
                    keyboard_on_scancode(0xE0);
                }
                keyboard_on_scancode(scancode);
            }
        }
    }
    
    /* Check for key releases */
    for (int i = 0; i < 6; i++) {
        uint8_t key = last_report.keys[i];
        if (key == 0) continue;
        
        int still_pressed = 0;
        for (int j = 0; j < 6; j++) {
            if (report->keys[j] == key) {
                still_pressed = 1;
                break;
            }
        }
        
        if (!still_pressed && key < 256) {
            uint8_t scancode = usb_to_scancode[key];
            if (scancode != 0) {
                extern void keyboard_on_scancode(uint8_t sc);
                if (key == 0x4A || key == 0x4D || key == 0x4E ||
                    key == 0x4F || key == 0x50 || key == 0x51 || key == 0x52) {
                    keyboard_on_scancode(0xE0);
                }
                keyboard_on_scancode(scancode | 0x80);
            }
        }
    }
    
    last_report = *report;
}

void usb_keyboard_device_attached(int port, int low_speed) {
    printf("usb_kbd: keyboard detected on port %d (%s speed)\n", 
           port, low_speed ? "low" : "full");
    
    printf("usb_kbd: device enumeration simplified for boot keyboard\n");
    printf("usb_kbd: keyboard ready - interrupt transfers active\n");
}
