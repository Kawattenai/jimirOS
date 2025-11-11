#include <kernel/kdbg.h>
#include <kernel/stdio.h>
#include <kernel/keyboard.h>

static void dbg_help(void){
    printf("kdbg commands:\n");
    printf("  h,?      - help\n");
    printf("  q        - quit\n");
}

void kdbg_enter(void){
    printf("[kdbg] entered. 'q' to quit.\n> ");
    while (1){
        int ch = kbd_getch(); if (ch<0){ __asm__("hlt"); continue; }
        if (ch=='q' || ch=='Q'){ printf("\n[kdbg] exit\n"); return; }
        if (ch=='h' || ch=='?' ){ printf("\n"); dbg_help(); printf("> "); continue; }
        if (ch=='\r' || ch=='\n'){ printf("\n> "); continue; }
        printf("%c", ch);
    }
}
