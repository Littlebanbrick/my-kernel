# 学习笔记：printf 实现与 IDT 中断框架

> 日期：2026-07-02
> 目标：理解 VGA 文本模式下的 printf 输出和 32 位保护模式下的 IDT 中断处理机制

---

## 一、从 putchar 到 printf——输出流水线

我们目前没有 stdio 库、没有操作系统内核、没有任何"文件描述符"的概念。所有输出最终都归结为"往 VGA 显存写属性字节"。

### 调用链

```
printf("dec: %d\n", 42)
  │
  ├─ 解析格式字符串：遇到 %d
  │   └─ print_dec() — 把整数 42 转成 "42"
  │       └─ putchar() — 每个字符写到 VGA 显存
  │
  ├─ 遇到 \n
  │   └─ 光标换行（y++，x 归零）
  │       └─ 满屏时 scroll_screen()
  │
  └─ 普通字符如 "H"
      └─ putchar() — 直接写 VGA
```

### putchar——底层的"写一个字符"

```c
void putchar(u16* vga, cursor_coordinates* coord, unsigned char c)
{
    u16 index = coord->y * MAX_WIDTH + coord->x;

    vga[index] = ((u16)0x0700) | c;

    coord->x++;
    if (coord->x >= MAX_WIDTH) {
        coord->x = 0;
        coord->y++;
        if (coord->y >= MAX_HEIGHT) {
            scroll_screen(vga);
            coord->y = MAX_HEIGHT - 1;
        }
    }
}
```

要点：
- **`vga[index]`**：VGA 文本模式的显存基址是 `0xB8000`，每个字符格 2 字节（低字节 = ASCII 码，高字节 = 属性 `0x07` = 黑底亮灰字）。
- **光标是软件维护的**：没有用 VGA 硬件光标寄存器（`0x3D4`/`0x3D5`），而是用一个 `cursor_coordinates` 结构体（x, y）跟踪当前位置。我们在 `kernel.c` 里关了硬件光标。
- **满屏自动上卷**：`y >= 25` 时调用 `scroll_screen()`，把行 1-24 复制到 0-23，最后一行清空。

### scroll_screen——上卷

```c
void scroll_screen(u16* vga)
{
    u16* dst = vga;
    u16* src = vga + MAX_WIDTH;
    int i;

    for (i = 0; i < (MAX_HEIGHT - 1) * MAX_WIDTH; i++)
        *dst++ = *src++;

    for (i = 0; i < MAX_WIDTH; i++)
        *dst++ = 0x0700 | ' ';
}
```

用 C 的内存复制实现了"滚屏"——所有行数据向上平移一行，最后一行填入空格。这个操作在 x86 上展开也就是一堆 `mov` 指令，但比逐字用汇编写可读多了。

### printf——格式化输出

```c
void printf(const char* fmt, ...)
{
    u16* const vga = (u16*)0xB8000;
    static cursor_coordinates cursor = {0, 0};
    va_list ap;

    va_start(ap, fmt);

    for (const char* p = fmt; *p; p++) {
        if (*p == '\n') {
            cursor.x = 0;
            cursor.y++;
            scroll_if_needed...
            continue;
        }
        if (*p != '%') {
            putchar(vga, &cursor, (unsigned char)*p);
            continue;
        }
        // 解析 % 之后的格式说明符
        p++;
        switch (*p) {
        case 'c': putchar(... va_arg(ap, int) ...); break;
        case 's': while (*s) putchar(... *s++); break;
        case 'd': print_dec(vga, &cursor, va_arg(ap, int)); break;
        case 'x': print_hex(vga, &cursor, va_arg(ap, unsigned int), width); break;
        case '%': putchar(vga, &cursor, '%'); break;
        }
    }
    va_end(ap);
}
```

**关键设计**：

1. **`static cursor_coordinates`**：光标在多次 `printf` 调用之间持续存在。如果每次调用都从 (0,0) 开始，输出就会互相覆盖。静态变量让光标"记住"上次写到了哪。

2. **C99 变长参数（`<stdarg.h>`）**：在裸机环境下也能用 `va_list`、`va_start`、`va_arg`、`va_end`。这些是编译器内建支持（`__builtin_va_list` 等），不需要操作系统。

3. **边解析边输出**：不分配临时缓冲区，逐字符处理。好处是不需要 malloc / 固定大缓冲区；坏处是性能不如先格式化到缓冲区再批量写——但在内核开发阶段，这种性能差异无关紧要。

### print_dec——整数转字符串

```c
static void print_dec(u16* vga, cursor_coordinates* coord, int val)
{
    char buf[12];
    int i = 0, negative = 0;

    if (val < 0) { negative = 1; val = -val; }

    do {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    } while (val > 0);

    if (negative) buf[i++] = '-';

    while (i > 0) putchar(vga, coord, buf[--i]);
}
```

**为什么要用 `do { } while (val > 0)` 而不是 `while`？**
因为当 `val == 0` 时，至少需要输出一个字符 `'0'`。`do-while` 保证了"至少执行一次"。

**`val = -val` 为什么不溢出？**
`int` 最小值是 `-2147483648`，取反后 `2147483648` 超出 `int` 范围。在我们的场景中只传了 `42`、`0xDEAD` 之类的值，没有用到边界值。更严谨的做法是使用 `unsigned int` 类型来避免这个 corner case。

### print_hex——十六进制输出

```c
static void print_hex(u16* vga, cursor_coordinates* coord,
                      unsigned int val, int width)
{
    const char hex_chars[] = "0123456789abcdef";
    char buf[8];
    int i = 0;

    do {
        buf[i++] = hex_chars[val & 0xF];
        val >>= 4;
    } while (val > 0);

    while (i < width) buf[i++] = '0';  // 零填充

    putchar(vga, coord, '0');          // 前缀
    putchar(vga, coord, 'x');

    while (i > 0) putchar(vga, coord, buf[--i]);
}
```

我们约定 `%x` 总是带 `0x` 前缀输出，并且支持宽度指定（`%08x` 补零）。这在打印地址和寄存器的值时非常有用。

---

## 二、IDT——中断描述符表

### 什么是中断？

中断是 CPU 响应外部/内部事件的机制：

| 类型 | 来源 | 例子 |
|------|------|------|
| **异常** | CPU 内部 | 除零、缺页、非法指令 |
| **IRQ** | 硬件外设 | 键盘按键、定时器 |
| **软中断** | `int` 指令 | BIOS 调用（实模式）|

在保护模式下，所有中断都通过 **IDT（Interrupt Descriptor Table）** 来路由——不再有实模式的中断向量表（IVT）。

### IDT 的结构

IDT 是一张最多 256 个条目的数组，每个条目 **8 字节**（32 位模式）：

```
位 63-48          位 47-40   位 39-37  位 36-32   位 31-16          位 15-0
┌─────────────────┬─────────┬─────────┬─────────┬─────────────────┬─────────────────┐
│   offset_high   │  flags  │  0x00   │  zero   │   selector      │   offset_low    │
│   (位 31-16)    │         │         │         │   (一般是 0x08)  │   (位 15-0)      │
└─────────────────┴─────────┴─────────┴─────────┴─────────────────┴─────────────────┘
```

C 结构体定义：
```c
struct idt_entry {
    u16 offset_low;     // handler 地址的低 16 位
    u16 selector;       // 代码段选择子（我们固定用 0x08）
    u8  zero;           // 必须为 0
    u8  flags;          // Present + DPL + Type
    u16 offset_high;    // handler 地址的高 16 位
} __attribute__((packed));
```

handler 地址的拼装：
```
handler_addr = (offset_high << 16) | offset_low
```
由于 32 位模式下所有代码在 4GB 空间内，地址就是普通的 32 位指针。

### flags 字段含义

```c
#define IDT_KERN_INT   (IDT_PRESENT | IDT_DPL_KERN | IDT_TYPE_INT)
                       // 0x80       | 0x00         | 0x0E
                       // = 0x8E
```

| 位 | 字段 | 值 | 含义 |
|----|------|----|------|
| 7 | P (Present) | 1 | 该条目有效 |
| 6-5 | DPL | 00 | Ring 0（内核级）才能触发 |
| 4 | 保留 | 0 | 必须为 0 |
| 3-0 | Type | 0xE | **中断门**（`int` 指令和硬件 IRQ 用）|

**中断门 vs 陷阱门**：中断门 (`0xE`) 在进入 handler 时自动 `cli`（关中断），陷阱门 (`0xF`) 不会。对于异常处理我们两者皆可，但中断门更安全——防止 handler 执行中被另一个中断嵌套打断。

### IDT 初始化

```c
static struct idt_entry idt[256] __attribute__((aligned(16)));

void idt_init(void)
{
    struct idt_ptr idtp;
    int i;

    for (i = 0; i < 256; i++)
        idt_set_entry(&idt[i], handler_addrs[i], IDT_KERN_INT);

    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (u32)&idt;

    asm volatile("lidt %0" : : "m"(idtp));
}
```

**`__attribute__((aligned(16)))`**：虽然 x86 不强制 IDT 对齐，但 16 字节对齐是好的实践（优化访问速度）。

**`lidt`**：唯一让 CPU "知道我 IDT 在哪"的方式。传入一个 6 字节的伪描述符（`limit` + `base`），CPU 将其加载到内部的 IDTR 寄存器。

**`handler_addrs[i]`**：这是一个汇编导出的 256 个函数指针的数组（来自 `idt_handlers.S`），每个指向一个 trampoline（跳板函数）。

### IDT 指针（传给 lidt 指令）

```c
struct idt_ptr {
    u16 limit;    // 总字节数 - 1
    u32 base;     // IDT 数组的 32 位物理地址
} __attribute__((packed));
```

---

## 三、Trampoline 机制——每个中断号的跳板

### 为什么需要 trampoline？

CPU 触发中断时，硬件会自动压入 `EFLAGS → CS → EIP`，然后查找 IDT 跳转到 handler。但 CPU **不告诉我们发生了哪个中断号**——handler 需要自己知道"我是谁"。

解决方案：为每个中断号写一个独立的入口函数（trampoline），每个 trampoline 把自己的编号压栈，然后跳转到公共处理函数。

### 汇编宏实现

```asm
.macro HANDLER_NOERR vec
    .align 4
    handler_\vec:
        pushl   $0              /* 没有错误码的异常：补充一个 0 */
        pushl   $\vec           /* 压入中断号 */
        jmp     handler_common
.endm

.macro HANDLER_ERR vec
    .align 4
    handler_\vec:
        pushl   $\vec           /* CPU 已经压了错误码，只需压中断号 */
        jmp     handler_common
.endm
```

对所有 256 个中断号生成 trampoline：
```
.irp vec, 0,1,2,3,4,5,6,7
    HANDLER_NOERR \vec
.endr
HANDLER_ERR 8          // Double Fault 有错误码
HANDLER_NOERR 9
.irp vec, 10,11,12,13,14
    HANDLER_ERR \vec    // 这些异常都有错误码
.endr
...
```

### 哪些异常有错误码？

只有 7 个异常 CPU 会自动压入错误码：**#DF(8)、#TS(10)、#NP(11)、#SS(12)、#GP(13)、#PF(14)、#AC(17)**。

对于其他异常，trampoline 手动压入 `$0` 作为"伪错误码"，保证栈布局统一。

### 公共处理函数 `handler_common`

```asm
handler_common:
    /* 保存所有通用寄存器（cdecl 调用约定要求 callee 保存 ebx, ebp, esi, edi） */
    pushl   %eax
    pushl   %ecx
    pushl   %edx
    pushl   %ebx
    pushl   %ebp
    pushl   %esi
    pushl   %edi

    /* 调用 handle_exception(vec, error_code, &frame) */
    /* cdecl: 从右向左压参 */

    leal    36(%esp), %eax      /* arg3: frame ptr = 栈上 EIP 的位置 */
    pushl   %eax
    movl    36(%esp), %eax      /* arg2: error code (经过 1 次 push 后偏移变了) */
    pushl   %eax
    movl    36(%esp), %eax      /* arg1: vector */
    pushl   %eax
    call    handle_exception
    addl    $12, %esp           /* 弹出 3 个参数 */

    popl    %edi
    ...
    popl    %eax                /* 恢复所有寄存器 */

    addl    $8, %esp            /* 弹出 vector 和 error code */
    iret                        /* 中断返回：弹出 EIP → CS → EFLAGS */
```

#### 栈布局详解

触发中断时 CPU 压入：
```
ESP →  EIP      (4B)  ← 中断前的指令地址
ESP+4→ CS       (4B)
ESP+8→ EFLAGS   (4B)
```

trampoline 压入后：
```
ESP →  vector   (4B)  ← 我们压的
ESP+4→ error    (4B)  ← 0 或 CPU 压的错误码
ESP+8→ EIP      (4B)
ESP+12→ CS      (4B)
ESP+16→ EFLAGS  (4B)
```

保存 7 个寄存器后（每次 push ESP 减 4）：
```
ESP+0 → EDI     (已保存的寄存器)
...
ESP+24→ EAX
ESP+28→ vector  ← 隔了 7 次 push = 28 字节
ESP+32→ error
ESP+36→ EIP     ← &frame（传给 C 的指针）
ESP+40→ CS
ESP+44→ EFLAGS
```

为什么 `leal 36(%esp), %eax` 能得到 `frame`？因为此时 ESP 指向最后一次 push 的 EDI，+36 刚好越过 7 个寄存器 + vector + error 到 EIP。

### handler_addrs 表

编译期生成的 256 个 `.long` 指针数组，供 `idt.c` 引用：

```asm
.globl handler_addrs
handler_addrs:
    .long handler_0
    .long handler_1
    .long handler_2
    ...
    .long handler_255
```

相当于 C 语言的：
```c
extern void *handler_addrs[256];
```

---

## 四、异常处理——C 层面的 handler

```c
void handle_exception(u32 vec, u32 error_code,
                      struct interrupt_frame *frame)
{
    const char *name;
    // 查表获取异常名称
    if (vec < 32 && exception_names[vec])
        name = exception_names[vec];
    else
        name = "Reserved / Unknown";

    printf("!!! CPU EXCEPTION\n");
    printf("Vector: %02x (%s)\n", vec, name);
    printf("EIP: %08x\n", frame->eip);

    if (has_error_code(vec))
        printf("Err:  0x%x\n", error_code);

    printf("System halted.\n");

    while (1) asm volatile("hlt");
}
```

**`struct interrupt_frame`** = CPU 自动压入栈的三个值：

```c
struct interrupt_frame {
    u32 eip;      // 发生异常时的指令地址（定位哪行代码出问题）
    u32 cs;       // 代码段（应该总是 0x08）
    u32 eflags;   // 中断前的 EFLAGS
} __attribute__((packed));
```

**关键信息**：
- `frame->eip`：异常发生在哪条指令。配合 `objdump -S` 可以定位源代码行号。
- `error_code`：只有 7 种异常有，比如 Page Fault 的错误码会告诉你是"读还是写"、"用户态还是内核态"等问题。

---

## 五、验证：故意触发 #UD 异常

```c
__asm__ volatile("ud2");   // Undefined Instruction
```

`ud2` 是一条 x86 专用指令——它的唯一目的就是触发 **#UD 异常（向量 6，Invalid Opcode）**。

运行流程：

```
kernel_main()
  ├─ printf("Hello...")           ← 正常输出
  ├─ idt_init()                   ← 加载 IDT
  ├─ printf("Triggering #UD...")  ← 提示
  ├─ ud2                          ← 触发异常
  │
  ▼
CPU 查到 IDT[6] → 跳转到 handler_6
  │
  ▼
trampoline 压入: vec=6, error=0
  │
  ▼
handler_common 保存寄存器 → 调用 handle_exception(6, 0, &frame)
  │
  ▼
printf("!!! CPU EXCEPTION\n")
printf("Vector: 06 (Invalid Opcode)\n")
printf("EIP: 0000xxxx\n")
printf("System halted.\n")
  │
  ▼
hlt 死循环
```

预期输出：
```
Hello from 32-bit kernel!
char: X  str: works
dec: 42  hex: 0xdead
percent: %
IDT loaded.  Triggering #UD...
!!! CPU EXCEPTION
Vector: 06 (Invalid Opcode)
EIP: 0000B0xx
System halted.
```

输出 `EIP` 的那个地址可以反汇编来确认 `ud2` 的位置：
```bash
objdump -d build/stage3.elf | grep -A2 ud2
```

---

## 六、代码架构总结

```
my-kernel/
├── include/
│   ├── types.h                类型定义（u8, u16, u32）
│   ├── vga.h                  VGA 屏幕尺寸常量（80×25）
│   ├── cursor_coordinates.h   光标结构体
│   ├── putchar.h              putchar / scroll_screen 声明
│   ├── printf.h               printf 声明
│   ├── interrupt_frame.h      中断帧结构体
│   └── idt.h                  IDT 条目 / 指针结构体 + 常量
│
└── kernel/
    ├── putchar.c              VGA 文本输出 + 滚屏
    ├── printf.c               格式化输出（%c, %s, %d, %x, %%）
    ├── idt.c                  IDT 初始化 + 异常处理 C 函数
    └── idt_handlers.S         256 个 trampoline + handler_common + handler_addrs 表
```

数据流向：
```
printf("...")
    ↓ putchar(vga, &cursor, c)
    ↓ vga[index] = 0x0700 | c
    ↓ 物理地址 0xB8000     → VGA 显卡 → 显示器
```

中断处理：
```
CPU 触发中断/异常
    ↓ 查 IDT（lidt 加载到 IDTR）
    ↓ trampoline（压 vec/error → jmp handler_common）
    ↓ 保存寄存器 → call handle_exception(vec, err, &frame)
    ↓ printf 输出异常信息 → hlt
```

---

## 七、与之前 stage 的关系

```
stage-1:  实模式，BIOS int 0x10 输出          ← 依赖 BIOS
stage-2:  32 位保护模式，直接写 VGA 显存       ← 脱离 BIOS
stage-3:  32 位保护模式 + printf + IDT 框架    ← 当前
            ├─ printf：可格式化输出，支持 %d %x %s
            └─ IDT：捕获异常，打印现场，然后停机
```

printf 和 IDT 的结合非常关键：**没有 printf 时，异常只能靠 QEMU 的 triple fault 重启来感知；有了 printf，异常发生时可以直接看到"在哪、为什么"。**

这也符合 CLAUDE.md 中说的"调试优先"——写任何功能前先确保有办法看它的输出。

---

## 八、术语表

| 术语 | 解释 |
|------|------|
| VGA 文本模式 | 80×25 字符的文本显示模式，显存在 0xB8000 |
| 属性字节 | 每个字符的高字节，定义前景色 + 背景色（0x07 = 黑底亮灰）|
| 滚屏 | 屏幕满时上移一行 |
| IDT | Interrupt Descriptor Table，保护模式的中断向量表 |
| Trampoline | 每个中断号独立的汇编入口，压入编号后跳公共 handler |
| 错误码 | CPU 自动压入的异常详细信息（仅 7 种异常有）|
| 中断门 | Type=0xE，进入时自动关中断 |
| #UD | Invalid Opcode 异常（向量 6），`ud2` 指令故意触发 |
| cdecl | C 调用约定：参数从右往左传，caller 清理栈 |
