#include <kernel/stdio.h>
#include <kernel/tty.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/pit.h>
#include <kernel/multiboot.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/kmalloc.h>
#include <kernel/keyboard.h>
#include <kernel/syscall.h>
#include <kernel/gdt.h>
#include <kernel/fs.h>
#include <kernel/block.h>
#include <kernel/sched.h>
#include <kernel/process.h>
#include <kernel/ports.h>

extern void enter_user_mode(void* entry, uint32_t user_stack);

/*
 * The multiboot struct is passed at a LOW address (e.g., 0x5000).
 * We must add 0xC0000000 to it to access it from the higher half.
 */
void kernel_main(uint32_t magic, uint32_t multiboot_addr) {
    /* Boot code already switched to a high kernel stack before calling us. */

        /*
         * 2. Initialize all kernel subsystems.
         * These functions are now all running at high addresses.
         */
        /* Bring up serial first for early logging */
            extern void serial_init(void);
            serial_init();
	gdt_initialize();
	idt_initialize();
  /* <-- ADD THIS */
	terminal_initialize(); /* This must be modified to use the high VGA address */
            pit_init(100);

    /* Tiny syscall smoke test from ring0 (allowed since DPL=3): write to serial */
    {
        static const char msg[] = "[syscall] write from kernel via int 0x80\n";
        uint32_t ret;
        __asm__ volatile (
            "int $0x80"
            : "=a"(ret)
            : "a"(SYS_write), "b"(msg), "c"((uint32_t)(sizeof(msg) - 1))
            : "edx", "esi", "edi", "memory", "cc"
        );
        (void)ret;
    }
	
    /* We can now print */
    printf("Hello, Higher-Half World!\n");
    /* Initialize memory subsystems */
    if (magic == MULTIBOOT_MAGIC) {
        uint32_t mb_high = multiboot_addr + 0xC0000000u; /* higher-half view */
        /* store for later module access */
        extern void bootinfo_set_mb(uint32_t);
        bootinfo_set_mb(mb_high);
        pmm_init(mb_high);
    } else {
        printf("No multiboot info; PMM may be limited.\n");
    }
    vmm_init();

    /* Set TSS kernel stack (use current esp) for privilege transitions */
    uint32_t cur_esp; __asm__ volatile("movl %%esp, %0" : "=r"(cur_esp));
    tss_set_kernel_stack(cur_esp);

    /* Bootstrap a small heap at 0xC0200000, map ~64 KiB initially */
    kmalloc_init((void*)0xC0200000u, 64*1024);
    void* test = kmalloc(1024);
    printf("kmalloc(1024) -> %p (phys %x)\n", test, vmm_resolve((uint32_t)test));

    /* Init keyboard driver */
    keyboard_init();
    
    /* Init USB (for USB keyboards) */
    extern int usb_init(void);
    if (usb_init() != 0) {
        printf("usb: no USB controller found, using PS/2 only\n");
    }
    
    /* Init block layer (optional) */
    if (block_init() != 0) {
        printf("block: no ATA disk detected, continuing with modules\n");
    }

    /* Init filesystem (ext2 preferred if present) */
    fs_init();
    /* Init scheduler */
    sched_init();
    /* Init process management */
    process_init();
    
    /* Init HTAS scheduler */
    extern void htas_init(void);
    htas_init();
    printf("HTAS: Initialized (4 CPUs, 2 NUMA nodes)\n");
    
    /* Accessing multiboot info (must add offset) */
    if (magic == 0x2BADB002) {
        printf("Multiboot magic is correct.\n");
        /* struct multiboot_info* mb_info = (struct multiboot_info*)(multiboot_addr + 0xC0000000); */
        /* printf("Mem lower: %dKB\n", mb_info->mem_lower); */
    }

    /* Enable interrupts for timer and keyboard */
    asm volatile ("sti");
    
    printf("\n*** NOTE: Type commands in the TERMINAL (not GUI window) ***\n");
    printf("*** Serial console is active and working! ***\n\n");

    /* Start interactive shell */
    extern void shell_run(void);
    shell_run();
}