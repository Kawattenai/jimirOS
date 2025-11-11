.global _start
.type _start, @function
_start:
    call main
    /* on return, exit(0) */
    xorl %ebx, %ebx
    movl $2, %eax   /* SYS_exit */
    int $0x80
    hlt
