/* stage3.S — Page tables, long-mode setup, and 64-bit entry
 *
 * This code is loaded by the boot sector to address 0x8000.
 * The linker script places each section at a known 4KB-aligned address:
 *
 *   0x8000  .pml4  — Page Map Level 4 table  (4 KB, 512 × 8 bytes)
 *   0x9000  .pdp   — Page Directory Pointer   (4 KB)
 *   0xA000  .pd    — Page Directory           (4 KB, 2 MB huge-pages)
 *   0xB000  .text  — 32-bit and 64-bit code
 *   0xC000+ .data  — GDT and other read-only data
 *
 * Flow:
 *   1. Called from boot.S in 32-bit protected mode
 *   2. Initialises identity-mapped 2 MB pages for the first 4 MB
 *   3. Enables PAE → loads CR3 → sets EFER.LME → sets CR0.PG
 *   4. Loads a 64-bit-capable GDT and far-jumps into 64-bit mode
 *   5. Calls the C function kernel_main()
 */

/* ====================================================================
 *  PML4 — Page Map Level 4
 *  ================================================================== */

.section .pml4, "aw", @progbits
.align 4096
.globl pml4
pml4:
    .space 4096

/* ====================================================================
 *  PDP — Page Directory Pointer Table
 *  ================================================================== */

.section .pdp, "aw", @progbits
.align 4096
.globl pdp
pdp:
    .space 4096

/* ====================================================================
 *  PD — Page Directory (2 MB huge-page entries)
 *  ================================================================== */

.section .pd, "aw", @progbits
.align 4096
.globl pd
pd:
    .space 4096

/* ====================================================================
 *  32-bit entry — set up paging and enter long mode
 *  ================================================================== */

.section .text
.code32
.globl stage3_32_entry
stage3_32_entry:

    /* ----------------------------------------------------------------
     *  Step 1 — Identity-map the first 4 MB with 2 MB huge-pages
     *
     *  Physical layout of page tables (we write the entries now):
     *    0x8000  PML4[0] = 0x9003       → PDP at 0x9000 | P | R/W
     *    0x9000  PDP[0]  = 0xA003       → PD  at 0xA000 | P | R/W
     *    0xA000  PD[0]   = 0x000083     → 0x000000-0x1FFFFF | P | R/W | PS
     *    0xA008  PD[1]   = 0x200083     → 0x200000-0x3FFFFF | P | R/W | PS
     *
     *  We write both the low and high 32 bits of each 64-bit page-table
     *  entry.  The high dword is zero because all addresses are < 4 GB.
     * -------------------------------------------------------------- */

    /* PML4[0]: points to PDP table at 0x9000 */
    movl    $0x9003, 0x8000       /* low  = 0x9000 | P(1) | R/W(1) = 0x9003 */
    movl    $0x0000, 0x8004       /* high = 0                                 */

    /* PDP[0]: points to PD table at 0xA000 */
    movl    $0xA003, 0x9000       /* low  = 0xA000 | P | R/W                  */
    movl    $0x0000, 0x9004       /* high = 0                                 */

    /* PD[0]: 2 MB page covering physical 0x000000-0x1FFFFF */
    movl    $0x000083, 0xA000     /* low  = 0x000000 | P | R/W | PS(1)       */
    movl    $0x0000, 0xA004       /* high = 0                                 */

    /* PD[1]: 2 MB page covering physical 0x200000-0x3FFFFF */
    movl    $0x200083, 0xA008     /* low  = 0x200000 | P | R/W | PS           */
    movl    $0x0000, 0xA00C       /* high = 0                                 */

    /* ----------------------------------------------------------------
     *  Step 2 — Enable PAE (Physical Address Extension)
     *
     *  CR4.PAE = 1 is mandatory before entering long mode.
     * -------------------------------------------------------------- */

    movl    %cr4, %eax
    orl     $(1 << 5), %eax       /* PAE is bit 5                             */
    movl    %eax, %cr4

    /* ----------------------------------------------------------------
     *  Step 3 — Load CR3 with PML4 base address
     *
     *  CR3 expects the physical address of the top-level page table.
     *  Our PML4 is at 0x8000.
     * -------------------------------------------------------------- */

    movl    $0x8000, %eax
    movl    %eax, %cr3

    /* ----------------------------------------------------------------
     *  Step 4 — Enable Long Mode in the EFER MSR
     *
     *  MSR 0xC0000080 (EFER), bit 10 (LME).
     *  rdmsr loads EDX:EAX from the MSR indexed by ECX.
     *  wrmsr writes EDX:EAX back.
     * -------------------------------------------------------------- */

    movl    $0xC0000080, %ecx
    rdmsr
    orl     $(1 << 10), %eax      /* LME = bit 10                             */
    wrmsr

    /* ----------------------------------------------------------------
     *  Step 5 — Enable Paging (this transitions to long mode)
     *
     *  CR0.PG = 1.
     *  Because PAE=1, LME=1, and CR3 points to valid page tables,
     *  the CPU enters long mode.  CS is still in 32-bit compat mode
     *  until we reload it via a far jump.
     * -------------------------------------------------------------- */

    movl    %cr0, %eax
    orl     $(1 << 31), %eax      /* PG is bit 31                             */
    movl    %eax, %cr0

    /* ----------------------------------------------------------------
     *  Step 6 — Load a 64-bit-capable GDT
     *
     *  The current GDT (loaded by boot.S) has a 32-bit code segment.
     *  We need a GDT with the L bit set for the 64-bit code descriptor.
     * -------------------------------------------------------------- */

    lgdt    gdt64_desc

    /* ----------------------------------------------------------------
     *  Step 7 — Far jump into 64-bit mode
     *
     *  ljmp $selector, $address reloads CS from the GDT.
     *  The selector 0x08 points to GDT[1], which now has L=1,
     *  so the CPU switches to full 64-bit long mode.
     *
     *  In .code32, ljmp generates the correct 7-byte encoding:
     *    0xEA + 32-bit offset + 16-bit selector
     * -------------------------------------------------------------- */

    ljmp    $0x08, $stage3_64_entry

/* ====================================================================
 *  64-bit entry — set up segments and call the C kernel
 *  ================================================================== */

.code64
stage3_64_entry:

    /* --- Reload data segments with flat 64-bit data selector --- */
    movw    $0x10, %ax            /* 0x10 = GDT index 2                    */
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss

    /* --- Set up stack (grows down from 0x7000) --- */
    movq    $0x7000, %rsp

    /* --- Clear direction flag (System V AMD64 ABI requirement) --- */
    cld

    /* --- Call the C entry point --- */
    call    kernel_main

    /* --- Halt if C code returns --- */
    cli
    hlt
    jmp     .

/* ====================================================================
 *  64-bit GDT
 *  ================================================================== */

.section .data
.align 8

gdt64_start:
    .quad   0x0000000000000000         /* [0] Null descriptor                */
    .quad   0x00209A0000000000         /* [1] 64-bit code: P|DPL0|S|Code|R|L */
    .quad   0x0000920000000000         /* [2] 64-bit data: P|DPL0|S|Data|W   */
gdt64_end:

gdt64_desc:
    .word   gdt64_end - gdt64_start - 1
    .long   gdt64_start
    .word   0                         /* padding (reserved)                 */
