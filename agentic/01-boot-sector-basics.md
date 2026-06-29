# 学习笔记：Boot Sector 基础

> 日期：2026-06-29
> 目标：编写并调试一个在 QEMU 上运行的极简 boot sector

---

## 一、计算机启动流程

```
通电
  │
  ▼
CPU 去 0xFFFFFFF0 执行 BIOS（固化在主板 ROM 中，我们改不了）
  │
  ▼
BIOS 自检（POST）→ 检测硬件 → 扫描启动设备
  │
  ▼
读取硬盘第一个扇区（512 字节）到内存地址 0x7C00
  │
  ▼
检查最后两字节是否为 0x55 0xAA → 否则不是可启动扇区
  │
  ▼
跳转到 0x7C00 执行我们的 boot sector 代码
```

- **BIOS**：主板固件，我们只调用它的服务，不改它。
- **Boot Sector**：我们写的第一段代码，512 字节，结尾魔数 0xAA55。

---

## 二、Boot Sector 的结构

```
地址范围        内容
──────────────────────────────────
0x000 - 0x1FD  代码和填充（510 字节）
0x1FE - 0x1FF  魔数 0x55 0xAA（小端序 = 0xAA55）
```

一个合法 boot sector 必须：
1. 正好 512 字节
2. 最后两字节是 `0x55 0xAA`

---

## 三、AT&T 汇编基础（GNU as）

### 语法速记

| 写法 | 含义 |
|------|------|
| `movb $0x0E, %ah` | 把**立即数** 0x0E 移动到**寄存器** ah |
| `movb $'A', %al` | 把字符 'A' 的 ASCII 码移动到寄存器 al |
| `int $0x10` | 触发 0x10 号中断（调用 BIOS 服务） |
| `jmp label` | 无条件跳转到 label |

### 关键规则

- **源操作数在左，目的操作数在右**（与 Intel 语法相反）
- 寄存器前加 `%`：`%ax`、`%sp`
- 立即数前加 `$`：`$0x7C00`、`$0x0E`
- 指令后缀表操作数大小：`b`(byte=8bit), `w`(word=16bit), `l`(long=32bit), `q`(quad=64bit)

---

## 四、关键寄存器

### CS:IP — 代码段寻址

实模式下：**物理地址 = CS × 16 + IP**

CPU 的核心循环就是：去 CS:IP 取指令 → 执行 → IP 前进 → 重复。

### EFLAGS — 标志寄存器

| 标志 | 位 | 含义 |
|------|----|------|
| CF | bit 0 | 进位/借位标志 |
| ZF | bit 6 | 结果为 0 |
| SF | bit 7 | 结果为负 |
| IF | bit 9 | 中断允许标志（保护模式切换前必须关） |
| DF | bit 10 | 方向标志 |
| OF | bit 11 | 溢出标志 |

### RAX 结构

```
RAX (64位)
├── EAX (低32位)
│   ├── AX (低16位)
│   │   ├── AH (高8位)
│   │   └── AL (低8位)
```

---

## 五、BIOS 中断调用

以 `int $0x10`（视频服务）为例：

```assembly
movb $0x0E, %ah     # AH = 功能号（0x0E = 电传打字机输出）
movb $'A', %al      # AL = 要打印的字符
int $0x10           # 调用 BIOS 视频中断
```

调用流程：
1. 在寄存器中设置参数
2. `int N` 触发第 N 号中断
3. CPU 查中断向量表，跳转到 BIOS 的处理函数
4. BIOS 执行完后 `iret` 返回

> 注意：切换到保护模式/长模式后，BIOS 中断不可用！必须自己写所有驱动。

---

## 六、QEMU + GDB 调试环境

### 环境搭建

| 工具 | 版本 |
|------|------|
| gcc | 15.2.0 |
| gdb | 17.1 |
| make | 4.4.1 |
| qemu-system-x86_64 | 10.2.1 |

### 构建流水线

```
boot.S  →  as --32  →  boot.o  →  ld -Ttext 0x7C00  →  boot.elf  →  objcopy -O binary  →  boot.bin
```

### GDB 常用命令

| 命令 | 简写 | 含义 |
|------|------|------|
| `break _start` | `b _start` | 软件断点（在内存写 0xCC） |
| `hbreak _start` | `hb _start` | **硬件断点**（用 CPU 调试寄存器，推荐用于 boot sector） |
| `break *0x7C09` | `b *0x7C09` | 在绝对地址设断点 |
| `continue` | `c` | 继续执行 |
| `stepi` | `si` | 单步执行一条汇编指令（会进入函数/中断内部） |
| `nexti` | `ni` | 单步执行，跳过函数/中断调用 |
| `info registers` | `ir` | 查看所有寄存器 |
| `p/x $eax` | | 以十六进制打印 EAX |
| `x/16bx 0x7C00` | | 查看内存内容 |

### 调试流程

```bash
# 终端 1：启动 QEMU（冻结 CPU）
make debug

# 终端 2：连接 GDB
gdb build/boot.elf \
    -ex "set architecture i386:x86-64" \
    -ex "target remote localhost:1234"

# 然后按顺序：
(gdb) hbreak _start
(gdb) continue
(gdb) stepi               # 逐条观察自己的代码
(gdb) nexti               # 跳过 int 调用
(gdb) info registers      # 查看 CPU 状态
```

### 关键经验

1. **Boot sector 调试用 `hbreak` 而非 `break`**：因为 BIOS 加载 boot sector 时会覆盖 `break` 写入的 0xCC 断点指令。
2. **`stepi` vs `nexti`**：调用 BIOS 中断时用 `nexti` 跳过去，用 `stepi` 会进入 BIOS 内部。
3. **计算指令地址**：手算机器码长度，或用 `xxd build/boot.bin` 查看（如 `eb fe` = `jmp hang`）。
4. **GDB 架构问题**：QEMU 的 64 位 CPU 与 32 位 ELF 不兼容，需要用 `set architecture i386:x86-64` 修复。
5. **QEMU 进程管理**：调试完后记得关掉 QEMU 窗口（Ctrl+A X），否则会后台吃 CPU。

---

## 七、Makefile 要点

| 命令 | 作用 |
|------|------|
| `make` | 编译 boot.bin |
| `make run` | 编译 + QEMU 运行 |
| `make debug` | 编译 + QEMU 冻结（-s -S） |
| `make clean` | 清理 build/ |

**objcopy 参数顺序**：`objcopy -O <输出格式> <输入文件> <输出文件>`
（曾踩坑：把 `$<` 和 `$@` 写反导致编译失败）

**ld 警告**：`LOAD segment with RWX permissions` 在裸机场景下忽略即可。

---

## 八、术语表

| 术语 | 解释 |
|------|------|
| 实模式 | 16 位模式，CPU 启动时的初始状态，只能访问 1MB 内存 |
| 保护模式 | 32 位模式，支持分段分页、特权级 |
| 长模式 | x86_64 的 64 位模式 |
| 魔数 | 固定的标记值（0xAA55），BIOS 用来识别可启动扇区 |
| 中断向量表 | 内存 0x0000-0x03FF，共 256 个中断处理函数地址 |
| Linker Script | 链接器用的布局脚本（我们用了 `-Ttext 0x7C00` 简版） |
