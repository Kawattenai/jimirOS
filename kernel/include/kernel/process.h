#ifndef _KERNEL_PROCESS_H
#define _KERNEL_PROCESS_H

#include <stdint.h>
#include <kernel/idt.h>  /* for struct registers */

/* Forward declaration for HTAS */
typedef struct htas_task_info htas_task_info_t;

#define MAX_PROCESSES 32

typedef enum {
    PROC_UNUSED = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_ZOMBIE
} proc_state_t;

/* Saved user context for a process */
typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eip;
    uint32_t eflags;
    uint32_t cs, ds, ss, es, fs, gs;
} proc_context_t;

/* Process Control Block */
typedef struct process {
    int pid;
    int ppid;               // Parent process ID
    proc_state_t state;
    uint32_t page_dir;      // Physical address of page directory
    proc_context_t context; // Saved user registers
    int exit_code;          // Exit code when zombie
    uint32_t brk;           // Current program break for sbrk/brk
    
    /* HTAS scheduler extensions */
    htas_task_info_t* htas_info;  // Task profile and statistics
    void* user_data;               // For benchmark identification
} process_t;

/* Initialize process management */
void process_init(void);

/* Create a new process (allocates PID and PCB) */
int process_create(int ppid);

/* Find process by PID */
process_t* process_find(int pid);

/* Get current running process */
process_t* process_current(void);

/* Alias for HTAS compatibility */
#define process_get_current() process_current()

/* Get all processes (for HTAS) */
process_t* process_get_list(void);

/* Get current PID (for HTAS) */
int process_get_current_pid(void);

/* Yield CPU (for HTAS benchmarks) */
void process_yield(void);

/* Set current running process */
void process_set_current(int pid);

/* Destroy a process and free its resources */
void process_destroy(int pid);

/* Fork current process - returns child PID to parent, 0 to child */
int process_fork(void);

/* Exit current process with exit code */
void process_exit(int code);

/* Wait for child process to exit */
int process_wait(int* status);

/* Switch to a different process */
void process_switch(int new_pid);

/* Schedule next ready process (called from timer interrupt) */
void process_schedule(struct registers* regs);

#endif
