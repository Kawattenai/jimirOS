#include <kernel/ahci.h>
#include <kernel/pci.h>
#include <kernel/stdio.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <string.h>
#include <stdint.h>

#define AHCI_CLASS_CODE 0x01
#define AHCI_SUBCLASS   0x06

#define HBA_PORT_DEV_PRESENT 0x3
#define HBA_PORT_IPM_ACTIVE  0x1

#define HBA_GHC_HR   (1u << 0)
#define HBA_GHC_AE   (1u << 31)

#define HBA_PxCMD_ST   (1u << 0)
#define HBA_PxCMD_SUD  (1u << 1)
#define HBA_PxCMD_POD  (1u << 2)
#define HBA_PxCMD_FRE  (1u << 4)
#define HBA_PxCMD_FR   (1u << 14)
#define HBA_PxCMD_CR   (1u << 15)

#define HBA_PxIS_TFES  (1u << 30)

#define FIS_TYPE_REG_H2D 0x27
#define ATA_CMD_READ_DMA_EXT  0x25
#define ATA_CMD_WRITE_DMA_EXT 0x35

#define AHCI_MAX_PORTS 32
#define AHCI_MAX_CMD_SLOTS 32
#define AHCI_DMA_SECTORS 8
#define SECTOR_SIZE 512

#define AHCI_VIRT_BASE 0xFEC00000u
#define AHCI_VIRT_SIZE (0x1000u * 4u)

/* Simple spinlock for serializing AHCI operations */
typedef struct {
    volatile int locked;
} spinlock_t;

static inline void spinlock_init(spinlock_t* lock) {
    lock->locked = 0;
}

static inline void spinlock_acquire(spinlock_t* lock) {
    while (__sync_lock_test_and_set(&lock->locked, 1)) {
        while (lock->locked) { /* spin */ }
    }
}

static inline void spinlock_release(spinlock_t* lock) {
    __sync_lock_release(&lock->locked);
}

typedef volatile struct {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t rsv0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t rsv1[11];
    uint32_t vendor[4];
} hba_port_t;

typedef volatile struct {
    /* 0x00 - Generic Host Control */
    uint32_t cap;       /* 0x00: Host Capabilities */
    uint32_t ghc;       /* 0x04: Global Host Control */
    uint32_t is;        /* 0x08: Interrupt Status */
    uint32_t pi;        /* 0x0C: Ports Implemented */
    uint32_t vs;        /* 0x10: Version */
    uint32_t ccc_ctl;   /* 0x14: Command Completion Coalescing Control */
    uint32_t ccc_pts;   /* 0x18: Command Completion Coalescing Ports */
    uint32_t em_loc;    /* 0x1C: Enclosure Management Location */
    uint32_t em_ctl;    /* 0x20: Enclosure Management Control */
    uint32_t cap2;      /* 0x24: Host Capabilities Extended */
    uint32_t bohc;      /* 0x28: BIOS/OS Handoff Control and Status */
    
    /* 0x2C - Reserved */
    uint32_t rsv[13];
    
    /* 0x60 - Vendor Specific (16 bytes) */
    uint32_t vendor[4];
    
    /* 0x70 - Reserved (144 bytes to reach 0x100) */
    uint32_t rsv2[36];
    
    /* 0x100 - Port Control Registers (256 bytes each, 32 ports max = 8KB) */
    hba_port_t ports[AHCI_MAX_PORTS];
} hba_mem_t;

typedef struct {
    uint8_t  cfl:5;
    uint8_t  a:1;
    uint8_t  w:1;
    uint8_t  p:1;
    uint8_t  rsv0;
    uint16_t prdtl;
    volatile uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv1[4];
} hba_cmd_header_t;

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv0;
    uint32_t dbc;
} hba_prdt_entry_t;

typedef struct {
    uint8_t  cfis[64];
    uint8_t  acmd[16];
    uint8_t  rsv[48];
    hba_prdt_entry_t prdt[1];
} hba_cmd_tbl_t;

static volatile hba_mem_t* g_hba = 0;
static hba_port_t* g_active_port = 0;
static uint8_t g_port_index = 0xFF;
static uint32_t g_cmd_header_phys = 0;
static uint32_t g_fis_phys = 0;
static uint32_t g_cmd_table_phys[AHCI_MAX_CMD_SLOTS];
static uint32_t g_dma_buf_phys = 0;
static uint8_t* g_dma_buf = 0;
static int g_ahci_ready = 0;
static spinlock_t g_ahci_lock;

/* Helper: Convert physical address to virtual (assumes identity mapping in low 16MB) */
static inline void* phys_to_virt(uint32_t phys) {
    /* Our kernel identity-maps 0-16MB, so physical addresses in that range
       can be used directly as virtual addresses */
    return (void*)(uintptr_t)phys;
}

static uint32_t map_abar(uint32_t phys) {
    uint32_t base = phys & ~0xFFFu;
    uint32_t offset = phys & 0xFFFu;
    uint32_t virt = AHCI_VIRT_BASE;
    for (uint32_t i = 0; i < AHCI_VIRT_SIZE; i += 0x1000u) {
        if (vmm_map(virt + i, base + i, PAGE_WRITE) != 0) {
            return 0;
        }
    }
    return virt + offset;
}

static void stop_cmd(hba_port_t* port) {
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;
    for (int spin = 0; spin < 1000000; ++spin) {
        if (!(port->cmd & (HBA_PxCMD_FR | HBA_PxCMD_CR))) return;
    }
    printf("ahci: timeout stopping command engine\n");
}

static void start_cmd(hba_port_t* port) {
    for (int spin = 0; spin < 1000000; ++spin) {
        if (!(port->cmd & HBA_PxCMD_CR)) break;
    }
    port->cmd |= HBA_PxCMD_POD;
    port->cmd |= HBA_PxCMD_SUD;
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
}

static void port_comreset(hba_port_t* port) {
    stop_cmd(port);
    
    /* Clear any pending interrupts and errors */
    port->serr = 0xFFFFFFFFu;
    port->is = 0xFFFFFFFFu;
    
    /* Power on and spin up device */
    port->cmd |= HBA_PxCMD_POD;
    port->cmd |= HBA_PxCMD_SUD;
    
    /* Start FIS receive to capture the D2H Register FIS with signature */
    port->cmd |= HBA_PxCMD_FRE;
    
    /* Wait for device to establish link and send signature */
    for (int spin = 0; spin < 100000; ++spin) {
        uint32_t ssts = port->ssts;
        uint8_t det = (uint8_t)(ssts & 0x0F);
        uint8_t ipm = (uint8_t)((ssts >> 8) & 0x0F);
        
        /* Check if link is up and device is active */
        if (det == HBA_PORT_DEV_PRESENT && ipm == HBA_PORT_IPM_ACTIVE) {
            /* Link is up, now wait a bit for signature to be received */
            for (volatile int i = 0; i < 10000; ++i) { }
            break;
        }
    }
}

static int check_drive_type(hba_port_t* port) {
    /* Wait for device to send D2H Register FIS with valid signature after reset */
    for (int spin = 0; spin < 100000; ++spin) {
        uint32_t sig = port->sig;
        if (sig != 0 && sig != 0xFFFFFFFF) {
            /* Got a valid signature */
            printf("ahci: device signature: 0x%x\n", sig);
            if (sig == 0x00000101) {
                printf("ahci: SATA disk detected\n");
                return 1;
            }
            if (sig == 0xEB140101) {
                printf("ahci: SATAPI device detected\n");
                return 1;
            }
            printf("ahci: unknown device type (sig=0x%x)\n", sig);
            return 0;
        }
    }
    
    /* Signature never became valid - check if link is at least up */
    uint32_t ssts = port->ssts;
    uint8_t det = (uint8_t)(ssts & 0x0F);
    uint8_t ipm = (uint8_t)((ssts >> 8) & 0x0F);
    
    printf("ahci: check_drive det=%u ipm=%u sig=0x%x (no valid sig)\n", det, ipm, port->sig);
    
    /* Accept if link is up as a fallback (QEMU workaround) */
    if (det == HBA_PORT_DEV_PRESENT && ipm == HBA_PORT_IPM_ACTIVE) {
        printf("ahci: accepting device despite invalid signature (link is up)\n");
        return 1;
    }
    
    return 0;
}

static uint32_t alloc_frame_low(void) {
    uint32_t phys = pmm_alloc_frame_below(0x01000000u);
    if (!phys) return 0;
    memset(phys_to_virt(phys), 0, 4096);
    return phys;
}

static int init_port_resources(hba_port_t* port) {
    g_cmd_header_phys = alloc_frame_low();
    g_fis_phys = alloc_frame_low();
    if (!g_cmd_header_phys || !g_fis_phys) return -1;
    for (int i = 0; i < AHCI_MAX_CMD_SLOTS; ++i) {
        g_cmd_table_phys[i] = alloc_frame_low();
        if (!g_cmd_table_phys[i]) return -1;
    }
    memset(phys_to_virt(g_cmd_header_phys), 0, 4096);
    memset(phys_to_virt(g_fis_phys), 0, 4096);
    for (int i = 0; i < AHCI_MAX_CMD_SLOTS; ++i) {
        memset(phys_to_virt(g_cmd_table_phys[i]), 0, 4096);
    }

    stop_cmd(port);
    port->clb = g_cmd_header_phys;
    port->clbu = 0;
    port->fb = g_fis_phys;
    port->fbu = 0;
    port->serr = 0xFFFFFFFFu;
    port->is = 0xFFFFFFFFu;
    port->ie = 0;
    hba_cmd_header_t* hdr = (hba_cmd_header_t*)phys_to_virt(g_cmd_header_phys);
    for (int i = 0; i < AHCI_MAX_CMD_SLOTS; ++i) {
        hdr[i].prdtl = 1;
        hdr[i].ctba = g_cmd_table_phys[i];
        hdr[i].ctbau = 0;
    }
    start_cmd(port);
    return 0;
}

static int find_cmdslot(hba_port_t* port) {
    uint32_t slots = ((((hba_mem_t*)g_hba)->cap >> 8) & 0x1Fu) + 1u;
    for (uint32_t slot = 0; slot < slots; ++slot) {
        if (!((port->sact | port->ci) & (1u << slot)))
            return (int)slot;
    }
    return -1;
}

static int issue_cmd(hba_port_t* port, uint32_t lba, uint8_t count, int write, void* buf) {
    if (!count) return 0;
    int slot = find_cmdslot(port);
    if (slot < 0) return -1;

    hba_cmd_header_t* hdr = &((hba_cmd_header_t*)phys_to_virt(g_cmd_header_phys))[slot];
    hdr->cfl = 5;
    hdr->w = write ? 1 : 0;
    hdr->p = 0;
    hdr->prdtl = 1;
    hdr->prdbc = 0;

    hba_cmd_tbl_t* tbl = (hba_cmd_tbl_t*)phys_to_virt(g_cmd_table_phys[slot]);
    memset(tbl, 0, sizeof(hba_cmd_tbl_t));
    uint8_t sectors = count;
    uint32_t bytes = sectors * SECTOR_SIZE;

    if (write) {
        memcpy(g_dma_buf, buf, bytes);
    }

    tbl->prdt[0].dba = g_dma_buf_phys;
    tbl->prdt[0].dbau = 0;
    tbl->prdt[0].dbc = (bytes - 1);

    memset(tbl->cfis, 0, sizeof(tbl->cfis));
    tbl->cfis[0] = FIS_TYPE_REG_H2D;
    tbl->cfis[1] = 1 << 7;  /* Command bit set */
    tbl->cfis[2] = write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;
    tbl->cfis[3] = 0;  /* Features (7:0) */
    
    /* LBA 48-bit addressing */
    tbl->cfis[4] = (uint8_t)(lba);         /* LBA (7:0) */
    tbl->cfis[5] = (uint8_t)(lba >> 8);    /* LBA (15:8) */
    tbl->cfis[6] = (uint8_t)(lba >> 16);   /* LBA (23:16) */
    tbl->cfis[7] = (1 << 6);  /* Device: LBA mode (bit 6 set) */
    
    tbl->cfis[8] = (uint8_t)(lba >> 24);   /* LBA (31:24) */
    tbl->cfis[9] = 0;   /* LBA (39:32) - we only use 32-bit LBA */
    tbl->cfis[10] = 0;  /* LBA (47:40) */
    tbl->cfis[11] = 0;  /* Features (15:8) */
    
    tbl->cfis[12] = sectors & 0xFF;        /* Count (7:0) */
    tbl->cfis[13] = (sectors >> 8) & 0xFF; /* Count (15:8) */
    tbl->cfis[14] = 0;  /* ICC */
    tbl->cfis[15] = 0;  /* Control */

    for (int spin = 0; spin < 1000000; ++spin) {
        if (!(port->tfd & (0x80 | 0x08))) break;
        if (spin == 999999) {
            printf("ahci: port busy\n");
            return -1;
        }
    }

    port->is = 0xFFFFFFFFu;
    port->ci = (1u << slot);

    int guard = 0;
    while (port->ci & (1u << slot)) {
        if (port->is & HBA_PxIS_TFES) {
            uint32_t serr = port->serr;
            printf("ahci: task file error (tfd=0x%x serr=0x%x)\n", port->tfd, serr);
            port->serr = serr;  /* Clear error bits by writing them back */
            /* DO NOT clear ci or sact - let hardware manage them */
            return -1;
        }
        if (++guard > 1000000) {
            printf("ahci: command timeout\n");
            /* In a real driver, we would reset the port here */
            return -1;
        }
    }

    if (port->is & HBA_PxIS_TFES) {
        uint32_t serr = port->serr;
        printf("ahci: task file error (post tfd=0x%x serr=0x%x)\n", port->tfd, serr);
        port->serr = serr;
        return -1;
    }

    if (!write) {
        memcpy(buf, g_dma_buf, bytes);
    }
    
    port->is = 0xFFFFFFFFu;
    return 0;
}

int ahci_init(void) {
    spinlock_init(&g_ahci_lock);
    
    struct pci_device dev;
    if (pci_find_class(AHCI_CLASS_CODE, AHCI_SUBCLASS, 0xFF, &dev) != 0) {
        return -1;
    }

    uint32_t abar = pci_config_read32(dev.bus, dev.slot, dev.function, 0x24);
    if (!(abar & 0xFFFFFFF0u)) {
        /* BAR not assigned - this should be done by BIOS/firmware */
        printf("ahci: BAR5 not assigned (BIOS/firmware issue)\n");
        return -1;
    }
    abar &= 0xFFFFFFF0u;

    uint16_t command = pci_config_read16(dev.bus, dev.slot, dev.function, 0x04);
    command |= (1u << 1) | (1u << 2);
    pci_config_write16(dev.bus, dev.slot, dev.function, 0x04, command);

    uint32_t hba_virt = map_abar(abar);
    if (!hba_virt) return -1;
    g_hba = (volatile hba_mem_t*)(uintptr_t)hba_virt;

    /* Read initial values before reset */
    uint32_t cap_before = g_hba->cap;
    uint32_t pi_before = g_hba->pi;
    printf("ahci: cap=0x%x pi=0x%x ghc=0x%x (before reset)\n", cap_before, pi_before, g_hba->ghc);
    
    g_hba->ghc |= HBA_GHC_HR;
    for (int spin = 0; spin < 1000000; ++spin) {
        if (!(g_hba->ghc & HBA_GHC_HR)) break;
        if (spin == 999999) {
            printf("ahci: HBA reset timeout\n");
            return -1;
        }
    }

    /* Verify mapping still works after reset */
    uint32_t cap_after = g_hba->cap;
    uint32_t pi_after = g_hba->pi;
    printf("ahci: cap=0x%x pi=0x%x (after reset)\n", cap_after, pi_after);
    
    if (cap_after == 0xFFFFFFFF || pi_after == 0xFFFFFFFF) {
        printf("ahci: HBA registers unreadable after reset (mapping issue)\n");
        return -1;
    }

    g_hba->is = 0xFFFFFFFFu;
    g_hba->ghc |= HBA_GHC_AE;

    printf("ahci: abar=0x%x ports=0x%x\n", abar, g_hba->pi);

    uint32_t ports = g_hba->pi;
    printf("ahci: scanning %u implemented ports\n", __builtin_popcount(ports));
    for (uint8_t i = 0; i < AHCI_MAX_PORTS; ++i) {
        if (!(ports & (1u << i))) continue;
        volatile hba_port_t* port = &g_hba->ports[i];
        printf("ahci: checking port %u at offset 0x%x\n", i, (uint32_t)((uintptr_t)port - (uintptr_t)g_hba));
        port_comreset((hba_port_t*)port);
        printf("ahci: port %u ssts=0x%x sig=0x%x cmd=0x%x\n", i, port->ssts, port->sig, port->cmd);
        if (!check_drive_type((hba_port_t*)port)) {
            printf("ahci: port %u: no device detected\n", i);
            continue;
        }
        if (init_port_resources((hba_port_t*)port) != 0) {
            printf("ahci: port %u: resource init failed\n", i);
            continue;
        }
        g_active_port = (hba_port_t*)port;
        g_port_index = i;
        printf("ahci: port %u: initialized successfully\n", i);
        break;
    }

    if (!g_active_port) {
        printf("ahci: no usable port found\n");
        return -1;
    }

    g_dma_buf_phys = alloc_frame_low();
    if (!g_dma_buf_phys) return -1;
    g_dma_buf = (uint8_t*)phys_to_virt(g_dma_buf_phys);

    printf("ahci: using controller %x:%x bus=%u slot=%u func=%u port=%u dma=0x%x\n",
        dev.vendor_id, dev.device_id, dev.bus, dev.slot, dev.function, g_port_index, g_dma_buf_phys);

    g_ahci_ready = 1;
    return 0;
}

static int ahci_io(uint32_t lba, uint8_t count, void* buffer, int write) {
    if (!g_ahci_ready || !g_active_port) return -1;
    
    /* CRITICAL: Acquire lock to prevent concurrent access to shared DMA buffer */
    spinlock_acquire(&g_ahci_lock);
    
    int result = 0;
    uint8_t remaining = count;
    uint8_t* buf = (uint8_t*)buffer;
    while (remaining) {
        uint8_t n = remaining > AHCI_DMA_SECTORS ? AHCI_DMA_SECTORS : remaining;
        if (issue_cmd(g_active_port, lba, n, write, buf) != 0) {
            result = -1;
            break;  /* Don't return early - must release lock */
        }
        buf += n * SECTOR_SIZE;
        lba += n;
        remaining -= n;
    }
    
    spinlock_release(&g_ahci_lock);
    return result;
}

int ahci_read(uint32_t lba, uint8_t count, void* buffer) {
    return ahci_io(lba, count, buffer, 0);
}

int ahci_write(uint32_t lba, uint8_t count, const void* buffer) {
    return ahci_io(lba, count, (void*)buffer, 1);
}
