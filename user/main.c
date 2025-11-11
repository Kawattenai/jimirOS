#define SYS_write 1
#define SYS_exit  2

static inline int sys_write(const char* s, unsigned n){
    int ret; __asm__ volatile ("int $0x80":"=a"(ret):"a"(SYS_write),"b"(s),"c"(n):"memory","cc"); return ret; }

void main(void){
    const char msg[] = "[user] hello from ELF userprog via int 0x80\n";
    sys_write(msg, sizeof(msg)-1);
}
