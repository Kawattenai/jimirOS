#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/syscall.h>
#include <stdint.h>

extern int run_user_and_wait(void* entry, uint32_t user_stack_top);

void userdemo_run(void) {
    const uint32_t USTACK_BASE = 0x00400000u; /* 16 KiB stack */
    for (int i=0;i<4;i++) {
        uint32_t phys = pmm_alloc_frame();
        if (!phys) return;
        vmm_map(USTACK_BASE + i*4096, phys, PAGE_WRITE|PAGE_USER);
        uint8_t* p = (uint8_t*)(USTACK_BASE + i*4096);
        for (int j=0;j<4096;j++) p[j]=0;
    }
    const uint32_t UCODE_BASE = 0x00410000u;
    {
        uint32_t phys = pmm_alloc_frame();
        if (!phys) return;
        vmm_map(UCODE_BASE, phys, PAGE_WRITE|PAGE_USER);
        uint8_t* code = (uint8_t*)UCODE_BASE;
        const char* msg = "Hello from user mode via int 0x80!\n";
        uint32_t msg_off = 128;
        char* msgdst = (char*)(UCODE_BASE + msg_off);
        for (int i=0; msg[i]; ++i) msgdst[i] = msg[i];
        uint32_t msg_len = 0; while (msg[msg_len]) msg_len++;
        int k=0;
        code[k++]=0xB8; *(uint32_t*)(code+k)=SYS_write; k+=4;            /* mov eax, SYS_write */
        code[k++]=0xBB; *(uint32_t*)(code+k)=UCODE_BASE+msg_off; k+=4;   /* mov ebx, msg */
        code[k++]=0xB9; *(uint32_t*)(code+k)=msg_len; k+=4;              /* mov ecx, len */
        code[k++]=0xCD; code[k++]=0x80;                                  /* int 0x80 */
        code[k++]=0xB8; *(uint32_t*)(code+k)=SYS_exit; k+=4;             /* mov eax, SYS_exit */
        code[k++]=0x31; code[k++]=0xDB;                                  /* xor ebx, ebx */
        code[k++]=0xCD; code[k++]=0x80;                                  /* int 0x80 */
        code[k++]=0xF4;                                                  /* hlt */
    }
    (void)run_user_and_wait((void*)UCODE_BASE, USTACK_BASE + 4*4096);
}
