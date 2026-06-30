/* boot.S - Stage 2: Enter 32-bit protected mode
 *
 * What this boot sector does:
 *   1. Prints "16" via BIOS (confirms real mode is working)
 *   2. Builds a minimal GDT, loads it, switches to 32-bit protected mode
 *   3. Writes "32" directly to VGA memory at 0xB8000 (top-left of screen)
 *      — no BIOS needed, we control the hardware directly
 *   4. Halts forever
 *
 * The screen should show "16" at the cursor position (BIOS teletype)
 * AND "32" at the top-left corner (our direct VGA write).
 *
 * Build & run:
 *   make && qemu-system-x86_64 -drive format=raw,file=build/boot.bin
 */

.code16
.section .text
.globl _start

_start:
    /* ================================================================
     *  Step 1 — Real-mode preamble
     *
     *  CPU starts in 16-bit real mode after BIOS loads this 512-byte
     *  sector to address 0x7C00.  We initialise the stack and print
     *  "16" via the BIOS teletype service to prove we are alive.
     * ============================================================== */

    movw    $0x7C00, %sp        /* stack grows down from above our code */

    movb    $0x0E, %ah          /* BIOS int 0x10, AH=0x0E: teletype out  */
    movb    $'1', %al
    int     $0x10               /* print '1' — confirms real mode works  */
    movb    $'6', %al
    int     $0x10               /* print '6' — screen shows "16"         */

    /* ================================================================
     *  Step 2 — Switch to 32-bit protected mode
     *
     *  The GDT defines segment descriptors that the CPU uses to check
     *  access permissions.  Our minimal table has 3 entries:
     *    [0] null descriptor (required by Intel)
     *    [1] code segment (ring 0, 32-bit, base 0, limit 4 GB)
     *    [2] data segment (ring 0, writable, base 0, limit 4 GB)
     *
     *  Sequence:
     *    1. DS = 0     — lgdt reads memory using DS, and BIOS may have
     *                    changed it during int $0x10.  We must reset it.
     *    2. cli        — interrupts must be OFF during the switch,
     *                    because the real-mode interrupt table (IVT) is
     *                    no longer valid but the protected-mode IDT is
     *                    not yet set up.  An interrupt -> triple fault.
     *    3. lgdt       — load GDT pseudo-descriptor (6 bytes) into GDTR
     *    4. CR0.PE = 1 — set Protection Enable bit; CPU is now in
     *                    protected mode but the instruction pipeline
     *                    still sees 16-bit decode state
     *    5. Far jump   — reload CS from GDT[1] and flush the pipeline
     * ============================================================== */

    xorw    %ax, %ax
    movw    %ax, %ds            /* DS = 0 (BIOS may have changed it)     */

    cli
    lgdt    gdt_desc            /* load GDT table location & size        */

    movl    %cr0, %eax
    orl     $1, %eax            /* set CR0 bit 0 (Protection Enable)     */
    movl    %eax, %cr0

    /*
     * Manual far jump encoding in .code16 mode:
     *   0x66 — operand-size override (expect 32-bit offset)
     *   0xEA — far jump opcode
     *   followed by 32-bit target address and 16-bit CS selector
     *
     * Without 0x66 the CPU would treat the offset as only 16 bits
     * and jump to the wrong address.
     */
    .byte   0x66, 0xEA
    .long   protected_mode      /* 32-bit jump target                    */
    .word   0x08                /* CS = GDT index 1, RPL = ring 0       */

/* ==================================================================== *
 *  32-bit Protected Mode                                               *
 *  CPU now decodes instructions as 32-bit, and address translation     *
 *  uses the segment descriptors we defined in the GDT.                  *
 * ==================================================================== */

.code32
protected_mode:
    /* ----------------------------------------------------------------
     *  Step 3 — Set up data segment registers
     *
     *  DS, ES, FS, GS, SS must all point to our flat data segment
     *  (selector 0x10 = GDT index 2, base 0, limit 4 GB).
     *  Until we reload them, they still hold stale real-mode values
     *  that would cause the wrong linear address to be computed.
     * -------------------------------------------------------------- */

    movw    $0x10, %ax          /* 0x10 = index 2, TI=GDT, RPL=0        */
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss

    /* ----------------------------------------------------------------
     *  Step 4 — Write to VGA text-mode framebuffer
     *
     *  In protected mode, the data segment covers the entire 4 GB
     *  address space starting at physical address 0.  Whatever 32-bit
     *  address we write in code maps directly to the same physical
     *  address — this is the "flat memory model".
     *
     *  VGA memory starts at physical address 0xB8000.
     *  One character cell = 2 bytes: ASCII code + attribute byte.
     *  Attribute 0x07 = light-grey text on black background.
     *
     *  We write "32" to the TOP-LEFT corner of the screen (columns 0-1),
     *  NOT at the cursor position where "16" appears.
     * -------------------------------------------------------------- */

    movl $0xB8000, %edi
    movl $2000, %ecx
    movw $0x0720, %ax
    rep stosw

    movl    $0xB8000, %edi      /* VGA framebuffer base address          */
    movw    $0x0733, (%edi)     /* write '3' (0x33), attribute white/black */
    movw    $0x0732, 2(%edi)    /* write '2' (0x32) at the next column   */

    /* ----------------------------------------------------------------
     *  Step 5 — Halt forever
     * -------------------------------------------------------------- */

hang32:
    jmp     hang32

/* ==================================================================== *
 *  GDT — Global Descriptor Table                                        *
 *                                                                      *
 *  Each entry is exactly 8 bytes, describing one memory segment's       *
 *  base address, limit, and access permissions.                        *
 *                                                                      *
 *  GDT layout in the binary (starting at gdt_start = 0x7C50):          *
 *    0x7C50  [0] null descriptor          (8 zero bytes)               *
 *    0x7C58  [1] 32-bit code segment      0x00CF9A000000FFFF            *
 *    0x7C60  [2] 32-bit data segment      0x00CF92000000FFFF            *
 *                                                                      *
 *  Access byte (0x9A = code):  Present | DPL0 | S=1 | Code | Readable  *
 *  Access byte (0x92 = data):  Present | DPL0 | S=1 | Data | Writable  *
 *  Flags  (0xCF):  Gr=1 (4 KB pages) | Sz=1 (32-bit) | L=0             *
 * ==================================================================== */

.align 8
gdt_start:
    .quad   0x0000000000000000         /* [0] Null descriptor (required) */

    /* [1] 32-bit code segment — base 0, limit 4 GB, ring 0, readable   */
    .quad   0x00CF9A000000FFFF

    /* [2] 32-bit data segment — base 0, limit 4 GB, ring 0, writable   */
    .quad   0x00CF92000000FFFF
gdt_end:

/* Pseudo-descriptor for the lgdt instruction:                          *
 *   .word  = (GDT size - 1) — the CPU uses this value as the limit    *
 *   .long  = linear (physical) address of the very first GDT byte     */
gdt_desc:
    .word   gdt_end - gdt_start - 1
    .long   gdt_start

/* Fill to exactly 512 bytes and write the boot signature               */
    .space  510 - (. - _start)
    .word   0xAA55
