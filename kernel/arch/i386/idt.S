.extern isr_fault_handler

.global idt_load
.type idt_load, @function
idt_load:
    movl 4(%esp), %eax
    lidt (%eax)
    ret

/* Macro for ISRs that do NOT push an error code (0-7, 9, 15+) */
.macro ISR_NOERRCODE int_num
.global isr\int_num
isr\int_num:
    cli
    push $0         /* Push a DUMMY error code */
    push $\int_num   /* Push the interrupt number */
    jmp isr_common_stub
.endm

/* Macro for ISRs that DO push an error code (8, 10-14, 17, etc.) */
.macro ISR_ERRCODE int_num
.global isr\int_num
isr\int_num:
    cli
    /* The CPU pushes the REAL error code */
    push $\int_num   /* Push the interrupt number */
    jmp isr_common_stub
.endm

/* Create stubs for the first 32 exceptions */
ISR_NOERRCODE 0     /* 0: Divide by Zero */
ISR_NOERRCODE 1     /* 1: Debug */
ISR_NOERRCODE 2     /* 2: NMI */
ISR_NOERRCODE 3     /* 3: Breakpoint */
ISR_NOERRCODE 4     /* 4: Overflow */
ISR_NOERRCODE 5     /* 5: Bound Range Exceeded */
ISR_NOERRCODE 6     /* 6: Invalid Opcode */
ISR_NOERRCODE 7     /* 7: Device Not Available */
ISR_ERRCODE   8     /* 8: Double Fault */
ISR_NOERRCODE 9     /* 9: Coprocessor Segment Overrun */
ISR_ERRCODE   10    /* 10: Invalid TSS */
ISR_ERRCODE   11    /* 11: Segment Not Present */
ISR_ERRCODE   12    /* 12: Stack-Segment Fault */
ISR_ERRCODE   13    /* 13: General Protection Fault */
ISR_ERRCODE   14    /* 14: Page Fault */
ISR_NOERRCODE 15    /* 15: (Reserved) */
ISR_NOERRCODE 16    /* 16: x87 FPU Error */
ISR_ERRCODE   17    /* 17: Alignment Check */
/* ... and so on ... */

/* Syscall interrupt (int 0x80) - no error code */
ISR_NOERRCODE 128

.type isr_common_stub, @function
isr_common_stub:
    pusha

    movw %ds, %ax
    pushl %eax
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

     /* Reserve generous shadow space to prevent C calls (e.g., printf) from
         clobbering the hardware iret frame. Increase from 512 to 4096 bytes
         to accommodate deeper call chains. */
     leal 0(%esp), %eax          /* eax = pointer to regs (ds at top) */
     subl $4096, %esp            /* make room for C stack usage */
    pushl %eax                  /* arg: struct registers* */
    call isr_fault_handler
    addl $4, %esp
     addl $4096, %esp

    popl %eax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    /* Before restoring regs, optionally perform a kernel stack switch/jump. */
    .extern g_proc_do_switch_now
    .extern g_proc_resume_esp
    .extern g_proc_resume_eip
    cmpl $0, g_proc_do_switch_now
    je 1f
    /* clear flag */
    movl $0, g_proc_do_switch_now
    /* Switch to saved kernel stack and jump to saved resume EIP (noreturn). */
    cli
    mov g_proc_resume_esp, %esp
    .extern g_proc_resume_ebp
    mov g_proc_resume_ebp, %ebp
    jmp *g_proc_resume_eip
1:
    popa

    addl $8, %esp   /* Pop int_num and err_code */
    sti
    iret