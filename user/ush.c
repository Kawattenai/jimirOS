#define SYS_write 1
#define SYS_exit  2
#define SYS_read  3
#define SYS_open  4
#define SYS_close 5
#define SYS_sbrk  6
#define SYS_time  7
#define SYS_fs_list 8
#define SYS_fwrite  9

static inline int sys_write(const char* s, unsigned n){ int r; __asm__ volatile("int $0x80":"=a"(r):"a"(SYS_write),"b"(s),"c"(n):"memory","cc"); return r; }
static inline int sys_exit(int code){ int r; __asm__ volatile("int $0x80":"=a"(r):"a"(SYS_exit),"b"(code):"memory","cc"); return r; }
static inline int sys_read(int fd, void* buf, unsigned n){ int r; __asm__ volatile("int $0x80":"=a"(r):"a"(SYS_read),"b"(fd),"c"(buf),"d"(n):"memory","cc"); return r; }
static inline int sys_open(const char* name){ int r; __asm__ volatile("int $0x80":"=a"(r):"a"(SYS_open),"b"(name):"memory","cc"); return r; }
static inline int sys_close(int fd){ int r; __asm__ volatile("int $0x80":"=a"(r):"a"(SYS_close),"b"(fd):"memory","cc"); return r; }
static inline int sys_fs_list(char* buf, unsigned n){ int r; __asm__ volatile("int $0x80":"=a"(r):"a"(SYS_fs_list),"b"(buf),"c"(n):"memory","cc"); return r; }
static inline int sys_fwrite(int fd, const void* buf, unsigned n){ int r; __asm__ volatile("int $0x80":"=a"(r):"a"(SYS_fwrite),"b"(fd),"c"(buf),"d"(n):"memory","cc"); return r; }

static unsigned strlen(const char* s){ unsigned n=0; while(s[n]) n++; return n; }
static void puts(const char* s){ sys_write(s, strlen(s)); }
static int streq(const char* a, const char* b){ while(*a && (*a==*b)){a++;b++;} return (unsigned char)*a - (unsigned char)*b; }

static void cmd_ls(void){
    char buf[512]; int n = sys_fs_list(buf, sizeof(buf)-1); if (n<0) n=0; buf[n]=0; sys_write(buf, n);
}

static void cmd_cat(const char* name){
    if (!name||!*name){ puts("usage: cat NAME\n"); return; }
    int fd = sys_open(name);
    if (fd < 0) { puts("cat: not found\n"); return; }
    char buf[256];
    while (1) { int n = sys_read(fd, buf, sizeof(buf)); if (n <= 0) break; sys_write(buf, (unsigned)n); }
    sys_close(fd);
    puts("\n");
}

static void cmd_write(const char* name){
    if (!name||!*name){ puts("usage: write NAME (type a line)\n"); return; }
    int fd = sys_open(name);
    if (fd < 0) { puts("open failed\n"); return; }
    char line[128]; int n = sys_read(0, line, sizeof(line)-1); if (n>0){ sys_fwrite(fd, line, (unsigned)n); }
    sys_close(fd);
}

void main(void){
    puts("ush: tiny user shell. Commands: ls, cat NAME, write NAME, exit\n");
    char line[128];
    for(;;){
        puts("u$ ");
        int n = sys_read(0, line, sizeof(line)-1);
        if (n <= 0) continue;
        line[n]=0;
        /* strip CR/LF */
        while (n>0 && (line[n-1]=='\n' || line[n-1]=='\r')) { line[--n]=0; }
        const char* p = line; while (*p==' ') p++;
        const char* cmd = p; while (*p && *p!=' ') p++; int has_arg = 0; if (*p){ *(char*)p++=0; while(*p==' ') p++; has_arg=1; }
        if (!*cmd) continue;
        if (streq(cmd,"exit")==0) { sys_exit(0); }
        else if (streq(cmd,"ls")==0) { cmd_ls(); }
        else if (streq(cmd,"cat")==0) { cmd_cat(has_arg?p:0); }
        else if (streq(cmd,"write")==0) { cmd_write(has_arg?p:0); }
        else { puts("unknown. try ls/cat/exit\n"); }
    }
}
