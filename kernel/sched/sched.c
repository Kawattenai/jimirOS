#include <kernel/sched.h>
#include <kernel/kmalloc.h>
#include <kernel/stdio.h>
#include <string.h>

#define MAX_THREADS 16
#define STACK_SIZE  (8*1024)

typedef enum { T_UNUSED=0, T_READY, T_RUNNING, T_BLOCKED } tstate_t;

struct kthread {
    uint32_t esp;
    tstate_t state;
    char     name[16];
};

static struct kthread th[MAX_THREADS];
static int current = -1;

extern void ctx_switch(uint32_t* old_esp, uint32_t new_esp);

static void kthread_trampoline(void);

struct start_pack { kthread_fn fn; void* arg; };

static uint32_t new_stack_with_trampoline(kthread_fn fn, void* arg){
    uint8_t* stk = (uint8_t*)kmalloc(STACK_SIZE);
    if (!stk) return 0;
    memset(stk, 0, STACK_SIZE);
    uint32_t* sp = (uint32_t*)(stk + STACK_SIZE);
    /* push start args for trampoline */
    struct start_pack* pack = (struct start_pack*)(sp - (sizeof(struct start_pack)/4));
    pack->fn = fn; pack->arg = arg;
    sp = (uint32_t*)pack;
    /* simulate call frame return address (unused) */
    *(--sp) = 0;
    /* entry EIP for switch return path */
    *(--sp) = (uint32_t)(uintptr_t)&kthread_trampoline;
    /* minimal callee-saved registers for ctx switch compatibility */
    /* reserve pusha frame: edi,esi,ebp,esp,ebx,edx,ecx,eax if needed by asm */
    for (int i=0;i<8;i++) *(--sp) = 0;
    return (uint32_t)(uintptr_t)sp;
}

void sched_init(void){
    memset(th, 0, sizeof(th));
    /* slot 0 is the bootstrap thread (current CPU context) */
    th[0].state = T_RUNNING; current = 0; th[0].name[0]='i'; th[0].name[1]='d'; th[0].name[2]='l'; th[0].name[3]='e'; th[0].name[4]='\0';
}

int kthread_create(kthread_fn fn, void* arg, const char* name){
    for (int i=1;i<MAX_THREADS;i++){
        if (th[i].state == T_UNUSED){
            uint32_t esp = new_stack_with_trampoline(fn,arg);
            if (!esp) return -1;
            th[i].esp = esp;
            th[i].state = T_READY;
            int j=0; if (name){ while (name[j] && j<15){ th[i].name[j]=name[j]; j++; } }
            th[i].name[j]=0;
            return i;
        }
    }
    return -1;
}

void sched_ps(void){
    printf("PID  STATE     NAME\n");
    for (int i=0;i<MAX_THREADS;i++){
        if (th[i].state!=T_UNUSED){
            const char* st = (th[i].state==T_RUNNING)?"RUNNING":(th[i].state==T_READY?"READY":"BLOCKED");
            printf("%2d   %-8s %s%s\n", i, st, th[i].name, (i==current)?" *":"");
        }
    }
}

static int rr_next(int from){
    for (int k=1;k<=MAX_THREADS;k++){
        int i = (from + k) % MAX_THREADS;
        if (th[i].state == T_READY) return i;
    }
    return from;
}

void sched_yield(void){
    int next = rr_next(current);
    if (next == current) return;
    int prev = current; current = next;
    th[prev].state = T_READY;
    th[next].state = T_RUNNING;
    ctx_switch(&th[prev].esp, th[next].esp);
}

void sched_tick(void){
    /* simple round-robin every tick */
    sched_yield();
}

/* Runs on a fresh stack for the new thread */
static void kthread_trampoline(void){
    struct start_pack pack;
    /* stack layout: [pusha..], ret, pack */
    uint32_t* sp; __asm__ volatile ("movl %%esp,%0":"=r"(sp));
    memcpy(&pack, (void*)(sp + 1), sizeof(pack));
    pack.fn(pack.arg);
    /* If function returns, just park */
    for(;;) { __asm__ volatile("hlt"); }
}
