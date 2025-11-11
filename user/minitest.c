/* minitest.c - Absolute minimal test */

#define SYS_write 1
#define SYS_exit  2

static inline int sys_write(const char* s, unsigned n){
    int ret;
    __asm__ volatile ("int $0x80":"=a"(ret):"a"(SYS_write),"b"(s),"c"(n):"memory","cc");
    return ret;
}

static inline void sys_exit(int code){
    __asm__ volatile ("int $0x80"::"a"(SYS_exit),"b"(code):"memory","cc");
    __builtin_unreachable();
}

void main(void){
    const char msg[] = "Minimal test working!\n";
    sys_write(msg, sizeof(msg)-1);
    sys_exit(0);
}
