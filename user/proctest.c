/* proctest.c - Test basic process management without fork */

extern int getpid(void);
extern int getppid(void);
extern void exit(int code);
extern int write(int fd, const char* buf, unsigned len);

static void print(const char* s) {
    unsigned len = 0;
    while (s[len]) len++;
    write(1, s, len);
}

static void print_num(int n) {
    if (n == 0) {
        print("0");
        return;
    }
    if (n < 0) {
        print("-");
        n = -n;
    }
    char buf[16];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        char c = buf[--i];
        write(1, &c, 1);
    }
}

int main(void) {
    print("Process Management Test\n");
    print("=======================\n\n");
    
    print("My PID: ");
    print_num(getpid());
    print("\n");
    
    print("My PPID: ");
    print_num(getppid());
    print("\n");
    
    print("\nProcess info retrieved successfully!\n");
    print("Note: fork() requires scheduler integration (TODO)\n");
    
    return 0;
}
