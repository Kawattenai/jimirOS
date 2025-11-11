#ifndef _KERNEL_PROC_H
#define _KERNEL_PROC_H

#include <stdint.h>
#include <kernel/idt.h> /* for struct registers */

/* Begin a synchronous user run: capture resume point for SYS_exit to return to. */
void proc_begin_wait(void* resume_eip, uint32_t resume_esp, uint32_t resume_ebp);

/* Called by syscall handler for SYS_exit to redirect iret back to kernel. */
int proc_prepare_kernel_return(struct registers* regs, int exit_code);

/* Last exit code from a finished user run. */
int proc_last_exit_code(void);

/* Helper: run user entry and wait until it calls SYS_exit, then return exit code. */
int run_user_and_wait(void* entry, uint32_t user_stack_top);

/* Force immediate switch to saved kernel stack and resume point (noreturn). */
void proc_switch_to_kernel_now(void) __attribute__((noreturn));
/* Request ISR tail to switch to saved kernel stack and jump. */
void proc_request_isr_tail_switch(void);

#endif
