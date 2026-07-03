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

## 九、PIC 重映射——让硬件中断不跟异常打架

### 为什么需要 PIC

PIC（Programmable Interrupt Controller，8259A 芯片）是 PC 上管理硬件中断的老牌芯片。16 条 IRQ 线连接着键盘、定时器、硬盘等外设。

**核心矛盾**：PIC 出厂默认把 IRQ 0-15 映射到 IDT[0-15]，**和 CPU 异常向量完全重叠**：

```
IRQ 0  →  IDT[0]    冲突 →  #DE (除零异常)
IRQ 1  →  IDT[1]    冲突 →  #DB (调试异常)
...
IRQ 7  →  IDT[7]    冲突 →  #NM (设备不可用)
```

如果不开中断（不执行 `sti`），这个冲突不会暴露——PIC 的 IRQ 信号被 CPU 挡在门外。一旦开中断，键盘按一下就会触发"除零异常"。

### 解决：重映射到 IDT[32-47]

PIC 是可编程的——通过 I/O 端口发 4 轮初始化命令字（ICW1-4），可以告诉它"IRQ 0 走 IDT[32]"：

```c
void pic_remap(void)
{
    /* ICW1: 开始初始化 */
    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);

    /* ICW2: 设置基地址 */
    outb(PIC1_DATA, IRQ_BASE);          /* IRQ 0-7  → IDT[32-39] */
    outb(PIC2_DATA, IRQ_BASE + 8);       /* IRQ 8-15 → IDT[40-47] */

    /* ICW3: 级联配置——从 PIC 挂在主 PIC 的 IRQ 2 */
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    /* ICW4: 80x86 模式，非缓冲，正常 EOI */
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    /* 屏蔽所有 IRQ——后面逐个放开 */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}
```

PIC 通过 **I/O 端口** 编程（`outb` 指令），不是内存映射。主 PIC 端口 0x20/0x21，从 PIC 端口 0xA0/0xA1。

### IMR——中断屏蔽寄存器

PIC 内部有一个 8 位 IMR（Interrupt Mask Register），每位对应一个 IRQ：

```
bit:  7     6     5     4     3     2     1     0
     IRQ7  IRQ6  IRQ5  IRQ4  IRQ3  IRQ2  IRQ1  IRQ0
```

- 写 `1` → 屏蔽（忽略这个 IRQ）
- 写 `0` → 放行（允许发给 CPU）

`pic_remap` 最后设 IMR = `0xFF` 全屏蔽，是安全的做法——等写了对应 handler 再逐个取消屏蔽。

### EOI——End of Interrupt

中断 handler 处理完后必须通知 PIC"我搞完了"，否则 PIC 不会处理后续中断：

```c
void pic_send_eoi(unsigned char irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);    /* 从片也要发 */
    outb(PIC1_CMD, PIC_EOI);        /* 最后总是发主片 */
}
```
为什么 IRQ >= 8 要从片也发？因为从 PIC 的 IRQ 是通过级联线（主 PIC 的 IRQ 2）送到 CPU 的，不清从片的话从片 ISR 寄存器不释放，后续 IRQ 8-15 全堵住。

### handle_exception 中的 IRQ 分发

我们修改了 `handle_exception`，让它在识别到向量属于 [32, 47] 时走 IRQ 路径而不是异常路径：

```c
void handle_exception(u32 vec, u32 error_code, struct interrupt_frame *frame)
{
    if (vec >= IRQ_BASE && vec < IRQ_BASE + 16) {
        if (vec == IRQ0_VECTOR)
            g_ticks++;
        else if (vec == IRQ1_VECTOR)
            kbd_display_scancode(inb(KBD_DATA_PORT));
        pic_send_eoi(vec - IRQ_BASE);
        return;     /* ← 关键：不 halt，正常返回 → iretd */
    }
    /* ... 异常处理（打印 + halt）... */
}
```

**为什么能正常返回？** 汇编 `handler_common` 在调用完 `handle_exception` 后会恢复寄存器、弹栈、执行 `iretd`——只要 handler 不卡死在 `while(1) hlt` 里，中断就完整返回。`pic_send_eoi` 在 `return` 前执行，保证了 EOI 先于 `iretd`。

---

## 十、PIT 定时器——用硬件中断验证整条链路

### PIT 是什么

PIT（Programmable Interval Timer，Intel 8253/8254）是 PC 上最古老也最可靠的计时芯片。它挂在 **IRQ 0** 上，默认以 **~18.2 Hz**（每 ~55ms）的频率产生中断。

PIT 的默认频率在 BIOS 初始化时就已经设好了——我们什么都不用配就能收到中断，非常适合用来验证硬件中断链路。

### 验证方案

思路：开一个全局计数器 `g_ticks`，每次 IRQ 0 触发时 +1。主循环里更新屏幕显示：

```c
/* kernel.c */
volatile u32 g_ticks;              /* 中断 handler 会改这个值 */

void kernel_main(void)
{
    /* ... 之前的初始化 ... */
    idt_init();
    pic_remap();

    /* 取消屏蔽 PIT (IRQ 0) */
    outb(PIC1_DATA, inb(PIC1_DATA) & ~1);

    /* 写 "Ticks:" 标签到屏幕第 8 行 */
    for (i = 0; "Ticks: "[i]; i++)
        vga[8 * MAX_WIDTH + i] = 0x0700 | "Ticks: "[i];

    __asm__ volatile("sti");       /* 开中断——PIT 开始发 IRQ 0 */

    while (1) {
        __asm__ volatile("hlt");   /* 等待下一个中断 */
        vga_write_dec_at(8, 7, g_ticks);   /* 更新显示 */
    }
}
```

### `hlt` 指令

`hlt`（halt）让 CPU 停止执行，进入低功耗待机状态，直到下一个中断来才醒来继续：

```
sti;            ← 开中断
hlt;            ← 睡。PIT 55ms 后发 IRQ 0
                → handler: g_ticks++ → EOI → 返回
vga_write...    ← 醒来，更新数字
→ hlt;          ← 又睡，等下一次
```

这也是 Linux 内核 idle 循环的原理。

### 为什么不在中断 handler 里用 printf

printf 内部有一个 `static cursor_coordinates` 跟踪光标位置。如果中断 handler 调用 printf，而 main loop 正好也在 printf 里，两者抢同一个光标——输出位置错乱。

所以 handler 里我们用 **直接 VGA 写**（`vga_write_dec_at`），不碰任何共享状态：

```c
static void vga_write_dec_at(int row, int col, u32 val)
{
    u16 *const vga = (u16 *)0xB8000;
    char buf[10];
    int i = 0;
    int pos = row * MAX_WIDTH + col;

    do {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    } while (val > 0);

    for (j = 0; j < 6 - i; j++) vga[pos++] = 0x0700 | ' ';
    for (j = i - 1; j >= 0; j--) vga[pos++] = 0x0700 | buf[j];
}
```

这里的行/列是硬编码的，不需要光标状态，完全可重入。

### 运行效果

```
Row 8: Ticks: 000142      ← 数字不断增长，约 18 次/秒
```

看到这个数字在动，就证明：**PIC → IDT → trampoline → C handler → EOI → 返回** 整条链路是通的。

---

## 十一、键盘驱动

### v1 — 显示 scancode 原始值

键盘按下一个键时，PS/2 控制器把一个 **scancode**（扫描码）放到 I/O 端口 `0x60` 里，然后触发 IRQ 1。我们的 handler 读出来直接用十六进制显示：

```c
#define KBD_DATA_PORT   0x60

static void kbd_display_scancode(u8 scancode)
{
    u16 *const vga = (u16 *)0xB8000;
    int base = 9 * MAX_WIDTH;

    vga[base]     = 0x0700 | 'K';
    vga[base + 1] = 0x0700 | 'e';
    /* ... "Key: 0xNN" ... */
    vga[base + 7] = 0x0700 | hex[(scancode >> 4) & 0xF];
    vga[base + 8] = 0x0700 | hex[scancode & 0xF];
}
```

### scancode 的规律

键盘不传"字符"，只传"位置号"（scancode）。同一个位置在不同键盘布局下对应不同字符，所以翻译是操作系统的责任。

| 键 | 按下（make） | 松开（break） |
|----|------------|-------------|
| A | `0x1E` | `0x9E`（= make + 0x80）|
| B | `0x30` | `0xB0` |
| 空格 | `0x39` | `0xB9` |
| 左 Shift | `0x2A` | `0xAA` |

### v2 — 翻译成 ASCII

用一张 128 项的查找表，scancode 作索引，取到对应字符：

```c
static const char s2a[128] = {
    [0x02] = '1', [0x03] = '2', ..., [0x0B] = '0',
    [0x10] = 'q', [0x11] = 'w', ..., [0x19] = 'p',
    [0x1E] = 'a', [0x1F] = 's', ..., [0x26] = 'l',
    [0x2C] = 'z', ..., [0x32] = 'm',
    [0x39] = ' ',           /* Space */
    /* 没列出来的 = 0 → 显示 "others" */
};

u8 idx = scancode & 0x7F;     /* 去掉 break 标志位 */
char ch = s2a[idx];
```

- 能翻译成字符的：显示字符（a-z, 0-9, 空格、标点）
- 不能翻译的（F1、方向键、Ctrl 等）：显示 `others`
- 按下和松开都触发输出，scancode 十六进制显示始终保留

### v3 — Shift 支持

Shift 键本身也是普通按键——按下发 `0x2A`/`0x36`，松开发 `0xAA`/`0xB6`。加一个 `shift` 状态标记：

```c
static int shift = 0;

/* 检测 Shift 键，更新状态，不输出字符 */
if (idx == 0x2A || idx == 0x36) {
    shift = !(scancode & 0x80);    /* make → 1, break → 0 */
    return;
}

/* 根据 shift 状态选表 */
const char *table = shift ? s2a_shift : s2a;
char ch = table[idx];
```

`shift` 表把字母变了大写，数字行变符号（`1` → `!`），标点变对应上档符号（`;` → `:`）。

---

## 十二、完整的代码架构

```
my-kernel/
├── include/
│   ├── types.h                u8/u16/u32 等基础类型
│   ├── vga.h                  VGA 尺寸常量 (80×25)
│   ├── cursor_coordinates.h   光标结构体
│   ├── putchar.h              putchar / scroll_screen 声明
│   ├── printf.h               printf 声明
│   ├── interrupt_frame.h      中断帧结构体
│   ├── idt.h                  IDT 条目/指针结构体
│   ├── pic.h                  PIC 端口/IRQ 向量常量
│   └── utils.h                outb / inb I/O 封装
│
└── kernel/
    ├── putchar.c              VGA 文本输出 + 滚屏
    ├── printf.c               格式化输出
    ├── idt.c                  IDT 初始化 + 统一中断入口
    ├── idt_handlers.S         256 trampoline + handler_common
    └── pic.c                  PIC 重映射 + EOI

stage-3-protected-mode/
├── boot.S                     引导扇区 (512B)
├── stage3.S                   32 位保护模式入口
├── kernel.c                   主函数: printf → IDT → PIC → PIT → 键盘
├── linker.ld                  链接脚本
└── Makefile                   构建系统
```

中断处理的全流程：

```
外设发 IRQ
    ↓
PIC 检查 IMR → 通知 CPU (INTR 引脚)
    ↓
CPU 查 IDT[offset] → 跳到对应的 trampoline
    ↓
trampoline 压入 vec + error → jmp handler_common
    ↓
handler_common 保存寄存器 → call handle_exception(vec, err, &frame)
    ↓
handle_exception:
  ├─ IRQ (vec 32-47): 做具体工作 → pic_send_eoi → return
  └─ 异常 (vec 0-31): printf 打印 → hlt 死机
    ↓
handler_common 恢复寄存器 → iretd
    ↓
返回被中断的代码继续执行
```

---

## 十三、术语表

| 术语 | 解释 |
|------|------|
| PIC | 可编程中断控制器，管理 16 条 IRQ 线 |
| IRQ | Interrupt ReQuest，硬件外设发的中断信号 |
| IMR | Interrupt Mask Register，PIC 内部的屏蔽寄存器 |
| ICW | Initialization Command Word，配置 PIC 的四轮命令 |
| EOI | End of Interrupt，通知 PIC 处理完成 |
| PIT | 可编程间隔定时器，IRQ 0，默认 ~18.2 Hz |
| Scancode | 键盘传的"位置号"而不是字符 |
| Make/Break | 按键按下/松开对应的 scancode |
| 可重入 (re-entrant) | 函数能否被中断后再次进入而不出错 |
| hlt | CPU 停机指令，等待下一个中断唤醒 |
| `outb` | 向 I/O 端口写一个字节的 C 封装（内联汇编）|
| `inb` | 从 I/O 端口读一个字节 |
