/* linker.ld — Linker script for 32-bit protected mode stage3
 *
 * Memory layout (must match boot.S load address + jump target):
 *   0x8000  .pad   — 0x3000 bytes of zeros
 *   0xB000  .text  — code (boot.S jumps here)
 *   0xC000+ .data / .bss
 */

OUTPUT_FORMAT(elf32-i386)
ENTRY(stage3_32_entry)

SECTIONS
{
    . = 0x8000;

    .pad : ALIGN(1)
    {
        . += 0x2FFF;
        BYTE(0);
    }

    .text : ALIGN(0x1000)
    {
        *(.text)
    }

    .data : ALIGN(0x1000)
    {
        *(.data)
    }

    .bss : ALIGN(0x1000)
    {
        *(.bss)
    }

    _end = .;
}
