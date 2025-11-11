/* Higher-half kernel with low bootstrap and explicit LMA <-> VMA mapping */
ENTRY(_start)

KERNEL_VIRT = 0xC0100000;  /* where the kernel runs (virtual) */
KERNEL_PHYS = 0x00100000;  /* physical base for higher-half sections */
BOOT_PHYS   = 0x00008000;  /* low bootstrap physical base (64 KiB) */

SECTIONS
{
    /* Low (identity) bootstrap placed early in memory, separate from kernel phys */
    . = BOOT_PHYS;
    .boot ALIGN(4K) :
    {
        boot_start = .;
        KEEP(*(.multiboot))      /* Header must be within first 8 KiB */
        KEEP(*(.text.boot*))     /* Low entry and paging setup */
        /* Merge low .bss into the same PT_LOAD to avoid a separate zero-sized file segment */
        . = ALIGN(4K);
        KEEP(*(.bss.boot*))
        boot_end = .;
    }

    /* No separate .boot.bss output section: it's folded into .boot above */

    /* Switch to higher-half VMA; map high sections at physical KERNEL_PHYS */
    . = KERNEL_VIRT;

    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VIRT + KERNEL_PHYS)
    {
        *(.text*)
        *(.init)
        *(.fini)
        *(.rodata*)
        *(.eh_frame)
    }

    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VIRT + KERNEL_PHYS)
    {
        *(.data*)
        *(.ctors)
        *(.dtors)
    }

    /* BSS is not stored in the file image; no AT() needed */
    .bss (NOLOAD) : ALIGN(4K)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
    }

    /* End of kernel (virtual) */
    end = .;

    /* Export kernel physical range for low-level allocators */
    kernel_phys_start = KERNEL_PHYS;
    kernel_phys_end   = (end - KERNEL_VIRT + KERNEL_PHYS);

    /DISCARD/ :
    {
        *(.comment)
        *(.note.gnu.build-id)
    }
}