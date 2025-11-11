

.global gdt_load
.type gdt_load, @function
gdt_load:

    push %ebp
    movl %esp, %ebp
    
    
    movl 8(%ebp), %eax

    lgdt (%eax)
    

    ljmpl $0x08, $flush_segments

flush_segments:

    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    
    movl %ebp, %esp
    pop %ebp
    ret