#include <kernel/idt.h>
#include <kernel/syscall.h>
#include <kernel/serial.h>
#include <kernel/proc.h>
#include <kernel/stdio.h>
#include <kernel/keyboard.h>
#include <kernel/tty.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/process.h>

static int sys_write_impl(const char* buf, unsigned len) {
    /* Mirror userland stdout to BOTH serial and VGA so output is visible
       in the QEMU window and (optionally) on the host terminal. */
    for (unsigned i = 0; i < len; ++i) {
        char c = buf[i];
        serial_putchar(c);
        terminal_putchar(c);
    }
    return (int)len;
}

/* legacy exit impl removed; handled in dispatcher to return to kernel */

/* regs->eax = nr, ebx = arg1, ecx = arg2, edx = arg3 */
/* Forward to FS for file syscalls */
extern int fs_open(const char* name);
extern int fs_read(int fd, void* buf, unsigned len);
extern int fs_write(int fd, const void* buf, unsigned len);
extern int fs_close(int fd);
extern int fs_dump_list(char* buf, unsigned len);

void syscall_dispatch(struct registers* regs) {
    switch (regs->eax) {
        case SYS_write:
              /* Quiet default: avoid per-call spam so user shells are readable. */
              regs->eax = (uint32_t)sys_write_impl((const char*)regs->ebx, (unsigned)regs->ecx);
            break;
        case SYS_exit: {
            int code = (int)regs->ebx;
            printf("\n[usr] exit(%d)\n", code);
            /* Save exit code and arrange to return control at the ISR tail. */
            if (!proc_prepare_kernel_return(regs, code)) {
                printf("[sys_exit] ERROR: proc_prepare_kernel_return failed!\n");
                for(;;) { __asm__ volatile("cli; hlt"); }
            }
            /* Perform an immediate switch to kernel stack and resume point.
               This bypasses the ISR tail and avoids any iret to user. */
            extern void*    g_proc_resume_eip;
            extern uint32_t g_proc_resume_esp;
            extern uint32_t g_proc_resume_ebp;
            /* Debug instrumentation: */
            printf("[sys_exit] hard switch now: resume_esp=0x%x resume_ebp=0x%x resume_eip=%p\n", 
                   g_proc_resume_esp, g_proc_resume_ebp, g_proc_resume_eip);
            proc_switch_to_kernel_now();
            __builtin_unreachable();
        }
        case SYS_read: {
            int fd = (int)regs->ebx;
            void* buf = (void*)regs->ecx;
            unsigned len = (unsigned)regs->edx;
            if (fd == 0) {
                /* Read from keyboard into user buffer, echo to console. */
                char* dst = (char*)buf;
                unsigned n = 0;
                /* Allow IRQ1 (keyboard) to fire while we block for input.
                   The syscall ISR entry cleared IF (interrupt gate), so
                   explicitly re-enable it for the duration of this read. */
                __asm__ volatile("sti" ::: "memory");
                while (n < len) {
                    int ch = kbd_getch();
                    if (ch < 0) { __asm__ volatile("sti; hlt"); continue; }
                    if (ch == '\r') ch = '\n';
                    if (ch == '\b') {
                        if (n > 0) { n--; terminal_putchar('\b'); terminal_putchar(' '); terminal_putchar('\b'); }
                        continue;
                    }
                    dst[n++] = (char)ch;
                    terminal_putchar((char)ch);
                    if (ch == '\n') break;
                }
                regs->eax = (uint32_t)n;
            } else {
                regs->eax = (uint32_t)fs_read(fd, buf, len);
            }
            break; }
        case SYS_open:
            regs->eax = (uint32_t)fs_open((const char*)regs->ebx);
            break;
        case SYS_close:
            regs->eax = (uint32_t)fs_close((int)regs->ebx);
            break;
        case SYS_fwrite:
            regs->eax = (uint32_t)fs_write((int)regs->ebx, (const void*)regs->ecx, (unsigned)regs->edx);
            break;
        case SYS_sbrk: {
            /* Very simple user heap: grow from 0x00800000 upwards. arg=increment */
            static uint32_t brk_base = 0x00800000u;
            static uint32_t brk_cur  = 0x00800000u;
            int inc = (int)regs->ebx;
            uint32_t old = brk_cur;
            uint32_t new_brk = brk_cur + inc;
            if (inc > 0) {
                for (uint32_t a = (brk_cur + 0xFFFu) & ~0xFFFu; a < ((new_brk + 0xFFFu) & ~0xFFFu); a += 4096) {
                    uint32_t phys = pmm_alloc_frame();
                    if (!phys) break;
                    vmm_map(a, phys, PAGE_WRITE|PAGE_USER);
                }
            }
            brk_cur = new_brk;
            regs->eax = old;
            break; }
        case SYS_time: {
            extern uint64_t pit_ticks(void);
            extern uint32_t pit_hz(void);
            uint64_t t = pit_ticks();
            uint32_t hz = pit_hz();
            uint32_t secs = hz ? (uint32_t)(t / hz) : 0u;
            regs->eax = secs;
            break; }
        case SYS_fs_list:
            regs->eax = (uint32_t)fs_dump_list((char*)regs->ebx, (unsigned)regs->ecx);
            break;
        case SYS_fork: {
            // Save current process context from interrupt frame
            process_t* proc = process_current();
            if (proc) {
                // Update current process context with register state
                proc->context.eax = regs->eax;
                proc->context.ebx = regs->ebx;
                proc->context.ecx = regs->ecx;
                proc->context.edx = regs->edx;
                proc->context.esi = regs->esi;
                proc->context.edi = regs->edi;
                proc->context.ebp = regs->ebp;
                proc->context.esp = regs->useresp;
                proc->context.eip = regs->eip;
                proc->context.eflags = regs->eflags;
                proc->context.cs = regs->cs;
                proc->context.ss = regs->ss;
                proc->context.ds = regs->ds;
                // Set default segment values for es, fs, gs
                proc->context.es = 0x23;
                proc->context.fs = 0x23;
                proc->context.gs = 0x23;
            }
            int child_pid = process_fork();
            regs->eax = (uint32_t)child_pid;
            break;
        }
        case SYS_wait: {
            int* status = (int*)regs->ebx;
            int pid = process_wait(status);
            regs->eax = (uint32_t)pid;
            break;
        }
        case SYS_getpid: {
            // For now, just return a fake PID without process management
            regs->eax = 1; // Always return PID 1
            break;
        }
        case SYS_getppid: {
            // For now, just return a fake PPID without process management
            regs->eax = 0; // Always return PPID 0
            break;
        }
        default:
            printf("Unknown syscall: %u\n", regs->eax);
            regs->eax = (uint32_t)-1;
    }
}
