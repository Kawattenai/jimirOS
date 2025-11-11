#include <kernel/bootinfo.h>
#include <kernel/multiboot.h>

static multiboot_info_t* s_mb = 0;

void bootinfo_set_mb(uint32_t mb_high_addr) {
    s_mb = (multiboot_info_t*)mb_high_addr;
}

/* Return first module if present */
int bootinfo_first_module(void** start, uint32_t* size) {
    if (!s_mb) return -1;
    if (!(s_mb->flags & (1u<<3))) return -2; /* no modules */
    if (s_mb->mods_count == 0) return -3;
    typedef struct {
        uint32_t mod_start;
        uint32_t mod_end;
        uint32_t string;
        uint32_t reserved;
    } mb_module_t;
    mb_module_t* mods = (mb_module_t*)(s_mb->mods_addr + 0xC0000000u);
    uint32_t start_phys = mods[0].mod_start;
    uint32_t end_phys   = mods[0].mod_end;
    if (end_phys <= start_phys) return -4;
    *start = (void*)(start_phys + 0xC0000000u);
    *size  = end_phys - start_phys;
    return 0;
}

int bootinfo_module_count(void) {
    if (!s_mb) return 0;
    if (!(s_mb->flags & (1u<<3))) return 0; /* no modules */
    return (int)s_mb->mods_count;
}

int bootinfo_get_module(int index, void** start, uint32_t* size, const char** name) {
    if (!s_mb) return -1;
    if (!(s_mb->flags & (1u<<3))) return -2; /* no modules */
    if (index < 0 || (unsigned)index >= s_mb->mods_count) return -3;
    typedef struct {
        uint32_t mod_start;
        uint32_t mod_end;
        uint32_t string;
        uint32_t reserved;
    } mb_module_t;
    mb_module_t* mods = (mb_module_t*)(s_mb->mods_addr + 0xC0000000u);
    uint32_t start_phys = mods[index].mod_start;
    uint32_t end_phys   = mods[index].mod_end;
    uint32_t str_phys   = mods[index].string;
    if (end_phys <= start_phys) return -4;
    if (start) *start = (void*)(start_phys + 0xC0000000u);
    if (size)  *size  = end_phys - start_phys;
    if (name)  *name  = (const char*)(str_phys ? (str_phys + 0xC0000000u) : 0);
    return 0;
}
