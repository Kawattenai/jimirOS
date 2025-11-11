.global _init
.type _init, @function
_init:
	push %ebp
	movl %esp, %ebp

.global _fini
.type _fini, @function
_fini:
	push %ebp
	movl %esp, %ebp