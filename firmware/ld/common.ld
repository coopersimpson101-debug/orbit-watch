/* Section layout shared by both boards (appended after MEMORY by build.py). */
ENTRY(Reset_Handler)
_msp_size = 4K;

SECTIONS
{
    .vectors : ALIGN(2048) { KEEP(*(.vectors)) } > FLASH
    .text : { *(.text .text.*) *(.glue_7 .glue_7t) } > FLASH
    .rodata : { *(.rodata .rodata.*) . = ALIGN(4); } > FLASH
    .ARM.exidx : { *(.ARM.exidx*) } > FLASH
    _sidata = LOADADDR(.data);
    .data : ALIGN(4) { _sdata = .; *(.data .data.*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
    .bss (NOLOAD) : ALIGN(4) { _sbss = .; *(.bss .bss.* COMMON) . = ALIGN(4); _ebss = .; } > RAM
    .rtt (NOLOAD) : ALIGN(4) { *(.rtt) } > RAM
    __heap_start = ALIGN(1024);
    _estack = ORIGIN(RAM) + LENGTH(RAM);
    __msp_limit = _estack - _msp_size;
    __heap_end = __msp_limit;
    /DISCARD/ : { *(.comment) *(.ARM.attributes) }
}
