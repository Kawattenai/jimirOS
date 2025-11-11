#include <kernel/elf.h>
#include <kernel/proc.h>
#include <kernel/bootinfo.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/stdio.h>
#include <stdint.h>
#include <string.h>

extern void enter_user_mode(void* entry, uint32_t user_stack);

static int map_user_range(uint32_t va_start, uint32_t size, const uint8_t* src, uint32_t src_len) {
    uint32_t page = va_start & ~0xFFFu;
    uint32_t end  = (va_start + size + 0xFFFu) & ~0xFFFu;
    for (uint32_t a = page; a < end; a += 4096) {
        uint32_t phys = pmm_alloc_frame();
        if (!phys) return -1;
        if (vmm_map(a, phys, PAGE_WRITE|PAGE_USER) != 0) return -2;
        /* zero page */
        uint8_t* vp = (uint8_t*)a;
        for (int i=0;i<4096;i++) vp[i]=0;
    }
    /* copy in file portion */
    if (src && src_len) {
        for (uint32_t i=0; i<src_len; ++i) ((uint8_t*)(va_start))[i] = src[i];
    }
    return 0;
}

int elf_run_first_module(void) {
    void* img; uint32_t size;
    int r = bootinfo_first_module(&img, &size);
    if (r != 0) { printf("no module: %d\n", r); return r; }
    if (size < sizeof(Elf32_Ehdr)) { printf("module too small\n"); return -10; }
    Elf32_Ehdr* eh = (Elf32_Ehdr*)img;
    if (!(eh->e_ident[0]==0x7F && eh->e_ident[1]=='E' && eh->e_ident[2]=='L' && eh->e_ident[3]=='F')) { printf("not ELF\n"); return -11; }
    if (eh->e_machine != 3 /* EM_386 */) { printf("bad machine\n"); return -12; }
    if (eh->e_phoff == 0 || eh->e_phnum == 0) { printf("no phdrs\n"); return -13; }
    /* map all PT_LOAD segments */
    uint32_t first_load_vaddr = 0;
    for (uint16_t i=0;i<eh->e_phnum;i++) {
        Elf32_Phdr* ph = (Elf32_Phdr*)((uint8_t*)img + eh->e_phoff + i*eh->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;
        if (!ph->p_memsz) continue;
        const uint8_t* src = (uint8_t*)img + ph->p_offset;
        uint32_t src_len = ph->p_filesz;
        int mr = map_user_range(ph->p_vaddr, ph->p_memsz, src, src_len);
        if (mr != 0) { printf("map seg fail %d\n", mr); return -20; }
        if (!first_load_vaddr) first_load_vaddr = ph->p_vaddr;
    }
    /* map a user stack (16 KiB) */
    const uint32_t USTACK_BASE = 0x00400000u;
    for (int i=0;i<4;i++) {
        uint32_t phys = pmm_alloc_frame(); if (!phys) return -30;
        if (vmm_map(USTACK_BASE + i*4096, phys, PAGE_WRITE|PAGE_USER) != 0) return -31;
        uint8_t* p = (uint8_t*)(USTACK_BASE + i*4096); for (int j=0;j<4096;j++) p[j]=0;
    }
    uint32_t entry = eh->e_entry;
    if (!entry) entry = first_load_vaddr ? first_load_vaddr : 0x00410000u;
    printf("ELF entry=0x%x\n", entry);
    (void)run_user_and_wait((void*)(uintptr_t)entry, USTACK_BASE + 4*4096);
    return 0;
}

static int name_match(const char* a, const char* b) {
    /* Compare by basename equivalence. */
    const char* ba = a; const char* bb = b;
    for (const char* p=a; *p; ++p) if (*p=='/'||*p=='\\') ba=p+1;
    for (const char* p=b; *p; ++p) if (*p=='/'||*p=='\\') bb=p+1;
    while (*ba && (*ba==*bb)) { ba++; bb++; }
    return (unsigned char)*ba - (unsigned char)*bb;
}

int elf_run_module_by_name(const char* name) {
    int n = bootinfo_module_count();
    for (int i=0;i<n;i++) {
        void* img; uint32_t size; const char* nm=0;
        if (bootinfo_get_module(i, &img, &size, &nm) != 0) continue;
        if (nm && name_match(nm, name)==0) {
            /* Treat this image like first_module path, but use this img/size */
            if (size < sizeof(Elf32_Ehdr)) { printf("module too small\n"); return -10; }
            Elf32_Ehdr* eh = (Elf32_Ehdr*)img;
            if (!(eh->e_ident[0]==0x7F && eh->e_ident[1]=='E' && eh->e_ident[2]=='L' && eh->e_ident[3]=='F')) { printf("not ELF\n"); return -11; }
            if (eh->e_machine != 3) { printf("bad machine\n"); return -12; }
            if (eh->e_phoff == 0 || eh->e_phnum == 0) { printf("no phdrs\n"); return -13; }
            uint32_t first_load_vaddr = 0;
            for (uint16_t j=0;j<eh->e_phnum;j++) {
                Elf32_Phdr* ph = (Elf32_Phdr*)((uint8_t*)img + eh->e_phoff + j*eh->e_phentsize);
                if (ph->p_type != PT_LOAD) continue;
                if (!ph->p_memsz) continue;
                const uint8_t* src = (uint8_t*)img + ph->p_offset;
                uint32_t src_len = ph->p_filesz;
                int mr = map_user_range(ph->p_vaddr, ph->p_memsz, src, src_len);
                if (mr != 0) { printf("map seg fail %d\n", mr); return -20; }
                if (!first_load_vaddr) first_load_vaddr = ph->p_vaddr;
            }
            /* stack */
            const uint32_t USTACK_BASE = 0x00400000u;
            for (int s=0;s<4;s++) { uint32_t phys = pmm_alloc_frame(); if (!phys) return -30; if (vmm_map(USTACK_BASE + s*4096, phys, PAGE_WRITE|PAGE_USER) != 0) return -31; uint8_t* p=(uint8_t*)(USTACK_BASE + s*4096); for(int k=0;k<4096;k++) p[k]=0; }
            uint32_t entry = eh->e_entry; if (!entry) entry = first_load_vaddr ? first_load_vaddr : 0x00410000u;
            printf("ELF entry=0x%x\n", entry);
            (void)run_user_and_wait((void*)(uintptr_t)entry, USTACK_BASE + 4*4096);
            return 0;
        }
    }
    printf("no module by name: %s\n", name);
    return -1;
}

/* New function: Load and run ELF from filesystem */
int elf_run_from_filesystem(const char* path) {
    extern int fs_open(const char* path);
    extern int fs_read(int fd, void* buf, unsigned len);
    extern int fs_close(int fd);
    
    printf("Loading ELF from filesystem: %s\n", path);
    
    /* Open the file */
    int fd = fs_open(path);
    if (fd < 0) {
        printf("Failed to open: %s (fd=%d)\n", path, fd);
        return -1;
    }
    
    printf("Opened file, fd=%d\n", fd);
    
    /* Allocate a temporary buffer for the ELF file (max 64KB) */
    #define MAX_ELF_SIZE (64*1024)
    static uint8_t elf_buffer[MAX_ELF_SIZE];
    
    /* Read the entire file */
    int total_read = 0;
    while (total_read < MAX_ELF_SIZE) {
        int n = fs_read(fd, elf_buffer + total_read, MAX_ELF_SIZE - total_read);
        printf("fs_read returned %d bytes (total so far: %d)\n", n, total_read + (n > 0 ? n : 0));
        if (n <= 0) break;
        total_read += n;
    }
    fs_close(fd);
    
    if (total_read < (int)sizeof(Elf32_Ehdr)) {
        printf("File too small to be ELF: %d bytes\n", total_read);
        return -10;
    }
    
    printf("Read %d bytes from %s\n", total_read, path);
    
    /* Debug: show first 32 bytes */
    printf("First 32 bytes: ");
    for (int i = 0; i < 32 && i < total_read; i++) {
        printf("%02x ", elf_buffer[i]);
    }
    printf("\n");
    
    /* Parse and load the ELF */
    Elf32_Ehdr* eh = (Elf32_Ehdr*)elf_buffer;
    
    /* Verify ELF magic */
    if (!(eh->e_ident[0]==0x7F && eh->e_ident[1]=='E' && 
          eh->e_ident[2]=='L' && eh->e_ident[3]=='F')) {
        printf("Not a valid ELF file\n");
        return -11;
    }
    
    if (eh->e_machine != 3 /* EM_386 */) {
        printf("Wrong architecture (expected i386)\n");
        return -12;
    }
    
    if (eh->e_phoff == 0 || eh->e_phnum == 0) {
        printf("No program headers\n");
        return -13;
    }
    
    printf("ELF valid: entry=0x%x, %d program headers\n", eh->e_entry, eh->e_phnum);
    
    /* Map all PT_LOAD segments */
    uint32_t first_load_vaddr = 0;
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        Elf32_Phdr* ph = (Elf32_Phdr*)(elf_buffer + eh->e_phoff + i * eh->e_phentsize);
        
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz == 0) continue;
        
        printf("Loading segment %d: vaddr=0x%x memsz=%u filesz=%u\n",
               i, ph->p_vaddr, ph->p_memsz, ph->p_filesz);
        
        const uint8_t* src = elf_buffer + ph->p_offset;
        uint32_t src_len = ph->p_filesz;
        
        int mr = map_user_range(ph->p_vaddr, ph->p_memsz, src, src_len);
        if (mr != 0) {
            printf("Failed to map segment %d (error %d)\n", i, mr);
            return -20;
        }
        
        if (!first_load_vaddr) {
            first_load_vaddr = ph->p_vaddr;
        }
    }
    
    /* Map user stack (16 KiB at 0x400000) */
    const uint32_t USTACK_BASE = 0x00400000u;
    for (int i = 0; i < 4; i++) {
        uint32_t phys = pmm_alloc_frame();
        if (!phys) {
            printf("Failed to allocate stack frame\n");
            return -30;
        }
        
        if (vmm_map(USTACK_BASE + i*4096, phys, PAGE_WRITE|PAGE_USER) != 0) {
            printf("Failed to map stack\n");
            return -31;
        }
        
        /* Zero the stack page */
        uint8_t* p = (uint8_t*)(USTACK_BASE + i*4096);
        for (int j = 0; j < 4096; j++) {
            p[j] = 0;
        }
    }
    
    /* Determine entry point */
    uint32_t entry = eh->e_entry;
    if (!entry) {
        entry = first_load_vaddr ? first_load_vaddr : 0x00410000u;
    }
    
    printf("Starting ELF at entry=0x%x, stack=0x%x\n", entry, USTACK_BASE + 4*4096);
    
    /* Run the program */
    (void)run_user_and_wait((void*)(uintptr_t)entry, USTACK_BASE + 4*4096);
    
    printf("Program exited\n");
    return 0;
}

