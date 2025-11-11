.extern irq_handler

/*
 * This macro now takes TWO arguments:
 * irq_num: The IRQ number (0, 1, etc.) to generate the function name (irq0, irq1)
 * vector_num: The IDT vector (32, 33, etc.) to push onto the stack
 */
.macro IRQ_STUB irq_num, vector_num
.global irq\irq_num
irq\irq_num:
    cli
    push $0              /* dummy error code to match struct registers */
    push $\vector_num   /* --- THIS IS THE FIX --- */
    jmp irq_common_stub
.endm

/*
 * Create the stubs
 * IRQ 0 (Timer) is at IDT 32. It calls irq0. irq0 must push 32.
 * IRQ 1 (Keyboard) is at IDT 33. It calls irq1. irq1 must push 33.
 */
IRQ_STUB 0, 32
IRQ_STUB 1, 33
/* ... add 2-15 as needed, e.g., IRQ_STUB 2, 34 ... */


.type irq_common_stub, @function
irq_common_stub:
    pusha

    movw %ds, %ax
    pushl %eax
    movw $0x10, %ax
    movw %ax, %ds

    pushl %esp
    call irq_handler
    addl $4, %esp

    popl %eax
    movw %ax, %ds

    popa
    addl $8, %esp /* Pop dummy error code + vector number */
    sti
    iret