/* simplefork.c - Simplest possible fork test */

int main(void) {
    // Use inline syscalls to avoid any library issues
    
    // Print starting message
    const char* msg1 = "Starting fork test...\n";
    unsigned len1 = 0;
    while (msg1[len1]) len1++;
    __asm__ volatile("int $0x80" :: "a"(1), "b"(msg1), "c"(len1));
    
    // Call fork
    int pid;
    __asm__ volatile("int $0x80" : "=a"(pid) : "a"(10));
    
    if (pid == 0) {
        // Child
        const char* msg = "CHILD process!\n";
        unsigned len = 0;
        while (msg[len]) len++;
        __asm__ volatile("int $0x80" :: "a"(1), "b"(msg), "c"(len));
        __asm__ volatile("int $0x80" :: "a"(2), "b"(99)); // exit(99)
    } else if (pid > 0) {
        // Parent
        const char* msg = "PARENT process!\n";
        unsigned len = 0;
        while (msg[len]) len++;
        __asm__ volatile("int $0x80" :: "a"(1), "b"(msg), "c"(len));
        __asm__ volatile("int $0x80" :: "a"(2), "b"(0)); // exit(0)
    } else {
        // Error
        const char* msg = "Fork FAILED!\n";
        unsigned len = 0;
        while (msg[len]) len++;
        __asm__ volatile("int $0x80" :: "a"(1), "b"(msg), "c"(len));
        __asm__ volatile("int $0x80" :: "a"(2), "b"(1)); // exit(1)
    }
    
    return 0;
}
