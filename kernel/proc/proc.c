#include <kernel/proc.h>
#include <kernel/gdt.h>
#include <kernel/stdio.h>
#include <kernel/process.h>
#include <kernel/vmm.h>

/* Globals used by the assembly thunk to resume on the correct stack. */
void*    g_proc_resume_eip = 0;
uint32_t g_proc_resume_esp = 0;
uint32_t g_proc_resume_ebp = 0;
static int s_waiting = 0;
int g_proc_last_exit = 0;
volatile int g_proc_do_switch_now = 0;

void proc_begin_wait(void* resume_eip, uint32_t resume_esp, uint32_t resume_ebp) {
    g_proc_resume_eip = resume_eip;
    g_proc_resume_esp = resume_esp;
    g_proc_resume_ebp = resume_ebp;
    s_waiting = 1;
}

/* Trampoline: returns to the caller of run_user_and_wait on saved ESP. */
extern void proc_ret_to_caller(void);

int proc_prepare_kernel_return(struct registers* regs, int exit_code) {
    if (!s_waiting || !g_proc_resume_eip || !g_proc_resume_esp) return 0;
    g_proc_last_exit = exit_code;
    s_waiting = 0;
    return 1;
}

int proc_last_exit_code(void) { return g_proc_last_exit; }

extern void enter_user_mode(void* entry, uint32_t user_stack);

static inline uint32_t read_cr3(void) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3,%0":"=r"(cr3));
    return cr3;
}

__attribute__((noinline,optimize("O0")))
int run_user_and_wait(void* entry, uint32_t user_stack_top) {
    uint32_t resume_esp;
    __asm__ volatile ("movl %%esp, %0" : "=r"(resume_esp));
    uint32_t resume_ebp;
    __asm__ volatile ("movl %%ebp, %0" : "=r"(resume_ebp));
    void* resume_eip = &&after_user;
    
    printf("[proc] run_user_and_wait: entry=%p stack=0x%x\n", entry, user_stack_top);
    
    int pid = process_create(0);
    if (pid < 0) {
        printf("[proc] FAILED to create process\n");
        return -1;
    }
    
    process_t* proc = process_find(pid);
    if (!proc) {
        printf("[proc] FAILED to find process %d\n", pid);
        return -1;
    }
    
    proc->page_dir = read_cr3();
    
    proc->context.eip = (uint32_t)entry;
    proc->context.esp = user_stack_top;
    proc->context.ebp = 0;
    proc->context.eflags = 0x202;
    proc->context.cs = 0x1B;
    proc->context.ss = 0x23;
    proc->context.ds = 0x23;
    proc->state = PROC_RUNNING;
    
    process_set_current(pid);
    
    printf("[proc] Process %d set up and ready\n", pid);
    
    proc_begin_wait(resume_eip, resume_esp, resume_ebp);
    __asm__ volatile("": : : "memory");
    enter_user_mode(entry, user_stack_top);
    __asm__ volatile("": : : "memory");
    
after_user:
    printf("[proc] after_user: resumed in kernel, exit_code=%d\n", proc_last_exit_code());
    
    // Clean up the process
    if (proc) {
        process_destroy(pid);
    }
    
    return proc_last_exit_code();
}

/* Assembly helper */
extern void proc_switch_to_kernel_and_jump(uint32_t esp, void* eip);

void proc_switch_to_kernel_now(void) {
    /* Jump to the saved resume point on the saved kernel stack. */
    void* eip = g_proc_resume_eip;
    uint32_t esp = g_proc_resume_esp;
    proc_switch_to_kernel_and_jump(esp, eip);
    __builtin_unreachable();
}

/* Request the ISR tail path to perform the switch before iret. */
void proc_request_isr_tail_switch(void) {
    g_proc_do_switch_now = 1;
}
