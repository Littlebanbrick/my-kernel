/* linker.ld — Linker script for stage 3 payload
 *
 * Places page tables and code at known 4KB-aligned addresses
 * so that the boot sector can load everything to the correct
 * locations and the CPU can find the page tables via CR3.
 *
 * Memory layout:
 *   0x8000  PML4  (4 KB)
 *   0x9000  PDP   (4 KB)
 *   0xA000  PD    (4 KB)
 *   0xB000  .text (code)
 *   0xC000+ .data (read-only data)
 */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(stage3_32_entry)

SECTIONS
{
    . = 0x8000;

    .pml4 : ALIGN(0x1000)
    {
        *(.pml4)
    }

    .pdp : ALIGN(0x1000)
    {
        *(.pdp)
    }

    .pd : ALIGN(0x1000)
    {
        *(.pd)
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
}
