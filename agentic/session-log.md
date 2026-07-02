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


### 2026-07-02 — 模拟器调试与 IDT 框架

**目标**：实现 IDT 框架，然后尝试在 VirtualBox/Bochs 中运行长模式内核。

**完成内容**：
- 实现完整的 IDT 框架（include/idt.h, kernel/idt.c, kernel/idt_handlers.S）
- 256 个汇编 trampoline + handle_exception C 函数
- printf/putchar 功能（含滚动支持）
- kernel/main.c 入口点

**模拟器调试**：
- 修复 boot.S 的扇区偏移 bug（CL=2 是正确值）
- VirtualBox 7.2.6：EFER.LME 同样被过滤（VT-x 路径同 QEMU）
- Bochs 3.0 快照版：CPU 模型不完整，wrmsr 静默丢弃
- QEMU TCG：仍然失败（PDPTR check failed）
- 最小 32-bit stage3 验证通过（boot 加载和跳转无误）

**学习笔记**：agentic/04-emulator-debugging-journey.md

**踩坑记录**：
- IDT handler 不能使用 `__attribute__((interrupt))` 来获取向量号 → 需要 256 个汇编 trampoline 每个硬编码自己的编号
- GAS `.altmacro` 模式下 `$0` 被解释为宏参数引用，而非立即数 0 → 改用手动 `.irp` 展开
- `putchar` 的光标坐标必须传指针（值传递的修改对调用方不可见）
- Bochs 要求磁盘几何参数匹配文件大小，过小的磁盘（<1MB）会导致自动检测出 0 柱面
- VirtualBox VDI 要求至少 ~1MB 磁盘
- Bochs `msrs.def` 解析器不支持 0xC0000080 这样的大 MSR 索引号

---

### 2026-07-01 — 64 位长模式与分页

**目标**：进入 64 位长模式，首次运行 C 代码。

**完成内容**：
- Booth.S 扩展：用 `int 0x13, AH=0x02` 读取 60 个扇区到 0x8000，突破 512 字节限制
- 编写 4 级页表（PML4、PDP、PD），使用 2MB 大页恒等映射前 4MB
- 配置 CR4.PAE、CR3、EFER.LME 准备进入长模式
- 编写 64 位 GDT（L=1）与 ljmp 切换代码
- 编写 kernel.c——首个 C 语言内核代码
- 编写 linker.ld 将各段放在正确位置（0x8000 ~ 0xC000）
- 理解页表分级、PAE、wrmsr/rdmsr、volatile、.align、.globl 等概念
- 学习笔记：agentic/03-long-mode-paging.md

**踩坑记录**：
- `.equ` 与 `#define` 混淆：前者是 GAS 汇编器指令，后者是 C 预处理器，boot.S 必须用 `.equ`
- 页表三个 4KB 段（.pml4 /.pdp /.pd）是编译时占位，运行时由 32 位代码写入页表项
- 链接脚本通过各段累加地址自动将 .text 推到 0xB000
- GDB 跳转到 0xB000 后失去符号 → `add-symbol-file build/stage3.elf 0xB000`
- **wrmsr 设置 EFER.LME（bit 10）在 QEMU 10.2.1 + KVM + Intel Core Ultra 9 285H 环境下被静默拒绝**：
  - SCE (bit 0) 和 NXE (bit 11) 可以正常写入，唯独 LME 被过滤
  - 排除 TCG 模拟 bug → KVM（真 CPU）同样复现
  - 排除 CPU 不支持（`lm` flag 存在，CPUID 确认）
  - 排除 PAE 顺序问题（CR4.PAE 在 LME 之前设置）
  - QEMU monitor 确认 `vmx-entry-ia32e-mode = true`（VMCS 允许长模式）
  - 可能原因：QEMU 10.2.1 / Linux 7.0.0 的 KVM 模块在特定 CPU 上有 EFER 过滤 bug
  - 代码本身正确，等后续环境修复后可验证
