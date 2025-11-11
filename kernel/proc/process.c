#include <kernel/process.h>
#include <kernel/idt.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/stdio.h>
#include <kernel/htas.h>
#include <string.h>
#include <stdbool.h>

static process_t process_table[MAX_PROCESSES];
static int current_pid = -1;
static int next_pid = 1;

/* Helper: get kernel page directory (CR3) */
static inline uint32_t read_cr3(void) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3,%0":"=r"(cr3));
    return cr3;
}

/* Helper: set page directory (CR3) */
static inline void write_cr3(uint32_t pd_phys) {
    __asm__ volatile("mov %0,%%cr3"::"r"(pd_phys):"memory");
}

/* Helper: copy page directory for fork */
static uint32_t clone_page_directory(uint32_t src_pd_phys) {
    printf("process: WARNING - clone_page_directory not fully implemented, sharing address space\n");
    return src_pd_phys;
}

/* Release all user-space mappings held by the given page directory. This
   frees user pages and their page tables, and (when the directory is not the
   currently active one) the page directory itself. */
static void free_user_address_space(uint32_t pd_phys) {
    if (!pd_phys) return;

    uint32_t current_pd = read_cr3();
    uint32_t* pd = (uint32_t*)pd_phys;

    for (int i = 0; i < 768; ++i) {
        uint32_t pde = pd[i];
        if (!(pde & PAGE_PRESENT)) continue;
        if (!(pde & PAGE_USER)) continue;

        uint32_t pt_phys = pde & ~0xFFFu;
        uint32_t* pt = (uint32_t*)pt_phys;

        if (pd_phys == current_pd) {
            /* Unmap via the active page directory so TLB entries are flushed. */
            for (int j = 0; j < 1024; ++j) {
                uint32_t pte = pt[j];
                if (!(pte & PAGE_PRESENT)) continue;
                if (!(pte & PAGE_USER)) continue;
                uint32_t virt = ((uint32_t)i << 22) | ((uint32_t)j << 12);
                uint32_t phys = pte & ~0xFFFu;
                vmm_unmap(virt);
                pmm_free_frame(phys);
            }

            /* If the table no longer has user mappings, release it. */
            int still_user = 0;
            for (int j = 0; j < 1024; ++j) {
                if ((pt[j] & PAGE_PRESENT) && (pt[j] & PAGE_USER)) {
                    still_user = 1;
                    break;
                }
            }
            if (!still_user) {
                pd[i] = 0;
                pmm_free_frame(pt_phys);
            }
        } else {
            for (int j = 0; j < 1024; ++j) {
                uint32_t pte = pt[j];
                if (!(pte & PAGE_PRESENT)) continue;
                if (pte & PAGE_USER) {
                    uint32_t phys = pte & ~0xFFFu;
                    pmm_free_frame(phys);
                }
                pt[j] = 0;
            }
            pd[i] = 0;
            pmm_free_frame(pt_phys);
        }
    }

    if (pd_phys != current_pd) {
        pmm_free_frame(pd_phys);
    }
}

void process_init(void) {
    memset(process_table, 0, sizeof(process_table));
    current_pid = -1;
    next_pid = 1;
    printf("process: initialized (max=%d)\n", MAX_PROCESSES);
}

int process_create(int ppid) {
    // Find free slot
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_UNUSED) {
            process_table[i].pid = next_pid++;
            process_table[i].ppid = ppid;
            process_table[i].state = PROC_READY;
            process_table[i].page_dir = 0;
            process_table[i].exit_code = 0;
            process_table[i].brk = 0;
            process_table[i].htas_info = 0;  // Initialize HTAS info
            process_table[i].user_data = 0;  // Initialize user data
            memset(&process_table[i].context, 0, sizeof(proc_context_t));
            return process_table[i].pid;
        }
    }
    return -1; // No free slots
}

process_t* process_find(int pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != PROC_UNUSED && process_table[i].pid == pid) {
            return &process_table[i];
        }
    }
    return 0;
}

process_t* process_current(void) {
    if (current_pid < 0) return 0;
    return process_find(current_pid);
}

/* Get all processes (for HTAS) */
process_t* process_get_list(void) {
    return process_table;
}

/* Get current PID (for HTAS) */
int process_get_current_pid(void) {
    return current_pid;
}

/* Yield CPU (for HTAS benchmarks) */
void process_yield(void) {
    // Just halt until next interrupt
    __asm__ volatile("hlt");
}

void process_set_current(int pid) {
    current_pid = pid;
}

void process_destroy(int pid) {
    process_t* proc = process_find(pid);
    if (!proc) return;

    /* Free user address space resources (page tables, frames, etc.). */
    if (proc->page_dir) {
        free_user_address_space(proc->page_dir);
        proc->page_dir = 0;
    }
    
    proc->state = PROC_UNUSED;
    
    printf("process: destroyed pid=%d\n", pid);
}

int process_fork(void) {
    process_t* parent = process_current();
    if (!parent) {
        printf("process: fork failed - no current process\n");
        return -1;
    }

    // Create child process
    int child_pid = process_create(parent->pid);
    if (child_pid < 0) {
        printf("process: fork failed - no free slots\n");
        return -1;
    }

    process_t* child = process_find(child_pid);
    if (!child) {
        printf("process: fork failed - couldn't find child\n");
        return -1;
    }

    // Clone page directory and memory
    child->page_dir = clone_page_directory(parent->page_dir);
    if (!child->page_dir) {
        printf("process: fork failed - couldn't clone page directory\n");
        process_destroy(child_pid);
        return -1;
    }

    // Copy parent context to child
    memcpy(&child->context, &parent->context, sizeof(proc_context_t));
    
    // Child returns 0 from fork
    child->context.eax = 0;
    
    // Copy other process state
    child->brk = parent->brk;
    child->state = PROC_READY;

    printf("process: fork: parent=%d child=%d\n", parent->pid, child_pid);

    // Parent returns child PID
    return child_pid;
}

void process_exit(int code) {
    process_t* proc = process_current();
    if (!proc) {
        printf("process: exit called with no current process\n");
        return;
    }

    proc->exit_code = code;
    proc->state = PROC_ZOMBIE;
    
    printf("process: pid=%d exited with code %d\n", proc->pid, code);

    // Wake up parent if it's waiting
    if (proc->ppid > 0) {
        process_t* parent = process_find(proc->ppid);
        if (parent && parent->state == PROC_BLOCKED) {
            printf("process: waking up parent %d\n", proc->ppid);
            parent->state = PROC_READY;
        }
    }

    // TODO: Reparent children to init process
}

int process_wait(int* status) {
    process_t* parent = process_current();
    if (!parent) {
        return -1;
    }

    // Keep checking for zombie children
    while (1) {
        // Find any zombie child
        for (int i = 0; i < MAX_PROCESSES; i++) {
            process_t* p = &process_table[i];
            if (p->state == PROC_ZOMBIE && p->ppid == parent->pid) {
                int pid = p->pid;
                if (status) {
                    *status = p->exit_code;
                }
                process_destroy(pid);
                printf("process: wait collected zombie child %d\n", pid);
                return pid;
            }
        }

        // Check if parent has ANY children at all
        int has_children = 0;
        for (int i = 0; i < MAX_PROCESSES; i++) {
            process_t* p = &process_table[i];
            if (p->state != PROC_UNUSED && p->state != PROC_ZOMBIE && p->ppid == parent->pid) {
                has_children = 1;
                break;
            }
        }

        // No children at all - return error
        if (!has_children) {
            return -1;
        }

        // Has children but none are zombies - yield CPU
        // The timer interrupt will switch to another process
        // When we get scheduled again, we'll loop and check again
        __asm__ volatile("hlt"); // Wait for next interrupt
    }
}

void process_switch(int new_pid) {
    if (new_pid == current_pid) return;

    process_t* new_proc = process_find(new_pid);
    if (!new_proc || new_proc->state != PROC_READY) {
        printf("process: can't switch to pid=%d\n", new_pid);
        return;
    }

    process_t* old_proc = process_current();
    
    // Save old process context (would be saved from interrupt/syscall)
    if (old_proc && old_proc->state == PROC_RUNNING) {
        old_proc->state = PROC_READY;
    }

    // Switch to new process
    current_pid = new_pid;
    new_proc->state = PROC_RUNNING;

    // Switch page directory
    write_cr3(new_proc->page_dir);

    // Context would be restored by return from interrupt
}

/* Round-robin scheduler - pick next READY process */
void process_schedule(struct registers* regs) {
    process_t* current = process_current();
    if (!current) {
        return;
    }

    bool was_running = (current->state == PROC_RUNNING);

    if (was_running) {
        current->context.eax = regs->eax;
        current->context.ebx = regs->ebx;
        current->context.ecx = regs->ecx;
        current->context.edx = regs->edx;
        current->context.esi = regs->esi;
        current->context.edi = regs->edi;
        current->context.ebp = regs->ebp;
        current->context.esp = regs->useresp;
        current->context.eip = regs->eip;
        current->context.eflags = regs->eflags;
        current->context.cs = regs->cs;
        current->context.ss = regs->ss;
        current->context.ds = regs->ds;
        current->state = PROC_READY;
    }

    process_t* next = htas_pick_next_process(current);
    if (!next) {
        if (was_running) {
            current->state = PROC_RUNNING;
        }
        return;
    }

    if (next == current) {
        current->state = PROC_RUNNING;
        return;
    }

    if (next->state != PROC_READY && next->state != PROC_RUNNING) {
        if (was_running) {
            current->state = PROC_RUNNING;
        }
        return;
    }

    htas_record_switch(current, next);

    current_pid = next->pid;
    next->state = PROC_RUNNING;

    write_cr3(next->page_dir);

    regs->eax = next->context.eax;
    regs->ebx = next->context.ebx;
    regs->ecx = next->context.ecx;
    regs->edx = next->context.edx;
    regs->esi = next->context.esi;
    regs->edi = next->context.edi;
    regs->ebp = next->context.ebp;
    regs->useresp = next->context.esp;
    regs->eip = next->context.eip;
    regs->eflags = next->context.eflags;
    regs->cs = next->context.cs;
    regs->ss = next->context.ss;
    regs->ds = next->context.ds;
}
