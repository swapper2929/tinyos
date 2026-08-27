/* linker.ld - Linker script for x86 kernel */
ENTRY(_start)

SECTIONS
{
    /* Kernel loaded at 1MB by Multiboot */
    . = 0x100000;

    .text : {
        *(.text)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
        *(COMMON)
    }

    /* Ensure 4KB alignment for sections */
    . = ALIGN(4096);
}
