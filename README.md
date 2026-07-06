# Kernel

A small x86 teaching kernel built from scratch in C and assembly.
The project is a hands-on study of how a PC boots, how privilege and
memory work on x86, and how a process scheduler is built up layer by
layer — from a single boot sector to a preemptive scheduler with
priorities, sleeping, and automatic process reaping.

The eventual goal is to read and contribute to the Linux kernel; this
repository is the ladder there.

## Status

The kernel runs in **32-bit protected mode** under QEMU and currently
provides:

- **Boot chain** — a 512-byte boot sector loads further sectors from
  disk via BIOS `int 0x13`, switches to protected mode, and jumps to
  a C entry point.
- **VGA text output** — direct framebuffer writes at `0xB8000`, with
  a small `printf` supporting `%c %s %d %i %x %%` and screen scrolling.
- **Interrupt handling** — a full 256-entry IDT generated from an
  assembly macro, a shared C trampoline, and human-readable CPU
  exception reporting.
- **PIC + PIT drivers** — the 8259A is remapped to vectors 32-47 and
  individually masked/unmasked; the 8254 PIT channel 0 drives IRQ 0
  at a configurable rate.
- **Physical memory allocator** — a buddy allocator (per-order free
  lists + a page-order map) over the first 4 MiB of RAM.
- **Paging** — identity-mapped first 4 MiB with `map_page()` /
  `valloc_pages()` helpers.
- **Ring 0 / Ring 3 transition** — GDT with user segments, a TSS for
  `ss0`/`esp0`, an `iret`-based jump to ring 3, and `int $0x80` as a
  syscall vector. The ring-3 experiment is currently dormant
  (`#if 0`) while the scheduler is the focus.
- **Preemptive scheduler** — round-robin with priority levels
  (`PRIO_USER` / `PRIO_IDLE`), a permanent idle task as fallback,
  `sleep()` with timer-based wakeup, voluntary `sched_exit()`, and
  automatic reaping of finished processes (stack page freed, PCB
  slot cleared).

A 64-bit long-mode path was written (4-level paging, GDT64, C entry)
but cannot be verified on the current host (Intel Core Ultra 9 285H +
Linux 7.0.0 — `wrmsr` to `EFER.LME` is filtered by KVM and QEMU TCG
alike). Development continues in 32-bit protected mode; the core
algorithms are mode-agnostic and will port by changing only the
architecture-specific layer (IDT entry size, register width,
`iretd`→`iretq`).

## Repository layout

```
stage-1-boot-sector/       BIOS boot sector that prints 'A'
stage-2-protected-mode/    Enters 32-bit protected mode, VGA output
stage-3-long-mode/         64-bit long-mode path (coded, unverifiable here)
stage-3-protected-mode/    Active development stage — 32-bit kernel
my-kernel/                 Shared kernel sources
├── include/               Header files (types, sched, idt, pic, pit, ...)
└── kernel/                C and assembly implementations
agentic/                   Learning notes (Chinese), numbered by topic
CLAUDE.md                  Project instructions and roadmap
```

The active stage is `stage-3-protected-mode/`. It pulls shared kernel
sources from `my-kernel/` and builds a flat binary that the boot
sector loads.

## Toolchain

Tested with the following versions:

| Tool    | Version   |
|---------|-----------|
| gcc     | 15.2.0    |
| binutils| 2.46      |
| make    | 4.4.1     |
| QEMU    | 8.2.0     |
| GDB     | 17.1      |

The kernel is built freestanding (`-m32 -ffreestanding -nostdlib`) and
linked as a flat binary (`elf_i386`, then `objcopy` to raw).

## Build and run

```bash
cd stage-3-protected-mode
make            # build build/disk.img
make run        # build and boot in QEMU
make debug      # build and boot with QEMU -s -S (wait for GDB)
```

### Debugging with GDB

```bash
# Terminal 1 — QEMU paused, GDB server on :1234
cd stage-3-protected-mode
make debug

# Terminal 2 — connect GDB
gdb build/stage3.elf \
    -ex "set architecture i386:x86-64" \
    -ex "target remote localhost:1234"
```

`set architecture i386:x86-64` is required because QEMU's `qemu64` CPU
identifies as x86-64 even while running 32-bit code.

Common inspection commands:

```
break irq0_enter           # break on every timer tick
print/x $cr3               # page-directory physical address
x/4wx 0x11000              # inspect page-directory entries
p/x procs                  # the process table
```

## Notes

The `agentic/` directory contains numbered learning notes written in
Chinese. They are the primary record of design decisions, pitfalls, and
the reasoning behind each subsystem. The session log
(`agentic/session-log.md`) summarises progress per working session.

See `CLAUDE.md` for the project roadmap and working conventions.
