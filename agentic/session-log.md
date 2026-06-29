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
