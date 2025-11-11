/* syscalls.c - User-space syscall wrappers */

#define SYS_write 1
#define SYS_exit  2
#define SYS_read  3
#define SYS_open  4
#define SYS_close 5
#define SYS_sbrk  6
#define SYS_time  7
#define SYS_fs_list 8
#define SYS_fwrite 9
#define SYS_fork   10
#define SYS_wait   11
#define SYS_getpid 12
#define SYS_getppid 13

int write(int fd, const char* buf, unsigned len) {
    // Note: kernel SYS_write ignores fd and expects (buf, len) only
    // We pass fd in ebx but kernel only uses ebx and ecx
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_write), "b"(buf), "c"(len)
        : "memory"
    );
    return ret;
}

void exit(int code) {
    __asm__ volatile (
        "int $0x80"
        : 
        : "a"(SYS_exit), "b"(code)
        : "memory"
    );
    __builtin_unreachable();
}

int read(int fd, void* buf, unsigned len) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_read), "b"(fd), "c"(buf), "d"(len)
        : "memory"
    );
    return ret;
}

int open(const char* path) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_open), "b"(path)
        : "memory"
    );
    return ret;
}

int close(int fd) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_close), "b"(fd)
        : "memory"
    );
    return ret;
}

void* sbrk(int increment) {
    void* ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_sbrk), "b"(increment)
        : "memory"
    );
    return ret;
}

int fork(void) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_fork)
        : "ebx", "ecx", "edx", "esi", "edi", "memory"
    );
    return ret;
}

int wait(int* status) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_wait), "b"(status)
        : "memory"
    );
    return ret;
}

int getpid(void) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_getpid)
        : "memory"
    );
    return ret;
}

int getppid(void) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_getppid)
        : "memory"
    );
    return ret;
}
