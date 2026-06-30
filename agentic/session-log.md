# 会话日志

> 记录每次对话的学习进展、关键决策和踩过的坑。

---

### 2026-06-29 — 第一个 Boot Sector

**目标**：编写并调试一个在 QEMU 上运行的极简 boot sector。

**完成内容**：
- 编写 boot.S（16 位实模式，AT&T 语法，打印 'A' 后死循环）
- 编写 Makefile（build / run / debug / clean）
- 学习笔记：agentic/01-boot-sector-basics.md

**踩坑记录**：
- `objcopy` 参数顺序写反（`$<` 和 `$@` 颠倒），导致编译失败
- `break`（软件断点）被 BIOS 加载 boot sector 时覆盖 → 改用 `hbreak`（硬件断点）
- `stepi` 陷入 BIOS 中断内部，出不来 → 用 `nexti` 或 `break *0x7C09` 跳过
- GDB 误判目标 CPU 架构（32-bit ELF vs QEMU's 64-bit）→ `set architecture i386:x86-64`
- QEMU 进程残留后台吃 CPU → 记得 `Ctrl+A X` 退出

---

### 2026-06-30 — 32 位保护模式 + GDT

**目标**：进入 32 位保护模式，用 VGA 显存直接输出（脱离 BIOS）。

**完成内容**：
- 编写 GDT（空描述符、代码段、数据段）
- 从实模式切换到保护模式（LGDT → CR0.PE → far jump）
- 用 `mov` 直接写 VGA 显存 0xB8000 输出 "32"
- 用 `rep stosw` 实现清屏（2000 次写入填满 80×25 屏幕）
- 调试环境重构：按 stage 组织目录（stage-1-boot-sector / stage-2-protected-mode）
- 学习笔记：agentic/02-protected-mode-gdt.md

**踩坑记录**：
- `lgdt` 使用 DS:偏移量寻址，BIOS 中断可能改了 DS → 必须 `xorw %ax,%ax; movw %ax,%ds`
- VGA 写入在 0xB8000（左上角），不是光标位置 → 看错位置以为代码没工作
- `rep stosw` 调试时 stepi 要按 2000 次 → 用 `break *next_addr` + `continue` 跳过
- `-debugcon stdio` 方式不可靠 → 改用 `-serial stdio` + 串口初始化确认保护模式执行
- 环境 headless 无法直接调试 QEMU → 用 `-no-reboot` 判断是否 triple fault
