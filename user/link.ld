ENTRY(_start)
PHDRS
{
  text PT_LOAD FLAGS(5); /* R+X */
  data PT_LOAD FLAGS(6); /* R+W */
}
SECTIONS
{
  . = 0x00410000; /* user code base expected by loader */
  .text : ALIGN(4096) {
    *(.text*)
    *(.rodata*)
  } :text

  .data : ALIGN(4096) {
    *(.data*)
  } :data

  .bss : ALIGN(4096) {
    *(.bss*)
    *(COMMON)
  } :data
}
