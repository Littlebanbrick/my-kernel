// exec.c — load a disk program image and run it as a process
//
// The load path, in prose:
//
//   1. Read the program's first sector (the header) into a static
//      kernel buffer (identity-mapped, so safe from any context).
//   2. Validate the magic and that `entry` falls inside the image.
//   3. Allocate `npages` consecutive physical pages for the code.
//   4. Map [load_addr, load_addr+npages*4K) to those pages in BOTH:
//        - the kernel page directory (map_page)   — so the new
//          process's cloned PD inherits the mapping, and
//        - the current (shell) process's PD (map_page_in) — so WE can
//          write the disk bytes in from here.
//      Both mappings point at the same physical pages.
//   5. Read `nsec` code sectors (starting at lba+1) into load_addr.
//   6. create_process(exec_run): the new process clones kernel_page_dir,
//      so it inherits the 0x400000 mapping — no race, because the
//      mapping existed in kernel_page_dir BEFORE the clone.
//   7. wait() for the child.  exec_run calls the program; when the
//      program returns, exec_run calls sched_exit() and the child is
//      reaped here.
//
// Known limitation (documented, like the earlier PT leak): after the
// child exits we do NOT unmap 0x400000 nor free the code pages.  The
// mappings are overwritten on the next exec; the old code pages leak
// until reboot.  Cleanup is a follow-up once we have per-process PTs
// for the program region (mirroring priv_pt).

#include "exec.h"
#include "ata.h"       /* ata_read_sectors */
#include "memory.h"    /* alloc_pages */
#include "paging.h"    /* map_page, map_page_in, PAGE_* */
#include "sched.h"     /* create_process, wait, sched_exit, sched_current_pd */
#include "printf.h"

/* The entry point the launched program should start at.  Set by do_exec
 * before create_process(), consumed by exec_run().  A plain global
 * because exec_run is entered via a fake iret frame (no arguments). */
static u32 g_exec_entry;

/* Trampoline: the new process starts here (its EIP = exec_run).  It
 * calls the loaded program as an ordinary C function, then — when the
 * program returns — exits.  This is how a header-less, syscall-less
 * program terminates: by `ret`-ing back into kernel code. */
static void exec_run(void)
{
	void (*fn)(void) = (void (*)(void))g_exec_entry;
	fn();
	sched_exit(0);
}

int do_exec(u32 lba)
{
	static u8 sec[512];        /* header scratch — identity-mapped */
	struct exec_hdr *h;
	u32 npages, nsec, phys, i;
	u32 *cur_pd;
	int pid, code;

	/* 1. Read + validate the header (sector `lba`). */
	if (ata_read_sectors(lba, 1, sec) < 0) {
		printf("exec: header read failed (LBA %d)\n", lba);
		return -1;
	}
	h = (struct exec_hdr *)sec;
	if (h->magic != EXEC_MAGIC) {
		printf("exec: bad magic %x (want %x)\n", h->magic, EXEC_MAGIC);
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
	npages = (h->length + 0xFFF) / 0x1000;
	nsec   = (h->length + 511) / 512;
	phys   = (u32)alloc_pages((int)npages);
	if (!phys) {
		printf("exec: OOM (need %d pages)\n", npages);
		return -1;
	}

	/* 3. Map the image at load_addr in the kernel PD (so the child's
	 *    cloned PD inherits it) AND in the current PD (so this code can
	 *    write the disk bytes in).  Same physical pages both ways. */
	for (i = 0; i < npages; i++)
		map_page(h->load_addr + i * 0x1000, phys + i * 0x1000,
			 PAGE_PRESENT | PAGE_WRITE);
	cur_pd = sched_current_pd();
	if (cur_pd)
		for (i = 0; i < npages; i++)
			map_page_in(cur_pd, h->load_addr + i * 0x1000,
				    phys + i * 0x1000,
				    PAGE_PRESENT | PAGE_WRITE);

	/* 4. Read the code (starts at the sector AFTER the header). */
	if (ata_read_sectors(lba + 1, nsec, (void *)h->load_addr) < 0) {
		printf("exec: image read failed (LBA %d)\n", lba + 1);
		return -1;   /* mappings + phys leak; documented */
	}

	/* 5. Launch + wait.  The child runs exec_run, which jumps to the
	 *    program's entry and exits when it returns. */
	g_exec_entry = h->entry;
	pid = create_process(exec_run, "prog");
	if (pid < 0) {
		printf("exec: create_process failed\n");
		return -1;
	}
	printf("exec: launched pid %d, entry %x\n", pid, h->entry);

	wait(NULL, &code);
	printf("exec: pid %d exited (code %d)\n", pid, code);
	return 0;
}
