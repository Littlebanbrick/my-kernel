# 08 — Ring 3 用户态实现笔记

## 概述

从零搭建了 x86 保护模式下的用户态执行环境（ring 3）。实现了 GDT + TSS + IDT 配置、页表权限设置、iret 跳转和系统调用。

---

## 一句话总结

> **GDT** 定义段的属性（DPL、类型），CPU 用 **CS.CPL**（加载 CS 时从 GDT 读来的）判断能不能执行特权指令。
> **页表 U/S 位**判断能不能访问某个物理页。
> **TSS** 只在 ring 切换时告诉 CPU"用哪个地址做安全栈"，换完就没它的事了。
> GDT 和页表是两套独立的权限体系，**都要过才能访问内存**。

## Ring 级别的本质

Ring 级别存在 **CS 寄存器**的底 2 位（CPL, Current Privilege Level）：

```
CS = 0x0008  →  CPL = 0  →  ring 0（内核）
CS = 0x001B  →  CPL = 3  →  ring 3（用户）
```

Ring 是 **CPU 的全局状态**——一个时刻 CPU 只能在一个 ring 里。切换 ring 的唯一途径是硬件自动完成：

```
ring 3 → 0:  int $0x80 / 中断 / 异常
              CPU 查 IDT → 取内核 CS（DPL=0）→ 从 TSS 读内核栈 → 切换

ring 0 → 3:  iret
              CPU 从栈上弹出用户 CS（RPL=3）→ 切换 CPL
```

---

## 涉及的硬件机制

### GDT（Global Descriptor Table）

定义段的**属性**（DPL、类型、base、limit）。CPU 通过段选择子（如 0x08）查 GDT 获取段信息。

⚠️ **常见误解**：GDT 存的是段的**属性**，不是段的"地址"。在平坦模型下所有段的 base=0, limit=4GB，段基址毫无意义。GDT 真正有用的是 **DPL**（告诉 CPU 这个段是 ring 0 还是 ring 3）和 **类型**（代码还是数据）。

```
GDT 布局（在 ring3.c 中统一定义）：
  [0] null           sel 0x00
  [1] kernel code    sel 0x08    access=0x9A  DPL=0, 代码, 可读
  [2] kernel data    sel 0x10    access=0x92  DPL=0, 数据, 可写
  [3] user code      sel 0x18    access=0xFA  DPL=3, 代码, 可读
  [4] user data      sel 0x20    access=0xF2  DPL=3, 数据, 可写
  [5] TSS            sel 0x28    access=0x89  DPL=0, TSS32-available
```

access byte 的位含义：
- 位 7: P（Present）
- 位 6-5: **DPL**（ring 级别，最重要的字段）
- 位 4: S（0=系统段, 1=代码/数据段）
- 位 3-0: 类型（代码/数据/可读/可写）

### TSS（Task State Segment）

CPU 在 ring 3→0 切换时**强制换栈**——这是硬性规则，不是可选优化。原因：如果不换栈，用户设 `esp=内核敏感地址` 然后 `int $0x80`，CPU 会把返回地址压到用户指定的位置，内核数据就能被用户篡改。

换栈时用的栈地址从 TSS 读取。TSS 只存了两样东西（其余 90 多字节全是摆设）：

```c
struct tss {
    // ... 很多用不到的字段 ...
    u32 esp0;          // 0x04  ← ★ 内核栈指针
    u32 ss0;           // 0x08  ← ★ 内核栈段选择子（平坦模型下固定 0x10）
    // ...
};
```

⚠️ **TSS 不存代码段（CS）**。切到 ring 0 后执行哪段代码由 **IDT 表项里的 selector** 决定，不需要 TSS。TSS 只回答一个问题：**"切栈时用哪个地址？"**

还需要注意：`ltr` 指令加载 TSS 必须在实际使用前执行（告诉 CPU 你的 TSS 结构体在哪）。如果不 `ltr`，Task Register 无效，`int $0x80` 从 ring 3 陷入时会触发 #TS（Invalid TSS）→ 系统崩溃。

### 代码权限的判断（易混淆）

**代码本身没有 ring 级别。** 同一段字节码，在不同 CPL 下执行结果不同：

```
ring 3 时 (CS=0x001B):
  从 0x400000 取指令 → 页表 PAGE_USER=1 → OK
  mov eax, 'Y' → 通用指令 → OK
  int $0x80    → IDT[0x80] 的 DPL=3 → OK
  cli          → CPU 查表："cli 只能在 CPL≤0 执行" → 当前 CPL=3 → #GP！

ring 0 时 (CS=0x0008):
  执行同一段 cli → CPU 查表：当前 CPL=0 → OK！
```

CPU 的每条指令都有隐含的"权限要求"（`cli`/`hlt`/写 CR3/写 MSR 等只能 ring 0），CPU 执行前对比 **CS.CPL** 和指令要求，不匹配就触发 #GP。

### IDT（Interrupt Descriptor Table）

定义中断/异常/系统调用的入口。每个条目有：

- handler 地址
- handler 运行在哪个段（CS 选择子）
- DPL：什么 ring 级别允许 `int` 触发这个入口

```c
#define IDT_KERN_INT  0x8E   // DPL=0, 仅内核可触发
#define IDT_USER_INT  0xEE   // DPL=3, ring 3 可触发
```

### 页表 U/S 位

页表条目的 bit 2 控制页面是否允许 ring 3 访问。CPU 翻译每个虚拟地址时都检查两级（PDE + PTE）的 U/S 位：

```
CPL=3 访问 0x400000：
  → PDE[1] 有 PAGE_USER → OK
  → PTE[0] 有 PAGE_USER → OK
  → 允许访问

CPL=3 访问 0xB8000（VGA framebuffer，无 PAGE_USER）：
  → PDE[0] 没有 PAGE_USER → #PF（Page Fault）！
```

**我们的代码中在哪里设的：**

```c
// kernel.c — 用户代码/栈页：明确加了 PAGE_USER
pt[ptx] = PAGE_ENTRY(ring3_page, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
//                                                          ^^^^^^^^^^

// paging.c — 内核的身份映射（0~4MB）：没有 PAGE_USER
pt[ptx] = PAGE_ENTRY(virt, PAGE_PRESENT | PAGE_WRITE);
//                          ^^^^^^^^^^^^^^^^^^^^^^^^
//                          ring 3 访问这些页面会 #PF
```

这就是为什么 ring 3 程序只能访问 `0x400000`（代码）和 `0x500000`（栈）——**我们在页表里只给这两页开了 U/S 权限**。写 VGA 显存、读内核数据都会触发 #PF。

注意：PDE 也必须设置 PAGE_USER。`map_page` 新分配页表时 PDE 默认只有 PRESENT|WRITE，需要手动加 USER：

```c
kernel_page_dir[pdx] |= PAGE_USER;
```

---

## 实现细节

### ring3.c — 基础设施

```c
void ring3_init_gdt_tss(u32 kstack_top)
```

- 在 C 中定义 6 条目 GDT（不依赖 boot.S 的 GDT）
- 使用 `ring3_gdt[6]`（.bss 数组）
- 加载 GDT → 刷新段寄存器 → `ltr`
- TSS 只设 `ss0=0x10`（内核数据段）和 `esp0=kstack_top`

```c
void ring3_jump(u32 eip, u32 stack_top)
```

- 构造 5 元素数组：EIP, CS(RPL=3), EFLAGS(IF=1), ESP(user), SS(RPL=3)
- `cli; mov %0, %%esp; iret` 切换到 ring 3

### kernel.c — 实验

```c
run_ring3_experiment()
```

1. 调 `ring3_init_gdt_tss()` → 桩 GDT/TSS
2. 申请/寻找 PDX=1 的页表，设 `PAGE_USER`
3. `map_page` 把 `ring3_page` 映射到 0x400000（代码）和 0x500000（栈）
4. 写机器码：`mov eax, 'Y'; int $0x80; cli`
5. `ring3_jump(0x400000, 0x501000)`

### idt.c — 系统调用门

```c
idt_set_entry(&idt[0x80], handler_addrs[0x80], IDT_USER_INT);
```

- int 0x80 的 handler 使用现有的 `handle_exception()`
- `vec == 0x80` 时打印消息并返回（不 halt）
- 其余异常走"!!! CPU EXCEPTION"流程并 halt

---

## 测试验证

```
ring3: setting up GDT and TSS...
ring3: GDT+TSS loaded, kernel stack at 0x12000
ring3: jumping to user code at 0x400000...
  ring3 syscall: returning to user code.    ← syscall 成功（ring 3 运行中）
!!! CPU EXCEPTION                           ← cli 被 CPU 拦截
Vector: 0x0d (General Protection Fault)
EIP: 0x00400007
Err:  0x0
System halted.
```

| 事件 | 证明 |
|------|------|
| `int $0x80` 成功返回 | ✅ 成功进入 ring 3，syscall 正常工作 |
| `cli` 触发 #GP | ✅ ring 3 保护生效，特权指令被拒绝 |

---

## 实现中的坑

1. **IDT 的 DPL**：必须单独设置 int 0x80 的 DPL=3，例外默认 DPL=0
2. **PDE 的 U/S**：不仅 PTE 需要 PAGE_USER，PDE 也需要。`map_page` 新分配页表时 PDE 硬编码没有 PAGE_USER，需手动添加
3. **`ltr` 前必须设置 TSS**：先填 `ss0/esp0`，再 `ltr`，否则 Task Register 无效
4. **GDT 加载后刷新段寄存器**：`lgdt` 只是改 GDTR，CS/DS 的缓存还在引用旧 GDT
5. **不同编译单元引用 kernel_page_dir**：需要去掉 `static` 并在 header 加 `extern`
6. **`iret` 的栈帧布局**：顺序是 EIP → CS → EFLAGS → ESP → SS（由低到高）
7. **TSS 只存 ss0/esp0**：不存代码段。代码段由 IDT 条目的 selector 提供
8. **ring3_page 既是内核栈又是用户代码页**：同一物理页映射在两个虚拟地址。内核栈从顶往下用，用户代码从底开始，4096 字节足够

---

## 和 Linux 内核的对应

| 概念 | 这里 | Linux |
|------|------|-------|
| GDT 加载 | C 代码静态数组 | `arch/x86/kernel/cpu/common.c` |
| TSS | 静态结构体，`ltr` 加载 | 每个 CPU 一个 `per_cpu` TSS |
| 系统调用 | int 0x80 | `syscall` 指令（64 位）或 `int 0x80`（32 位） |
| 用户栈 | 同一物理页映射两次 | `vm_area_struct` 管理的独立映射 |
| 特权指令拦截 | #GP handler halt | 发送 `SIGSEGV` 给进程 |
| 段级别 | 用 DPL=0/3 | (64 位) 基本不用，只剩 CS 和 SS |

---

## 术语速查

| 缩写 | 全称 | 作用 |
|------|------|------|
| CPL | Current Privilege Level | CS 底 2 位，表示 CPU 当前在 ring 几 |
| DPL | Descriptor Privilege Level | 段或门的属性，表示"谁有权访问" |
| RPL | Requested Privilege Level | 段选择子底 2 位，用于权限检查 |
| GDT | Global Descriptor Table | 定义所有段的属性（base/limit/DPL） |
| TSS | Task State Segment | 存 ring 3→0 切换用的内核栈地址 |
| #GP | General Protection Fault | 特权违规时触发的中断（向量 13） |
| `ltr` | Load Task Register | 加载 TSS 到 Task Register |
| `lgdt` | Load GDT | 加载 GDT 到 GDTR |
