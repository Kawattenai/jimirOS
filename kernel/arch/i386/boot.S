/* -------------------------------------------------------------------
 * Multiboot header and paging bootstrap for a higher-half kernel
 * ------------------------------------------------------------------- */

/* Multiboot header (must be within the first 8 KiB of the file) */
.set ALIGN,    1<<0             /* align loaded modules on page boundaries */
.set MEMINFO,  1<<1             /* provide memory map */
.set FLAGS,    ALIGN | MEMINFO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot, "a"
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

/* Low, identity-mapped bootstrap data: page tables and temp stack */
.section .bss.boot, "aw", @nobits
.align 4096
page_directory:
	.space 4096
.align 4096
page_table0:
	.space 4096
.align 4096
page_table1:
	.space 4096
.align 4096
page_table2:
	.space 4096
.align 4096
page_table3:
	.space 4096

.align 16
boot_stack_bottom:
	.space 16384                    /* 16 KiB low stack */
boot_stack_top:

/* Save Multiboot registers (magic in EAX, info ptr in EBX) */
.align 4
mb_magic:
	.space 4
mb_info:
	.space 4

/* High (virtual) stack for the kernel proper */
.section .bss, "aw", @nobits
.align 16
stack_bottom:
	.space 16384
stack_top:

/* Low entry point: set up paging and jump to higher half */
.section .text.boot, "ax"
.global _start
.type _start, @function
_start:
	/* Use a known-good low stack first */
	movl $boot_stack_top, %esp

	/* Preserve multiboot magic and info pointer before clobbering registers */
	movl %eax, mb_magic
	movl %ebx, mb_info

	/* Zero the page directory */
	leal page_directory, %edi
	xorl %eax, %eax
	movl $1024, %ecx
	rep stosl

	/* Zero all four page tables (16MB identity mapping) */
	leal page_table0, %edi
	xorl %eax, %eax
	movl $4096, %ecx                 /* 4 tables * 1024 entries */
	rep stosl

	/* Fill PT0: identity map 0x00000000 - 0x003FFFFF (4MB) */
	movl $0, %eax
	leal page_table0, %edi
	movl $1024, %ecx
1:
	movl %eax, %ebx
	orl $0x3, %ebx                   /* present | writable */
	movl %ebx, (%edi)
	addl $4, %edi
	addl $0x1000, %eax
	loop 1b

	/* Fill PT1: identity map 0x00400000 - 0x007FFFFF (4-8MB) */
	leal page_table1, %edi
	movl $1024, %ecx
2:
	movl %eax, %ebx
	orl $0x3, %ebx
	movl %ebx, (%edi)
	addl $4, %edi
	addl $0x1000, %eax
	loop 2b

	/* Fill PT2: identity map 0x00800000 - 0x00BFFFFF (8-12MB) */
	leal page_table2, %edi
	movl $1024, %ecx
3:
	movl %eax, %ebx
	orl $0x3, %ebx
	movl %ebx, (%edi)
	addl $4, %edi
	addl $0x1000, %eax
	loop 3b

	/* Fill PT3: identity map 0x00C00000 - 0x00FFFFFF (12-16MB) */
	leal page_table3, %edi
	movl $1024, %ecx
4:
	movl %eax, %ebx
	orl $0x3, %ebx
	movl %ebx, (%edi)
	addl $4, %edi
	addl $0x1000, %eax
	loop 4b

	/* Set up page directory entries for identity mapping */
	leal page_directory, %edi
	
	/* PDE[0] -> PT0 */
	leal page_table0, %eax
	orl $0x3, %eax
	movl %eax, (%edi)
	
	/* PDE[1] -> PT1 */
	leal page_table1, %eax
	orl $0x3, %eax
	movl %eax, 4(%edi)
	
	/* PDE[2] -> PT2 */
	leal page_table2, %eax
	orl $0x3, %eax
	movl %eax, 8(%edi)
	
	/* PDE[3] -> PT3 */
	leal page_table3, %eax
	orl $0x3, %eax
	movl %eax, 12(%edi)

	/* PDE[0x300] -> PT0 (higher-half map at 0xC0000000) */
	leal page_directory, %edi
	movl $0x300, %ebx                /* 0xC0000000 / 4 MiB */
	shl $2, %ebx                     /* * sizeof(pde) */
	addl %ebx, %edi
	leal page_table0, %eax
	orl $0x3, %eax
	movl %eax, (%edi)

	/* Load CR3 with page directory base */
	leal page_directory, %eax
	movl %eax, %cr3

	/* Enable paging (set PG bit in CR0) */
	movl %cr0, %eax
	orl $0x80000000, %eax
	movl %eax, %cr0

	/* Jump to the higher-half entry to flush the pipeline */
	lea _start_high, %eax
	jmp *%eax

.size _start, . - _start

/* Higher-half entry: switch to high stack, call global ctors and kernel_main */
.section .text, "ax"
.global _start_high
.type _start_high, @function
_start_high:
	/* Switch to the high (virtual) kernel stack */
	movl $stack_top, %esp

	/* Skip calling global constructors for now to avoid relying on
	   toolchain crt init arrays before we have a console for debugging. */

	/* Reload preserved multiboot values and pass to kernel_main */
	movl mb_magic, %eax
	movl mb_info, %ebx
	pushl %ebx            /* multiboot_addr */
	pushl %eax            /* magic */
	call kernel_main

	/* Hang if kernel_main unexpectedly returns */
	cli
2:  hlt
	jmp 2b
.size _start_high, . - _start_high
