/* forktest.c - Test fork() syscall */

extern int fork(void);
extern int getpid(void);
extern int getppid(void);
extern void exit(int code);
extern int write(int fd, const char* buf, unsigned len);
extern int wait(int* status);

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

void _start(void) {
    int shared_var = 100;
    
    print("Parent: My PID is ");
    print_num(getpid());
    print(", shared_var = ");
    print_num(shared_var);
    print("\n");
    
    print("Parent: Calling fork()...\n");
    int pid = fork();
    
    if (pid < 0) {
        print("Fork failed!\n");
        exit(1);
    } else if (pid == 0) {
        // Child process
        print("Child: I am the child! PID=");
        print_num(getpid());
        print(", PPID=");
        print_num(getppid());
        print(", shared_var=");
        print_num(shared_var);
        print("\n");
        
        // Modify the variable - should NOT affect parent if memory is copied!
        shared_var = 200;
        print("Child: Set shared_var to ");
        print_num(shared_var);
        print("\n");
        
        print("Child: Exiting with code 42\n");
        exit(42);
    } else {
        // Parent process
        print("Parent: fork() returned child PID=");
        print_num(pid);
        print("\n");
        
        // Small delay to let child run first
        for (volatile int i = 0; i < 1000000; i++);
        
        print("Parent: After fork, shared_var=");
        print_num(shared_var);
        print(" (should still be 100 if memory was copied)\n");
        
        print("Parent: Waiting for child (polling)...\n");
        int status;
        int waited = wait(&status);
        if (waited > 0) {
            print("Parent: Child ");
            print_num(waited);
            print(" exited with status ");
            print_num(status);
            print("\n");
        } else {
            print("Parent: wait() returned -1 (child may not be zombie yet)\n");
        }
        
        print("Parent: Done!\n");
        exit(0);
    }
}
