.globl proc_kernel_resume_thunk
.type proc_kernel_resume_thunk, @function
.extern g_proc_resume_eip
.extern g_proc_resume_esp
.extern g_proc_resume_ebp
.extern g_proc_last_exit
proc_kernel_resume_thunk:
    /* Load saved kernel stack and jump to saved resume point */
    mov g_proc_resume_esp, %esp
    mov g_proc_resume_ebp, %ebp
    jmp *g_proc_resume_eip

.globl proc_switch_to_kernel_and_jump
.type proc_switch_to_kernel_and_jump, @function
/* cdecl: void proc_switch_to_kernel_and_jump(uint32_t esp, void* eip) */
proc_switch_to_kernel_and_jump:
    push %ebp
    mov %esp, %ebp
    cli
    mov 8(%ebp), %eax   /* esp arg */
    mov 12(%ebp), %edx  /* eip arg */
    mov %eax, %esp      /* Set ESP to the saved value */
    movl g_proc_resume_ebp, %ebp  /* Load EBP from saved value */
    jmp *%edx

/* Trampoline: switch to saved kernel ESP, put last exit code in EAX, and RET.
    This cleanly returns to the caller of run_user_and_wait with the exit code. */
.globl proc_ret_to_caller
.type proc_ret_to_caller, @function
proc_ret_to_caller:
     movl g_proc_resume_esp, %esp
     movl g_proc_last_exit, %eax
     sti
     ret
