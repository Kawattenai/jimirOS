#include <kernel/idt.h>
#include <kernel/tty.h>
#include <kernel/stdio.h>
#include <kernel/pic.h>

/* --- External Assembly Functions --- */
extern void idt_load(struct IdtPtr* idt_ptr);
extern void irq_install(void);

/* --- External ISR Stubs --- */
extern void isr0();
extern void isr1();
extern void isr2();
extern void isr3();
extern void isr4();
extern void isr5();
extern void isr6();
extern void isr7();
extern void isr8();
extern void isr9();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15();
extern void isr16();
extern void isr17();
extern void isr128();
/* ... etc ... */

/* --- Global IDT --- */
#define IDT_ENTRIES 256
struct IdtEntry idt[IDT_ENTRIES];
struct IdtPtr   idt_ptr;

/*
 * This is the C-level fault handler.
 * It is now updated to print the error code.
 */
/* syscall dispatcher */
extern void syscall_dispatch(struct registers* regs);

void isr_fault_handler(struct registers* regs) {
    if (regs->int_num == 128) {
        syscall_dispatch(regs);
        return;
    }
    printf("--- KERNEL PANIC ---\n");
    printf("Received Exception: %d\n", regs->int_num);
    if (regs->int_num == 13) {
        printf("GPF context: EIP=0x%x CS=0x%x EFLAGS=0x%x\n", regs->eip, regs->cs, regs->eflags);
        printf("             ESP=0x%x SS=0x%x\n", regs->useresp, regs->ss);
        const uint8_t* p = (const uint8_t*)regs->eip;
        printf("Bytes @EIP:");
        for (int i=0;i<16;i++) { printf(" %x", p[i]); }
        printf("\n");
    }
    
    /* Page Faults (14) and GPFs (13) are common */
    if (regs->int_num == 14) {
        uint32_t fault_addr;
        asm volatile("movl %%cr2, %0" : "=r"(fault_addr));
        uint32_t cr3; asm volatile("movl %%cr3, %0" : "=r"(cr3));
        printf("Page Fault at address: 0x%x\n", fault_addr);
        printf("EIP=0x%x CS=0x%x EFLAGS=0x%x ESP=0x%x SS=0x%x CR3=0x%x\n", regs->eip, regs->cs, regs->eflags, regs->useresp, regs->ss, cr3);
        printf("Error Code: 0x%x (", regs->err_code);
        if (regs->err_code & 0x1) printf("protection-violation ");
        if (regs->err_code & 0x2) printf("write-error ");
        if (regs->err_code & 0x4) printf("user-mode ");
        printf(")\n");
    } else {
        printf("Error Code: 0x%x\n", regs->err_code);
    }
    
    printf("Halting system.\n");
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

void idt_set_entry(int index, uint32_t base, uint16_t selector, uint8_t flags) {
    idt[index].base_low  = base & 0xFFFF;
    idt[index].base_high = (base >> 16) & 0xFFFF;
    idt[index].selector  = selector;
    idt[index].zero      = 0;
    idt[index].flags     = flags;
}

void idt_initialize(void) {
    pic_remap();

    idt_ptr.limit = (sizeof(struct IdtEntry) * IDT_ENTRIES) - 1;
    idt_ptr.base  = (uint32_t)&idt;

    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_entry(i, 0, 0, 0);
    }

    /* 0x08 = Kernel Code Segment */
    /* 0x8E = 32-bit Interrupt Gate, Present */
    idt_set_entry(0, (uint32_t)isr0, 0x08, 0x8E);
    idt_set_entry(1, (uint32_t)isr1, 0x08, 0x8E);
    idt_set_entry(2, (uint32_t)isr2, 0x08, 0x8E);
    idt_set_entry(3, (uint32_t)isr3, 0x08, 0x8E);
    idt_set_entry(4, (uint32_t)isr4, 0x08, 0x8E);
    idt_set_entry(5, (uint32_t)isr5, 0x08, 0x8E);
    idt_set_entry(6, (uint32_t)isr6, 0x08, 0x8E);
    idt_set_entry(7, (uint32_t)isr7, 0x08, 0x8E);
    idt_set_entry(8, (uint32_t)isr8, 0x08, 0x8E);
    idt_set_entry(9, (uint32_t)isr9, 0x08, 0x8E);
    idt_set_entry(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_entry(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_entry(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_entry(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_entry(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_entry(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_entry(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_entry(17, (uint32_t)isr17, 0x08, 0x8E);

    /* Syscall gate: DPL=3 so user-mode can invoke int 0x80 */
    idt_set_entry(0x80, (uint32_t)isr128, 0x08, 0xEE);

    irq_install();

    idt_load(&idt_ptr);
}