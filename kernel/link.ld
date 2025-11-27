OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kernel_entry)

SECTIONS
{
    . = 0x0000000000100000; /* 1 MiB */

    .text : ALIGN(0x1000)
    {
        *(.text .text.*)
    }

    .rodata : ALIGN(0x1000)
    {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(0x1000)
    {
        *(.data .data.*)
    }

    .bss : ALIGN(0x1000)
    {
        *(COMMON)
        *(.bss .bss.*)
    }
}
