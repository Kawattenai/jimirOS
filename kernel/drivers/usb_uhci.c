#include <kernel/usb.h>
#include <kernel/pci.h>
#include <kernel/stdio.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <string.h>

/* UHCI (USB 1.1) Host Controller Driver */

#define UHCI_CLASS_CODE     0x0C
#define UHCI_SUBCLASS       0x03
#define UHCI_PROG_IF        0x00  /* UHCI */

/* UHCI Registers (I/O space) */
#define UHCI_USBCMD         0x00  /* USB Command */
#define UHCI_USBSTS         0x02  /* USB Status */
#define UHCI_USBINTR        0x04  /* USB Interrupt Enable */
#define UHCI_FRNUM          0x06  /* Frame Number */
#define UHCI_FRBASEADD      0x08  /* Frame List Base Address */
#define UHCI_SOFMOD         0x0C  /* Start of Frame Modify */
#define UHCI_PORTSC1        0x10  /* Port 1 Status/Control */
#define UHCI_PORTSC2        0x12  /* Port 2 Status/Control */

/* USB Command Register bits */
#define UHCI_CMD_RS         (1 << 0)  /* Run/Stop */
#define UHCI_CMD_HCRESET    (1 << 1)  /* Host Controller Reset */
#define UHCI_CMD_GRESET     (1 << 2)  /* Global Reset */
#define UHCI_CMD_EGSM       (1 << 3)  /* Enter Global Suspend */
#define UHCI_CMD_FGR        (1 << 4)  /* Force Global Resume */
#define UHCI_CMD_SWDBG      (1 << 5)  /* Software Debug */
#define UHCI_CMD_CF         (1 << 6)  /* Configure Flag */
#define UHCI_CMD_MAXP       (1 << 7)  /* Max Packet */

/* USB Status Register bits */
#define UHCI_STS_USBINT     (1 << 0)  /* USB Interrupt */
#define UHCI_STS_ERROR      (1 << 1)  /* USB Error Interrupt */
#define UHCI_STS_RD         (1 << 2)  /* Resume Detect */
#define UHCI_STS_HSE        (1 << 3)  /* Host System Error */
#define UHCI_STS_HCPE       (1 << 4)  /* Host Controller Process Error */
#define UHCI_STS_HCH        (1 << 5)  /* HC Halted */

/* Port Status/Control bits */
#define UHCI_PORT_CCS       (1 << 0)  /* Current Connect Status */
#define UHCI_PORT_CSC       (1 << 1)  /* Connect Status Change */
#define UHCI_PORT_PED       (1 << 2)  /* Port Enable/Disable */
#define UHCI_PORT_PEDC      (1 << 3)  /* Port Enable/Disable Change */
#define UHCI_PORT_LSL       (1 << 4)  /* Line Status - Low */
#define UHCI_PORT_LSH       (1 << 5)  /* Line Status - High */
#define UHCI_PORT_RWC       (1 << 6)  /* Resume */
#define UHCI_PORT_LSDA      (1 << 8)  /* Low Speed Device Attached */
#define UHCI_PORT_PR        (1 << 9)  /* Port Reset */
#define UHCI_PORT_SUSP      (1 << 12) /* Suspend */

#define UHCI_NUM_FRAMES     1024
#define UHCI_MAX_PORTS      8

/* UHCI Transfer Descriptor */
typedef struct uhci_td {
    uint32_t link_ptr;
    uint32_t status;
    uint32_t token;
    uint32_t buffer;
    /* Software use fields */
    uint32_t reserved[4];
} __attribute__((aligned(16))) uhci_td_t;

/* UHCI Queue Head */
typedef struct uhci_qh {
    uint32_t head_ptr;
    uint32_t element_ptr;
    /* Software use fields */
    uint32_t reserved[2];
} __attribute__((aligned(16))) uhci_qh_t;

/* TD Control/Status bits */
#define TD_CTRL_SPD       (1 << 29)  /* Short Packet Detect */
#define TD_CTRL_C_ERR_MASK (3 << 27) /* Error Counter */
#define TD_CTRL_LS        (1 << 26)  /* Low Speed Device */
#define TD_CTRL_IOS       (1 << 25)  /* Isochronous Select */
#define TD_CTRL_IOC       (1 << 24)  /* Interrupt on Complete */
#define TD_CTRL_ACTIVE    (1 << 23)  /* Active */
#define TD_CTRL_STALLED   (1 << 22)  /* Stalled */
#define TD_CTRL_BABBLE    (1 << 20)  /* Babble Detected */
#define TD_CTRL_NAK       (1 << 19)  /* NAK Received */
#define TD_CTRL_CRCTO     (1 << 18)  /* CRC/Timeout Error */
#define TD_CTRL_BITSTUFF  (1 << 17)  /* Bitstuff Error */

/* TD Token bits */
#define TD_TOKEN_PID_MASK   0xFF
#define TD_TOKEN_PID_IN     0x69
#define TD_TOKEN_PID_OUT    0xE1
#define TD_TOKEN_PID_SETUP  0x2D

#define TD_TOKEN_DEVADDR_SHIFT 8
#define TD_TOKEN_ENDPOINT_SHIFT 15
#define TD_TOKEN_MAXLEN_SHIFT 21

static uint16_t g_uhci_iobase = 0;
static uint32_t* g_frame_list = NULL;
static int g_uhci_ready = 0;

/* Per-device state */
#define MAX_USB_DEVICES 8
typedef struct {
    int active;
    int port;
    int address;
    int low_speed;
    uhci_qh_t* interrupt_qh;
    uhci_td_t* interrupt_td;
    uint32_t interrupt_buffer_phys;
    uint8_t* interrupt_buffer;
} usb_device_t;

static usb_device_t g_usb_devices[MAX_USB_DEVICES];
static int g_next_address = 1;

/* I/O port operations */
static inline uint16_t uhci_read16(uint16_t reg) {
    uint16_t val;
    uint16_t port = g_uhci_iobase + reg;
    __asm__ volatile("inw %w1, %w0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void uhci_write16(uint16_t reg, uint16_t val) {
    uint16_t port = g_uhci_iobase + reg;
    __asm__ volatile("outw %w0, %w1" : : "a"(val), "Nd"(port));
}

static inline uint32_t uhci_read32(uint16_t reg) {
    uint32_t val;
    uint16_t port = g_uhci_iobase + reg;
    __asm__ volatile("inl %w1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void uhci_write32(uint16_t reg, uint32_t val) {
    uint16_t port = g_uhci_iobase + reg;
    __asm__ volatile("outl %0, %w1" : : "a"(val), "Nd"(port));
}

static int uhci_reset(void) {
    printf("uhci: resetting controller\n");
    
    /* Stop the controller */
    uhci_write16(UHCI_USBCMD, 0);
    
    /* Wait for halt */
    for (int i = 0; i < 1000; i++) {
        if (uhci_read16(UHCI_USBSTS) & UHCI_STS_HCH) {
            break;
        }
        for (volatile int j = 0; j < 1000; j++) { }
    }
    
    /* Perform HC Reset */
    uhci_write16(UHCI_USBCMD, UHCI_CMD_HCRESET);
    
    /* Wait for reset to complete */
    for (int i = 0; i < 1000; i++) {
        if (!(uhci_read16(UHCI_USBCMD) & UHCI_CMD_HCRESET)) {
            break;
        }
        for (volatile int j = 0; j < 1000; j++) { }
    }
    
    if (uhci_read16(UHCI_USBCMD) & UHCI_CMD_HCRESET) {
        printf("uhci: reset timeout\n");
        return -1;
    }
    
    printf("uhci: reset complete\n");
    return 0;
}

static int uhci_init_controller(struct pci_device* dev) {
    /* Get I/O base address from BAR4 */
    uint32_t bar4 = pci_config_read32(dev->bus, dev->slot, dev->function, 0x20);
    if (!(bar4 & 1)) {
        printf("uhci: BAR4 is not I/O space\n");
        return -1;
    }
    
    g_uhci_iobase = bar4 & 0xFFF0;
    printf("uhci: I/O base = 0x%x\n", g_uhci_iobase);
    
    /* Enable PCI bus mastering and I/O space */
    uint16_t cmd = pci_config_read16(dev->bus, dev->slot, dev->function, 0x04);
    cmd |= (1 << 0) | (1 << 2);  /* I/O Space | Bus Master */
    pci_config_write16(dev->bus, dev->slot, dev->function, 0x04, cmd);
    
    /* Reset the controller */
    if (uhci_reset() != 0) {
        return -1;
    }
    
    /* Allocate frame list (1024 entries * 4 bytes, 4KB aligned) */
    uint32_t frame_list_phys = pmm_alloc_frame_below(0x01000000);
    if (!frame_list_phys) {
        printf("uhci: failed to allocate frame list\n");
        return -1;
    }
    
    g_frame_list = (uint32_t*)(uintptr_t)frame_list_phys;
    memset(g_frame_list, 0, 4096);
    
    /* Mark all frames as invalid initially */
    for (int i = 0; i < UHCI_NUM_FRAMES; i++) {
        g_frame_list[i] = 1;  /* T-bit set = invalid */
    }
    
    /* Set frame list base address */
    uhci_write32(UHCI_FRBASEADD, frame_list_phys);
    
    /* Set frame number to 0 */
    uhci_write16(UHCI_FRNUM, 0);
    
    /* Clear status register */
    uhci_write16(UHCI_USBSTS, 0xFFFF);
    
    /* Enable interrupts */
    uhci_write16(UHCI_USBINTR, 0x0F);  /* All interrupts */
    
    /* Start the controller */
    uhci_write16(UHCI_USBCMD, UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP);
    
    printf("uhci: controller started\n");
    
    return 0;
}

/* Helper: Build a TD for interrupt IN transfer */
static uhci_td_t* uhci_build_interrupt_td(uint8_t devaddr, uint8_t endpoint, 
                                          uint32_t buffer_phys, uint16_t maxlen,
                                          int low_speed) {
    uint32_t td_phys = pmm_alloc_frame_below(0x01000000);
    if (!td_phys) return NULL;
    
    uhci_td_t* td = (uhci_td_t*)(uintptr_t)td_phys;
    memset(td, 0, sizeof(uhci_td_t));
    
    /* Link pointer: invalid (T-bit set) for single TD */
    td->link_ptr = 1;
    
    /* Status: Active, low-speed if needed, IOC, 3 error retries */
    td->status = TD_CTRL_ACTIVE | TD_CTRL_IOC | (3 << 27);
    if (low_speed) {
        td->status |= TD_CTRL_LS;
    }
    
    /* Token: IN PID, device address, endpoint, max length */
    td->token = TD_TOKEN_PID_IN |
                (devaddr << TD_TOKEN_DEVADDR_SHIFT) |
                (endpoint << TD_TOKEN_ENDPOINT_SHIFT) |
                (((maxlen - 1) & 0x7FF) << TD_TOKEN_MAXLEN_SHIFT);
    
    /* Buffer pointer */
    td->buffer = buffer_phys;
    
    return td;
}

/* Helper: Setup interrupt transfer for keyboard */
static int uhci_setup_keyboard_interrupt(usb_device_t* dev) {
    printf("uhci: setting up interrupt transfer for device at address %d\n", dev->address);
    
    /* Allocate buffer for keyboard reports (8 bytes) */
    dev->interrupt_buffer_phys = pmm_alloc_frame_below(0x01000000);
    if (!dev->interrupt_buffer_phys) {
        printf("uhci: failed to allocate interrupt buffer\n");
        return -1;
    }
    dev->interrupt_buffer = (uint8_t*)(uintptr_t)dev->interrupt_buffer_phys;
    memset(dev->interrupt_buffer, 0, 8);
    
    /* Allocate Queue Head */
    uint32_t qh_phys = pmm_alloc_frame_below(0x01000000);
    if (!qh_phys) {
        printf("uhci: failed to allocate QH\n");
        return -1;
    }
    dev->interrupt_qh = (uhci_qh_t*)(uintptr_t)qh_phys;
    memset(dev->interrupt_qh, 0, sizeof(uhci_qh_t));
    
    /* Build interrupt IN TD for endpoint 1 (keyboard interrupt endpoint) */
    dev->interrupt_td = uhci_build_interrupt_td(dev->address, 1,  /* endpoint 1 */
                                                dev->interrupt_buffer_phys, 8,
                                                dev->low_speed);
    if (!dev->interrupt_td) {
        printf("uhci: failed to build interrupt TD\n");
        return -1;
    }
    
    /* Setup QH */
    dev->interrupt_qh->head_ptr = 1;  /* T-bit: terminate */
    dev->interrupt_qh->element_ptr = (uint32_t)(uintptr_t)dev->interrupt_td;
    
    /* Add QH to frame list (every 8ms = every 8 frames) */
    uint32_t qh_phys_link = qh_phys | 0x2;  /* QH pointer, bit 1 set */
    for (int i = 0; i < UHCI_NUM_FRAMES; i += 8) {
        g_frame_list[i] = qh_phys_link;
    }
    
    printf("uhci: interrupt transfer configured (polling every 8ms)\n");
    return 0;
}

static void uhci_check_ports(void) {
    printf("uhci: checking ports\n");
    
    for (int port = 0; port < 2; port++) {  /* UHCI typically has 2 ports */
        uint16_t reg = UHCI_PORTSC1 + (port * 2);
        uint16_t status = uhci_read16(reg);
        
        printf("uhci: port %d status = 0x%x\n", port, status);
        
        if (status & UHCI_PORT_CCS) {
            printf("uhci: port %d: device connected\n", port);
            
            /* Check if low-speed device */
            int low_speed = (status & UHCI_PORT_LSDA) ? 1 : 0;
            printf("uhci: port %d: %s speed\n", port, low_speed ? "low" : "full");
            
            /* Reset the port */
            printf("uhci: port %d: resetting\n", port);
            uhci_write16(reg, status | UHCI_PORT_PR);
            
            /* Wait 50ms for reset */
            for (volatile int i = 0; i < 50000; i++) { }
            
            /* Clear reset */
            uhci_write16(reg, status & ~UHCI_PORT_PR);
            
            /* Wait for port to be enabled */
            for (int i = 0; i < 100; i++) {
                status = uhci_read16(reg);
                if (status & UHCI_PORT_PED) {
                    printf("uhci: port %d: enabled\n", port);
                    break;
                }
                for (volatile int j = 0; j < 1000; j++) { }
            }
            
            /* Allocate device structure */
            usb_device_t* dev = NULL;
            for (int i = 0; i < MAX_USB_DEVICES; i++) {
                if (!g_usb_devices[i].active) {
                    dev = &g_usb_devices[i];
                    break;
                }
            }
            
            if (!dev) {
                printf("uhci: no free device slots\n");
                continue;
            }
            
            memset(dev, 0, sizeof(usb_device_t));
            dev->active = 1;
            dev->port = port;
            dev->address = g_next_address++;
            dev->low_speed = low_speed;
            
            /* For boot keyboards, we skip full enumeration and assume:
             * - Device responds to address 0
             * - Endpoint 1 is interrupt IN for boot protocol reports
             * - 8-byte reports every 10ms
             * 
             * In production, you would:
             * 1. Send SET_ADDRESS to assign address
             * 2. Get device descriptor
             * 3. Get configuration/interface/endpoint descriptors  
             * 4. Set configuration
             * 5. Send SET_PROTOCOL to enable boot protocol
             */
            
            /* For now, use address 0 (default) since we skip SET_ADDRESS */
            dev->address = 0;
            
            /* Setup interrupt transfer */
            if (uhci_setup_keyboard_interrupt(dev) != 0) {
                printf("uhci: failed to setup keyboard interrupt\n");
                dev->active = 0;
                continue;
            }
            
            /* Notify keyboard driver */
            extern void usb_keyboard_device_attached(int port, int low_speed);
            usb_keyboard_device_attached(port, low_speed);
        }
    }
}

int usb_init(void) {
    printf("usb: initializing UHCI driver\n");
    
    /* Debug: List all PCI devices to find USB controller */
    printf("usb: scanning PCI for USB controllers (class 0x0C subclass 0x03)...\n");
    int usb_found = 0;
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint16_t vendor = pci_config_read16(bus, slot, 0, 0x00);
            if (vendor == 0xFFFF) continue;
            
            uint8_t class = pci_config_read8(bus, slot, 0, 0x0B);
            uint8_t subclass = pci_config_read8(bus, slot, 0, 0x0A);
            uint8_t prog_if = pci_config_read8(bus, slot, 0, 0x09);
            uint16_t device = pci_config_read16(bus, slot, 0, 0x02);
            
            /* Print all devices for debugging */
            if (bus < 2) {  /* Only print bus 0 and 1 to avoid spam */
                printf("pci: %u:%u.0 vendor=0x%x device=0x%x class=0x%x:0x%x:0x%x\n",
                       bus, slot, vendor, device, class, subclass, prog_if);
            }
            
            if (class == 0x0C && subclass == 0x03) {
                printf("usb: found USB controller at %u:%u.0 vendor=0x%x device=0x%x prog_if=0x%x\n",
                       bus, slot, vendor, device, prog_if);
                usb_found = 1;
            }
        }
    }
    
    if (!usb_found) {
        printf("usb: no USB controllers found in PCI scan\n");
    }
    
    struct pci_device dev;
    /* Try UHCI (prog_if = 0x00) */
    if (pci_find_class(UHCI_CLASS_CODE, UHCI_SUBCLASS, UHCI_PROG_IF, &dev) != 0) {
        /* Try with wildcard prog_if - some controllers don't report correctly */
        if (pci_find_class(UHCI_CLASS_CODE, UHCI_SUBCLASS, 0xFF, &dev) != 0) {
            printf("usb: no UHCI controller found\n");
            return -1;
        }
        printf("usb: found USB controller with wildcard match (prog_if=0x%02x)\n",
               pci_config_read8(dev.bus, dev.slot, dev.function, 0x09));
    }
    
    printf("usb: found UHCI controller 0x%x:0x%x at bus=%u slot=%u func=%u\n",
           dev.vendor_id, dev.device_id, dev.bus, dev.slot, dev.function);
    
    if (uhci_init_controller(&dev) != 0) {
        return -1;
    }
    
    /* Check for connected devices */
    uhci_check_ports();
    
    g_uhci_ready = 1;
    return 0;
}

void usb_poll(void) {
    if (!g_uhci_ready) return;
    
    /* Check USB status */
    uint16_t status = uhci_read16(UHCI_USBSTS);
    if (status & UHCI_STS_USBINT) {
        /* Clear interrupt */
        uhci_write16(UHCI_USBSTS, UHCI_STS_USBINT);
    }
    
    /* Poll all active devices */
    for (int i = 0; i < MAX_USB_DEVICES; i++) {
        usb_device_t* dev = &g_usb_devices[i];
        if (!dev->active || !dev->interrupt_td) continue;
        
        /* Check if TD completed */
        if (!(dev->interrupt_td->status & TD_CTRL_ACTIVE)) {
            /* Check for errors */
            if (dev->interrupt_td->status & (TD_CTRL_STALLED | TD_CTRL_BABBLE | 
                                            TD_CTRL_CRCTO | TD_CTRL_BITSTUFF)) {
                /* Error occurred - re-activate TD */
                dev->interrupt_td->status = TD_CTRL_ACTIVE | TD_CTRL_IOC | (3 << 27);
                if (dev->low_speed) {
                    dev->interrupt_td->status |= TD_CTRL_LS;
                }
                continue;
            }
            
            /* Transfer completed successfully */
            /* Process keyboard report */
            extern void usb_keyboard_process_report(const uint8_t* data, int len);
            usb_keyboard_process_report(dev->interrupt_buffer, 8);
            
            /* Re-activate TD for next transfer */
            dev->interrupt_td->status = TD_CTRL_ACTIVE | TD_CTRL_IOC | (3 << 27);
            if (dev->low_speed) {
                dev->interrupt_td->status |= TD_CTRL_LS;
            }
            
            /* Toggle data toggle (should be done properly, but for boot protocol it often works without) */
        }
    }
}
