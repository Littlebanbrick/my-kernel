/* boot.S - Minimal boot sector for x86_64 in AT&T syntax
 *
 * This is the very first code the CPU executes after BIOS loads
 * the boot sector from disk into memory at address 0x7C00.
 *
 * Build:
 *   as --32 -o boot.o boot.S
 *   ld -m elf_i386 -N -o boot.elf -Ttext 0x7C00 boot.o
 *   objcopy -O binary boot.elf boot.bin
 *
 * The resulting boot.bin is exactly 512 bytes and can be run in QEMU:
 *   qemu-system-x86_64 -drive format=raw,file=boot.bin
 */

.code16                   /* CPU starts in 16-bit real mode (8086 mode) */
.section .text
.globl _start

_start:
    /* ---------------------------------------------------------------
     * Step 1: Set up the stack pointer.
     * The stack grows downward, so we point SP past our code region.
     * 0x7C00 is where we are loaded, so placing SP at 0x7C00 means
     * the stack safely grows down from above us.
     * ------------------------------------------------------------- */
    movw    $0x7C00, %sp

    /* ---------------------------------------------------------------
     * Step 2: Print a single character to the screen using BIOS.
     *
     * int $0x10 with AH=0x0E is the BIOS "teletype output" service.
     * It writes the character in AL at the cursor position, then
     * advances the cursor.  This works while we are still in real
     * mode and BIOS services are available.
     *
     * We print 'A' to prove the boot sector is running.
     * ------------------------------------------------------------- */
    movb    $0x0E, %ah      /* AH = 0x0E  -> "write char in teletype mode" */
    movb    $'A', %al       /* AL = 'A'   -> the character to print */
    int     $0x10           /* Call BIOS video interrupt service       */

    /* ---------------------------------------------------------------
     * Step 3: Halt the CPU forever.
     * After printing, there is nothing more to do — the boot sector's
     * job is done.  We loop forever to prevent the CPU from executing
     * garbage bytes beyond our code.
     * ------------------------------------------------------------- */
hang:
    jmp     hang

    /* ---------------------------------------------------------------
     * Step 4: Pad to 510 bytes and write the boot signature.
     *
     * BIOS checks that the last two bytes of the first sector are
     * 0x55 0xAA (little-endian: 0xAA55).  If they don't match,
     * BIOS assumes this disk is not bootable.
     *
     * .space fills bytes from the current position up to byte 510.
     * .word stores the 16-bit magic number at offset 510.
     * ------------------------------------------------------------- */
    .space  510 - (. - _start)
    .word   0xAA55
