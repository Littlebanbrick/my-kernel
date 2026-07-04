/* kernel.c — Test kernel_main with printf + IDT + PIC (32-bit) */

#include "types.h"
#include "vga.h"
#include "printf.h"
#include "idt.h"
#include "pic.h"
#include "utils.h"
#include "memory.h"
#include "paging.h"

/* Tick counter — incremented by PIT IRQ 0 handler in idt.c */
volatile u32 g_ticks;

/* Write a decimal number directly to VGA memory at a fixed position.
 * Safe to call from any context — no shared state, no printf. */
static void vga_write_dec_at(int row, int col, u32 val)
{
	u16 *const vga = (u16 *)0xB8000;
	char buf[10];
	int i = 0;
	int pos = row * MAX_WIDTH + col;
	int j;

	do {
		buf[i++] = '0' + (val % 10);
		val /= 10;
	} while (val > 0);

	/* Pad to 6 characters (enough for our test run) */
	for (j = 0; j < 6 - i; j++)
		vga[pos++] = 0x0700 | ' ';

	/* Write digits in correct order */
	for (j = i - 1; j >= 0; j--)
		vga[pos++] = 0x0700 | buf[j];
}

/* ====================================================================
 *  Ring 3 structures and helpers
 * ==================================================================== */

/* 32-bit GDT descriptor (8 bytes) */
struct gdt_entry {
	u16 limit_low;
	u16 base_low;
	u8  base_mid;
	u8  access;
	u8  granularity;
	u8  base_high;
} __attribute__((packed));

/* GDT pointer (6 bytes, passed to lgdt) */
struct gdt_ptr {
	u16 limit;
	u32 base;
} __attribute__((packed));

/* 32-bit Task State Segment (104 bytes minimum) */
struct tss {
	u32 prev_tss;		/* 0x00 */
	u32 esp0;		/* 0x04 */
	u32 ss0;		/* 0x08 */
	u32 esp1;		/* 0x0C */
	u32 ss1;		/* 0x10 */
	u32 esp2;		/* 0x14 */
	u32 ss2;		/* 0x18 */
	u32 cr3;		/* 0x1C */
	u32 eip;		/* 0x20 */
	u32 eflags;		/* 0x24 */
	u32 eax;		/* 0x28 */
	u32 ecx;		/* 0x2C */
	u32 edx;		/* 0x30 */
	u32 ebx;		/* 0x34 */
	u32 esp;		/* 0x38 */
	u32 ebp;		/* 0x3C */
	u32 esi;		/* 0x40 */
	u32 edi;		/* 0x44 */
	u32 es;			/* 0x48 */
	u32 cs;			/* 0x4C */
	u32 ss;			/* 0x50 */
	u32 ds;			/* 0x54 */
	u32 fs;			/* 0x58 */
	u32 gs;			/* 0x5C */
	u32 ldt;		/* 0x60 */
	u16 trap;
	u16 iomap_base;
} __attribute__((packed));

/* Big static buffers (.bss, zero-initialised at boot) */
static struct gdt_entry ring3_gdt[6];
static struct tss    ring3_tss;
static u8             ring3_page[4096] __attribute__((aligned(4096)));

/* Jump to ring 3 via iret — never returns */
__attribute__((noreturn)) static void jump_to_ring3(u32 eip, u32 stack_top)
{
	u32 frame[5];		/* EIP, CS, EFLAGS, ESP, SS — iret order */

	frame[0] = eip;
	frame[1] = 0x1B;	/* CS = user code sel (0x18) | RPL 3 */

	asm volatile("pushf; pop %0" : "=r"(frame[2]) : : "memory");
	frame[2] |= 0x200;	/* IF = 1 */

	frame[3] = stack_top;
	frame[4] = 0x23;	/* SS = user data sel (0x20) | RPL 3 */

	asm volatile(
		"cli\n"
		"mov %0, %%esp\n"
		"iret\n"
		:
		: "r"(&frame[0])
		: "memory"
	);

	while (1)
		asm("hlt");
}

/* Set up GDT + TSS + page tables, then enter ring 3 */
static void setup_ring3(void)
{
	u32 *pt;
	u32  pdx, ptx;
	int  i;

	printf("ring3: setting up GDT and TSS...\n");

	/* ---- GDT entries ----
	 *   [0] null        sel 0x00
	 *   [1] kernel code sel 0x08
	 *   [2] kernel data sel 0x10
	 *   [3] user code   sel 0x18  (0x1B with RPL 3)
	 *   [4] user data   sel 0x20  (0x23 with RPL 3)
	 *   [5] TSS         sel 0x28
	 */
	ring3_gdt[1].limit_low   = 0xFFFF;
	ring3_gdt[1].access      = 0x9A;	/* P=1, DPL=0, code, R */
	ring3_gdt[1].granularity = 0xCF;

	ring3_gdt[2].limit_low   = 0xFFFF;
	ring3_gdt[2].access      = 0x92;	/* P=1, DPL=0, data, W */
	ring3_gdt[2].granularity = 0xCF;

	ring3_gdt[3].limit_low   = 0xFFFF;
	ring3_gdt[3].access      = 0xFA;	/* P=1, DPL=3, code, R */
	ring3_gdt[3].granularity = 0xCF;

	ring3_gdt[4].limit_low   = 0xFFFF;
	ring3_gdt[4].access      = 0xF2;	/* P=1, DPL=3, data, W */
	ring3_gdt[4].granularity = 0xCF;

	/* ---- TSS (just ss0 / esp0 needed for ring 3→0 switch) ---- */
	ring3_tss.ss0  = 0x10;			/* kernel data */
	ring3_tss.esp0 = (u32)ring3_page + 0x1000;	/* top of ring3_page */

	/* ---- TSS descriptor in GDT[5] ---- */
	{
		u32 base  = (u32)&ring3_tss;
		u32 limit = sizeof(ring3_tss) - 1;

		ring3_gdt[5].limit_low   = limit & 0xFFFF;
		ring3_gdt[5].base_low    = base & 0xFFFF;
		ring3_gdt[5].base_mid    = (base >> 16) & 0xFF;
		ring3_gdt[5].access      = 0x89;	/* P=1, DPL=0, TSS32-avail */
		ring3_gdt[5].base_high   = (base >> 24) & 0xFF;
		/* granularity stays 0 — limit fits in 20 bits */
	}

	/* ---- Load GDT ---- */
	{
		struct gdt_ptr gp = { sizeof(ring3_gdt) - 1, (u32)ring3_gdt };
		asm volatile("lgdt %0" : : "m"(gp));
	}

	/* Reload CS (far jump) and data segments */
	asm volatile("ljmp $0x08, $1f\n" "1:\n");
	asm volatile(
		"mov $0x10, %%ax\n"
		"mov %%ax, %%ds\n"
		"mov %%ax, %%es\n"
		"mov %%ax, %%fs\n"
		"mov %%ax, %%gs\n"
		"mov %%ax, %%ss\n"
		: : : "eax"
	);

	/* Load TSS with ltr */
	asm volatile("ltr %%ax" : : "a"(0x28));

	printf("ring3: GDT+TSS loaded, kernel stack at %x\n",
	       ring3_tss.esp0);

	/* ---- Set up page tables for user code/stack ---- */
	pdx = 0x400000 >> 22;			/* PDX = 1 */

	if (!(kernel_page_dir[pdx] & PAGE_PRESENT)) {
		pt = (u32 *)alloc_page();
		if (!pt) {
			printf("ring3: OOM for PT\n");
			return;
		}
		for (i = 0; i < 1024; i++)
			pt[i] = 0;
		kernel_page_dir[pdx] = PAGE_ENTRY((u32)pt,
			PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
	} else {
		pt = (u32 *)(kernel_page_dir[pdx] & 0xFFFFF000);
		kernel_page_dir[pdx] |= PAGE_USER;
	}

	/* Write user program:  mov eax, 'Y';  int $0x80;  jmp $ */
	ring3_page[0] = 0xB8;
	ring3_page[1] = 'Y';
	ring3_page[2] = 0x00;
	ring3_page[3] = 0x00;
	ring3_page[4] = 0x00;
	ring3_page[5] = 0xCD;			/* int  */
	ring3_page[6] = 0x80;			/* 0x80 */
	ring3_page[7] = 0xEB;			/* jmp  */
	ring3_page[8] = 0xFE;			/*   $  */

	/* Map the same physical page at two virtual addresses:
	 *   0x400000 — user code (executes from the start of the page)
	 *   0x500000 — user stack (grows down from 0x501000)          */
	ptx = (0x400000 >> 12) & 0x3FF;
	pt[ptx] = PAGE_ENTRY((u32)ring3_page, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);

	ptx = (0x500000 >> 12) & 0x3FF;
	pt[ptx] = PAGE_ENTRY((u32)ring3_page, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);

	asm volatile("invlpg (%0)" : : "r"(0x400000) : "memory");
	asm volatile("invlpg (%0)" : : "r"(0x500000) : "memory");

	printf("ring3: jumping to user code at 0x400000...\n");

	/* Never returns */
	jump_to_ring3(0x400000, 0x501000);
}

/* ==================================================================== */

void kernel_main(void)
{
	volatile u16 *const vga = (u16 *)0xB8000;
	int i;

	/* Disable the hardware cursor */
	outb(0x3D4, 0x0A);
	outb(0x3D5, 0x20);

	/* Clear screen */
	for (i = 0; i < MAX_WIDTH * MAX_HEIGHT; i++)
		vga[i] = 0x0700 | ' ';

	printf("Hello from 32-bit kernel!\n");

	/* Load IDT and remap PIC */
	idt_init();
	printf("IDT loaded.\n");

	pic_remap();
	printf("PIC remapped.\n");

	/* Initialise physical memory buddy allocator */
	bitmap_init();

	/* Enable paging — identity-map first 4 MiB */
	paging_init();

/*
	// Combined buddy + paging test (temporarily disabled)
	{
		enum { N = 8 };
		void *phys[N];
		void *virt;
		u32 patterns[N];
		void *big;
		int i, ok = 1;

		for (i = 0; i < N; i++)
			phys[i] = alloc_page();
		printf("phys: ");
		for (i = 0; i < N; i++)
			printf(" %x", (u32)phys[i]);
		printf("\n");

		virt = valloc_pages(N);
		printf("virt: %x - %x\n", (u32)virt,
		       (u32)virt + (N - 1) * 0x1000);

		for (i = 0; i < N; i++)
			map_page((u32)virt + i * 0x1000, (u32)phys[i],
				 PAGE_PRESENT | PAGE_WRITE);

		for (i = 0; i < N; i++)
			patterns[i] = 0xBEEF0000 | i;
		for (i = 0; i < N; i++)
			*(u32 *)(virt + i * 0x1000) = patterns[i];

		printf("phys readback:\n");
		for (i = 0; i < N; i++) {
			u32 val = *(u32 *)phys[i];
			printf("  [%d] %x = %x\n",
			       i, (u32)phys[i], val);
			if (val != patterns[i]) {
				printf("       (expected %x)\n", patterns[i]);
				ok = 0;
			}
		}

		for (i = 0; i < N; i++)
			free_page(phys[i]);

		big = alloc_pages(6);
		if (big) {
			*(u32 *)big = 0xCAFE;
			free_pages(big, 6);
		} else {
			printf("alloc_pages(6) after free: FAIL\n");
			ok = 0;
		}

		printf("buddy+paging: %s\n", ok ? "OK" : "FAIL");
	}
*/

	/* Ring 3 experiment */
	setup_ring3();

/*
	// Unmask PIT timer (IRQ 0) and keyboard (IRQ 1)
	outb(PIC1_DATA, inb(PIC1_DATA) & ~((1 << 0) | (1 << 1)));
	printf("PIT + keyboard unmasked.\n");
*/

/*
	// Write static labels at rows 8-9
	for (i = 0; "Ticks: "[i]; i++)
		vga[8 * MAX_WIDTH + i] = 0x0700 | "Ticks: "[i];
	for (i = 0; "Key:   "[i]; i++)
		vga[9 * MAX_WIDTH + i] = 0x0700 | "Key:   "[i];
*/

	/* Enable interrupts — PIT will now fire at ~18 Hz */
	__asm__ volatile("sti");

	while (1) {
		/* Sleep until the next interrupt */
		__asm__ volatile("hlt");
/*
		// Update the tick display
		vga_write_dec_at(8, 7, g_ticks);
*/
	}
}
