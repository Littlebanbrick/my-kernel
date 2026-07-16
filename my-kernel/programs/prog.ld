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
        /* _start lives in .text.startup so the linker can pin it first
         * in the image — see hello.c.  With _start at the very load
         * address (0x400000), entry == load_addr holds and mkimage's
         * fixed entry stays correct even when other functions precede
         * it in the source. */
        *(.text.startup)
        *(.text)
        *(.text.*)
    }

    .rodata : { *(.rodata) *(.rodata.*) }
    .data   : { *(.data)   *(.data.*) }

    /* Intentionally no .bss — the format has no separate memsz. */

    _prog_end = .;
}
