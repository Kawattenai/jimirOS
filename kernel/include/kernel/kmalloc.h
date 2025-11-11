#ifndef _KERNEL_KMALLOC_H
#define _KERNEL_KMALLOC_H

#include <stddef.h>
#include <stdint.h>

void kmalloc_init(void* heap_base, size_t heap_size);
void* kmalloc(size_t sz);
void* kcalloc(size_t n, size_t sz);
void* krealloc(void* p, size_t sz);
void  kfree(void* p);

#endif
