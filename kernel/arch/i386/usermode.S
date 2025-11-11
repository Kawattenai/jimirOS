.global enter_user_mode
.type enter_user_mode, @function
# void enter_user_mode(void* entry, uint32_t user_stack)
enter_user_mode:
    # Set data segments to user data selector (0x23) without clobbering EAX
    mov $0x23, %cx
    mov %cx, %ds
    mov %cx, %es
    mov %cx, %fs
    mov %cx, %gs

    # args: entry at 4(%esp), user_stack at 8(%esp)
    mov 4(%esp), %eax   # entry point
    mov 8(%esp), %ebx   # user stack top

    # Push user stack segment and stack pointer
    pushl $0x23
    pushl %ebx

    # Push EFLAGS with IF set
    pushfl
    pop %ecx
    or $0x200, %ecx
    push %ecx

    # Push user CS and entry EIP
    pushl $0x1B
    pushl %eax

    iret
