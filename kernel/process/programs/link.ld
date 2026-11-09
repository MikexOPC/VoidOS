ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x400000;
    .text : ALIGN(4096) { *(.text._start) *(.text .text.*) } :text
    . = ALIGN(4096);
    .rodata : ALIGN(4096) { *(.rodata .rodata.*) } :rodata
    . = ALIGN(4096);
    .data : ALIGN(4096) { *(.data .data.*) } :data
    .bss : { *(.bss .bss.*) } :data
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
