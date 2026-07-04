// ring3.c — Ring 3 (user-mode) initialisation (32-bit)
//
// Builds a new GDT with user code/data segments and a TSS descriptor,
// allocates / maps a page for user code + stack, writes a tiny user
// program, and enters ring 3 via iret.

#include "types.h"
#include "printf.h"
#include "memory.h"
#include "paging.h"

/* ------------------------------------------------------------------ */
/*  Structures                                                         */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/*  Static storage (.bss)                                              */
/* ------------------------------------------------------------------ */

static struct gdt_entry ring3_gdt[6];
static struct tss    ring3_tss;
static u8             ring3_page[4096] __attribute__((aligned(4096)));

/* ------------------------------------------------------------------ */
/*   iret  helper — never returns                                      */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void setup_ring3(void)
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
	ring3_tss.esp0 = (u32)ring3_page + 0x1000;

	/* ---- TSS descriptor in GDT[5] ---- */
	{
		u32 base  = (u32)&ring3_tss;
		u32 limit = sizeof(ring3_tss) - 1;

		ring3_gdt[5].limit_low   = limit & 0xFFFF;
		ring3_gdt[5].base_low    = base & 0xFFFF;
		ring3_gdt[5].base_mid    = (base >> 16) & 0xFF;
		ring3_gdt[5].access      = 0x89;	/* P=1, DPL=0, TSS32-avail */
		ring3_gdt[5].base_high   = (base >> 24) & 0xFF;
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
