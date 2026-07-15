/* prog.ld — linker script for a disk-loaded user program.
 *
 * The program is a single load segment at a fixed virtual address
 * (0x400000, the conventional "user" base, right after the identity-
 * mapped first 4 MiB).  objcopy -O binary then concatenates .text +
 * .rodata + .data into one flat blob; mkimage prepends the 16-byte
 * header sector.  No .bss — see hello.c. */

OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS
{
    . = 0x400000;

    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : { *(.rodata) *(.rodata.*) }
    .data   : { *(.data)   *(.data.*) }

    /* Intentionally no .bss — the format has no separate memsz. */

    _prog_end = .;
}
