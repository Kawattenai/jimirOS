#include <kernel/pci.h>
#include <kernel/ports.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static inline uint32_t make_address(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    return (uint32_t)(0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                      ((uint32_t)function << 8) | (offset & 0xFC));
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, make_address(bus, slot, function, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t value = pci_config_read32(bus, slot, function, offset & 0xFC);
    return (uint16_t)((value >> ((offset & 2) * 8)) & 0xFFFFu);
}

uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t value = pci_config_read32(bus, slot, function, offset & 0xFC);
    return (uint8_t)((value >> ((offset & 3) * 8)) & 0xFFu);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, make_address(bus, slot, function, offset));
    outl(PCI_CONFIG_DATA, value);
}

void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value) {
    uint32_t reg = pci_config_read32(bus, slot, function, offset & 0xFC);
    uint32_t shift = (offset & 2) * 8;
    reg &= ~(0xFFFFu << shift);
    reg |= ((uint32_t)value << shift);
    pci_config_write32(bus, slot, function, offset & 0xFC, reg);
}

int pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, struct pci_device* out) {
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            uint32_t vendor_device = pci_config_read32(bus, slot, 0, 0x00);
            if ((vendor_device & 0xFFFFu) == 0xFFFFu) continue;
            uint8_t header_type = pci_config_read8(bus, slot, 0, 0x0E);
            uint8_t functions = (header_type & 0x80) ? 8 : 1;
            for (uint8_t function = 0; function < functions; ++function) {
                uint32_t vd = pci_config_read32(bus, slot, function, 0x00);
                if ((vd & 0xFFFFu) == 0xFFFFu) continue;
                uint32_t class_reg = pci_config_read32(bus, slot, function, 0x08);
                uint8_t cc = (uint8_t)(class_reg >> 24);
                uint8_t sc = (uint8_t)(class_reg >> 16);
                uint8_t pi = (uint8_t)(class_reg >> 8);
                if (cc == class_code && sc == subclass && (prog_if == 0xFFu || pi == prog_if)) {
                    if (out) {
                        out->bus = (uint8_t)bus;
                        out->slot = slot;
                        out->function = function;
                        out->vendor_id = (uint16_t)(vd & 0xFFFFu);
                        out->device_id = (uint16_t)(vd >> 16);
                        out->class_code = cc;
                        out->subclass = sc;
                        out->prog_if = pi;
                    }
                    return 0;
                }
            }
        }
    }
    return -1;
}
