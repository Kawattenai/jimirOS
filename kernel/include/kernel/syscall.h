#ifndef _KERNEL_SYSCALL_H
#define _KERNEL_SYSCALL_H

#define SYS_write 1
#define SYS_exit  2
#define SYS_read  3
#define SYS_open  4
#define SYS_close 5
#define SYS_sbrk  6
#define SYS_time  7
#define SYS_fs_list 8
/* file write (fd>2). Writes up to len bytes to an open file. */
#define SYS_fwrite 9
#define SYS_fork   10
#define SYS_wait   11
#define SYS_getpid 12
#define SYS_getppid 13

#endif
