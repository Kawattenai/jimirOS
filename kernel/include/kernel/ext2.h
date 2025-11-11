#ifndef _KERNEL_EXT2_H
#define _KERNEL_EXT2_H
#include <stdint.h>

/* Minimal, read-only ext2 interface used by fs.c */

int  ext2_mount_from_module(const void* start, uint32_t size);
int  ext2_mount_from_disk(void);
int  ext2_is_mounted(void);
int  ext2_list(char* buf, unsigned len);
int  ext2_open(const char* path);
int  ext2_read(int fd, void* buf, unsigned len);
int  ext2_write(int fd, const void* buf, unsigned len);
int  ext2_close(int fd);

#endif
