// exec.c — load a disk program image and run it as a process
//
// The load path, in prose:
//
//   1. Read the program's first sector (the header) into a static
//      kernel buffer (identity-mapped, so safe from any context).
//   2. Validate the magic and that `entry` falls inside the image.
//   3. Allocate `npages` consecutive physical pages for the code, plus
//      one page for the user stack.
//   4. Map [load_addr, load_addr+npages*4K) to the code pages in BOTH:
//        - the kernel page directory (map_page)   — so the new
//          process's cloned PD inherits the mapping, and
//        - the current (shell) process's PD (map_page_in) — so WE can
//          write the disk bytes in from here.
//      Both mappings point at the same physical pages, with PAGE_USER
//      so ring 3 can reach them.  The user stack gets the same treatment
//      at USER_STACK_BASE (one page, grows down from USER_STACK_TOP).
//   5. Read `nsec` code sectors (starting at lba+1) into load_addr.
//   6. create_process(exec_run): the new process clones kernel_page_dir,
//      so it inherits the 0x400000 + 0x500000 mappings — no race, because
//      the mappings existed in kernel_page_dir BEFORE the clone.
//   7. wait() for the child.  exec_run() drops into ring 3 at the
//      program's entry via ring3_jump(); the program terminates by
//      issuing a syscall (int 0x80), whose handler calls sched_exit(),
//      and the child is reaped here.
//
#include "exec.h"
#include "ata.h"       /* ata_read_sectors */
#include "memory.h"    /* alloc_pages, free_pages */
#include "paging.h"    /* map_page, map_page_in, unmap_page*, PAGE_* */
#include "ring3.h"      /* ring3_jump */
#include "sched.h"     /* create_process, wait, sched_exit, sched_current_pd */
#include "printf.h"

/* The entry point the launched program should start at.  Set by do_exec
 * before create_process(), consumed by exec_run().  A plain global
 * because exec_run is entered via a fake iret frame (no arguments). */
static u32 g_exec_entry;

/* Trampoline: the new process starts here (its EIP = exec_run).  It
 * drops into ring 3 to run the loaded program; when the program issues
 * its syscall to exit, the process ends (the syscall handler calls
 * sched_exit).  This is how a program terminates: by asking the kernel
 * to end it, never by `ret`-ing back into kernel code (there is nothing
 * sane to return to once we are in ring 3). */
static void exec_run(void)
{
	/* g_exec_entry is set by do_exec before create_process().  Passed
	 * by value into ring3_jump because ring3_jump is entered via a
	 * fake iret frame (no real arguments survive the iret). */
	ring3_jump(g_exec_entry, USER_STACK_TOP);
	/* unreachable: ring3_jump irets into ring 3 and never returns */
}

static void exec_cleanup(u32 *cur_pd, u32 load_addr, u32 phys,
			  u32 npages, u32 stack_phys)
{
	u32 i;

	if (npages == 0)
		return;

	for (i = 0; i < npages; i++) {
		u32 vaddr = load_addr + i * 0x1000;

		unmap_page(vaddr);
		if (cur_pd)
			unmap_page_in(cur_pd, vaddr);
	}

	/* The user stack shares load_addr's 4-MiB region (PDX 1), so its
	 * PTE lives in the same page table as the code PTEs.  Unmap it so
	 * the PT can be reclaimed cleanly if a later exec frees it. */
	unmap_page(USER_STACK_BASE);
	if (cur_pd)
		unmap_page_in(cur_pd, USER_STACK_BASE);

	if (phys)
		free_pages((void *)phys, (int)npages);
	if (stack_phys)
		free_page((void *)stack_phys);
}

int do_exec(u32 lba)
{
	static u8 sec[512];        /* header scratch — identity-mapped */
	struct exec_hdr *h;
	u32 load_addr = 0;
	u32 npages = 0;
	u32 nsec, phys = 0, stack_phys = 0, i;
	u32 *cur_pd = NULL;
	int pid, code, ret = -1;

	/* 1. Read + validate the header (sector `lba`). */
	if (ata_read_sectors(lba, 1, sec) < 0) {
		printf("exec: header read failed (LBA %d)\n", lba);
		return -1;
	}
	h = (struct exec_hdr *)sec;
	if (h->magic != EXEC_MAGIC) {
		/* The sector does not start with the LNX header magic.
		 * exec only ever reads the single sector it was given and
		 * treats its first 4 bytes as magic, so a miss means this
		 * LBA is not a program's header — most often it points into
		 * a program's code/data sectors instead.  Spell that out
		 * rather than dumping a raw magic mismatch. */
		printf("exec %u: not a program image (no LNX header at this sector)\n",
		       lba);
		printf("  hint: exec needs a program's header sector (the first "
		       "sector of an image), not a code sector inside it\n");
		return -1;
	}
	if (h->length == 0) {
		printf("exec: empty program\n");
		return -1;
	}
	if (h->entry < h->load_addr ||
	    h->entry >= h->load_addr + h->length) {
		printf("exec: entry %x outside [%x, %x)\n",
		       h->entry, h->load_addr, h->load_addr + h->length);
		return -1;
	}

	/* 2. Allocate physical pages for the code. */
	load_addr = h->load_addr;
	npages = (h->length + 0xFFF) / 0x1000;
	nsec   = (h->length + 511) / 512;
	phys   = (u32)alloc_pages((int)npages);
	if (!phys) {
		printf("exec: OOM (need %d pages)\n", npages);
		return -1;
	}

	/* 2b. Allocate one physical page for the user stack and map it at
	 *     USER_STACK_BASE in the kernel PD (so the child's cloned PD
	 *     inherits it) with PAGE_USER — ring 3 must be able to use it
	 *     as a stack.  The stack shares load_addr's 4-MiB region, so
	 *     no second page-table page is needed (map_page reuses PT[1]).
	 *     Zero it so no stale kernel bytes leak to the user. */
	stack_phys = (u32)alloc_page();
	if (!stack_phys) {
		printf("exec: OOM for user stack\n");
		free_pages((void *)phys, (int)npages);
		return -1;
	}
	for (i = 0; i < 1024; i++)
		((u32 *)stack_phys)[i] = 0;

	/* 3. Map the image at load_addr in the kernel PD (so the child's
	 *    cloned PD inherits it) AND in the current PD (so this code can
	 *    write the disk bytes in).  Same physical pages both ways.
	 *    PAGE_USER so ring 3 can execute the code; PAGE_WRITE so the
	 *    load path can write the bytes in (code will be writable from
	 *    ring 3 too, a simplification we accept for the demo). */
	for (i = 0; i < npages; i++)
		map_page(load_addr + i * 0x1000, phys + i * 0x1000,
			 PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
	map_page(USER_STACK_BASE, stack_phys,
		 PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
	cur_pd = sched_current_pd();
	if (cur_pd) {
		for (i = 0; i < npages; i++)
			map_page_in(cur_pd, load_addr + i * 0x1000,
				    phys + i * 0x1000,
				    PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
		map_page_in(cur_pd, USER_STACK_BASE, stack_phys,
			    PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
	}

	/* 4. Read the code (starts at the sector AFTER the header). */
	if (ata_read_sectors(lba + 1, nsec, (void *)load_addr) < 0) {
		printf("exec: image read failed (LBA %d)\n", lba + 1);
		goto out_cleanup;
	}

	/* 5. Launch + wait.  The child runs exec_run, which drops into
	 *    ring 3 at the program's entry.  The program exits via its
	 *    syscall; the syscall handler calls sched_exit, the child is
	 *    reaped here. */
	g_exec_entry = h->entry;
	pid = create_process(exec_run, "prog");
	if (pid < 0) {
		printf("exec: create_process failed\n");
		goto out_cleanup;
	}
	printf("exec: launched pid %d, entry %x (ring 3)\n", pid, h->entry);

	wait(NULL, &code);
	printf("exec: pid %d exited (code %d)\n", pid, code);
	ret = 0;

out_cleanup:
	exec_cleanup(cur_pd, load_addr, phys, npages, stack_phys);
	return ret;
}
