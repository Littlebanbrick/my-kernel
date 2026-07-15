/* boot.S - Stage 3: Load extra sectors, enter protected mode,
 *           then jump to long-mode setup code
 *
 * What this boot sector does:
 *   1. Prints "16" via BIOS (confirms real mode is working)
 *   2. Loads sectors 1+ from disk to 0x8000 using int 0x13
 *   3. Enters 32-bit protected mode (same GDT as stage 2)
 *   4. Jumps to stage3 code at 0xB000
 *
 * The extra sectors contain:
 *   - Page table data (PML4, PDP, PD)
 *   - Long-mode setup code
 *   - The C kernel
 *
 * Build & run:
 *   make && qemu-system-x86_64 -drive format=raw,file=build/disk.img
 */

.equ LOAD_SEG,     0x0000          /* ES = 0 for disk read buffer        */
.equ LOAD_OFF,     0x8000          /* Buffer = ES:0x8000                 */
.equ LOAD_SECTORS, 82              /* How many sectors to load           */
.equ STAGE3_ENTRY, 0xB000          /* Entry point in loaded payload      */

.code16
.section .text
.globl _start

_start:
    /* ================================================================
     *  Step 1 — Real-mode preamble
     *
     *  Set up stack and print "16" via BIOS to prove we're alive.
     * ============================================================== */

    movw    $0x7C00, %sp

    movb    $0x0E, %ah
    movb    $'1', %al
    int     $0x10
    movb    $'6', %al
    int     $0x10

    /* ================================================================
     *  Step 2 — Load extra sectors from disk using BIOS int 0x13
     *
     *  int 0x13, AH=0x02: read disk sectors into memory
     *    AL = number of sectors  (1-128, but safe < 64 per call)
     *    CH = cylinder (low 8 bits)
     *    CL[5:0] = sector number (1-indexed, max 63)
     *    CL[7:6] = cylinder high 2 bits
     *    DH = head number
     *    DL = drive number (set by BIOS, we preserve it)
     *    ES:BX = destination buffer
     *
     *  We read 60 sectors starting from sector 2 (sector 1 is ourselves)
     *  to ES:BX = 0x0000:0x8000.
     * ============================================================== */

    movw    $LOAD_SEG, %ax
    movw    %ax, %es               /* ES = 0x0000                        */
    movw    $LOAD_OFF, %bx         /* BX = 0x8000                        */

    movb    $LOAD_SECTORS, %al     /* AL = number of sectors             */
    movb    $0x02, %ah             /* AH = 0x02 (read)                   */
    movb    $0x00, %ch             /* CH = cylinder 0                    */
    movb    $0x02, %cl             /* CL = sector 2 (stage3.pad starts here) */
    movb    $0x00, %dh             /* DH = head 0                        */
    /* DL already contains the boot drive number from BIOS              */
    int     $0x13

    /* If carry flag is set, the disk read failed — hang                */
    jc      disk_error

    /* ================================================================
     *  Step 3 — Switch to 32-bit protected mode
     *
     *  Same GDT and sequence as stage 2:
     *    1. cli (interrupts off during switch)
     *    2. lgdt (load GDT pseudo-descriptor)
     *    3. CR0.PE = 1
     *    4. Far jump (flush prefetch, reload CS via GDT[1])
     * ============================================================== */

    xorw    %ax, %ax
    movw    %ax, %ds            /* DS = 0 (BIOS may have changed it)     */

    cli
    lgdt    gdt_desc

    movl    %cr0, %eax
    orl     $1, %eax            /* CR0 bit 0 = Protection Enable         */
    movl    %eax, %cr0

    /* Far jump to 32-bit protected mode
     *   In .code16: 0x66 overrides operand size to 32-bit offset        */
    .byte   0x66, 0xEA
    .long   protected_mode
    .word   0x08                /* CS = GDT index 1, RPL = 0             */

disk_error:
    jmp     disk_error          /* Infinite loop on error                */

/* ==================================================================== *
 *  32-bit Protected Mode                                               *
 * ==================================================================== */

.code32
protected_mode:
    /* --- Reload segment registers with flat data segment selector --- */

    movw    $0x10, %ax          /* 0x10 = GDT index 2, RPL 0             */
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss

    /* --- Jump to stage3 entry point at 0xB000 --- */

    movl    $STAGE3_ENTRY, %eax
    jmp     *%eax

    /* (never returns here) */

/* ==================================================================== *
 *  GDT — Global Descriptor Table (same as stage 2)                     *
 * ==================================================================== */

.align 8
gdt_start:
    .quad   0x0000000000000000         /* [0] Null descriptor             */
    .quad   0x00CF9A000000FFFF         /* [1] 32-bit code segment         */
    .quad   0x00CF92000000FFFF         /* [2] 32-bit data segment         */
gdt_end:

gdt_desc:
    .word   gdt_end - gdt_start - 1
    .long   gdt_start

/* Fill remaining space and write boot signature                        */
    .space  510 - (. - _start)
    .word   0xAA55
