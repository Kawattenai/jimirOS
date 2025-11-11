#ifndef _KERNEL_SCHED_H
#define _KERNEL_SCHED_H

#include <stdint.h>

typedef void (*kthread_fn)(void*);

void sched_init(void);
int  kthread_create(kthread_fn fn, void* arg, const char* name);
void sched_yield(void);
void sched_tick(void); /* call from timer IRQ */
void sched_ps(void);

#endif
