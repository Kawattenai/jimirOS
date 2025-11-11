.globl ctx_switch
.type ctx_switch, @function
/* void ctx_switch(uint32_t* old_esp, uint32_t new_esp) */
ctx_switch:
    push %ebp
    mov %esp, %ebp
    /* save current ESP to *old_esp */
    mov 8(%ebp), %eax
    mov %esp, (%eax)
    /* load new ESP */
    mov 12(%ebp), %esp
    /* restore a frame compatible with our created stacks: pop pusha regs and return to trampoline */
    pop %eax
    pop %ecx
    pop %edx
    pop %ebx
    pop %esp
    pop %ebp
    pop %esi
    pop %edi
    ret
