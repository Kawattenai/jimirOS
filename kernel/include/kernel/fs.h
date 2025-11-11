#ifndef _KERNEL_FS_H
#define _KERNEL_FS_H

#include <stdint.h>

void fs_init(void);

/* Open a file by name; returns fd >= 0 or -1. */
int fs_open(const char* name);

/* Read up to len bytes from fd into buf; returns bytes read or -1. */
int fs_read(int fd, void* buf, unsigned len);
/* Write up to len bytes to fd from buf; returns bytes written or -1. */
int fs_write(int fd, const void* buf, unsigned len);

/* Close fd; returns 0 or -1. */
int fs_close(int fd);

/* List files (print to console). */
void fs_list_print(void);

/* Dump file names into buf separated by '\n'; returns bytes written. */
int fs_dump_list(char* buf, unsigned len);

#endif
