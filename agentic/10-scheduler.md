# 10 — 进程调度器（Preemptive Round-Robin，ring 0）

## 概述

从"CPU 只跑一个程序"推进到"两个进程轮流跑"。实现了一个最小的抢占式调度器：PIT 时钟中断触发切换，两个 ring 0 进程 round-robin 轮转。最终效果：

```
A: 0
A: 1
B: hello
A: 2
B: hello
...
```

不需要任何进程主动 `yield()`，全靠硬件时钟抢占。

---

## 核心问题：什么是"上下文切换"？

**一句话：换栈 + 恢复寄存器。**

CPU 跑一个进程时，它的全部状态就是：
- 8 个通用寄存器（eax/ebx/ecx/edx/esi/edi/ebp/esp）
- EIP（下一条要执行的指令地址）
- EFLAGS（标志位）

只要把这些值完整保存下来，之后再原样恢复，进程就"不知道"自己被中断过——它会从上次停下的地方继续跑，所有寄存器和原来一模一样。

所以"切换进程"= **把当前寄存器存进当前进程的 PCB，从下一个进程的 PCB 把它的寄存器读回来**。寄存器存在哪儿？存在那个进程自己的栈上。所以本质上：

> **切换进程 = 切换 ESP（换到另一个进程的栈）+ popa + iret**

栈一换，`popa` 弹出来的就是另一个进程上次压进去的寄存器，`iret` 弹出来的 EIP 就是它当时执行到的地方。

---

## 关键设计：伪造一个"刚被中断的进程"

### 难题

正常切换流程是：IRQ0 来 → `pusha` 保存寄存器 → 选下一个进程 → `popa` 恢复。但这要求下一个进程**之前也被这样保存过**。

可是**新进程从来没跑过**，它没有"上次保存的寄存器"可以恢复。怎么办？

### 解法

**手动在新进程的栈上伪造一个"刚被中断"的样子。** 让它的栈看起来就像它曾经被 IRQ0 打断过一样：

```
进程栈顶（saved_sp 指向这里）
┌──────────┐
│ edi = 0  │  ← popa 弹出的第一个
├──────────┤
│ esi = 0  │
├──────────┤
│ ebp = 0  │
├──────────┤
│ esp = …  │  （popa 会丢弃这个，不重要）
├──────────┤
│ ebx = 0  │
├──────────┤
│ edx = 0  │
├──────────┤
│ ecx = 0  │
├──────────┤
│ eax = 0  │  ← popa 弹出的最后一个
├──────────┤
│ eip = 进程入口地址   │  ← iret 弹出，跳到这里开始跑
├──────────┤
│ cs  = 0x08           │  ← iret 弹出（内核代码段，ring 0）
├──────────┤
│ eflags = 0x202       │  ← iret 弹出（IF=1，中断保持开启）
└──────────┘
```

这样 `popa; iret` 之后，CPU 就跳到进程入口地址，所有寄存器从 0 开始。**新进程和"被切回来的老进程"走完全一样的恢复路径**，不需要特殊分支。

这就是 `create_process()` 干的事：分配一页栈，在栈顶摆好这个伪造的 `cpu_state`，把 `saved_sp` 记下来。

---

## 数据结构

### `struct cpu_state` —— 保存的寄存器集

按 `pusha` 压栈的**栈上布局**（最低地址在最前）排列：

```c
struct cpu_state {
    u32 edi;    // pusha 最后压的（最低地址）
    u32 esi;
    u32 ebp;
    u32 esp;    // pusha 自己存的"压栈前的 ESP"，popa 会丢弃
    u32 ebx;
    u32 edx;
    u32 ecx;
    u32 eax;    // pusha 最先压的（最高地址）

    u32 eip;    // CPU 中断时压的（iret 弹出）
    u32 cs;
    u32 eflags;
} __attribute__((packed));
```

**易错点**：结构体字段顺序必须和 `pusha` 在栈上的实际顺序一致，否则 `popa` 弹出来的值会错位。`pusha` 压栈顺序是 `EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI`（EAX 先压，所以在最高地址；EDI 最后压，在最低地址 = ESP 指向的地方）。栈是从高往低长的，所以结构体从低地址往高地址读就是 `EDI,ESI,EBP,ESP,EBX,EDX,ECX,EAX`。

### `struct pcb` —— 进程控制块

```c
struct pcb {
    int  used;       // 这个槽位在用吗
    int  pid;
    char name[8];
    u8  *stack;      // 分配的栈基址（用于将来释放）
    u32  saved_sp;   // ★ 关键：指向这个进程的 cpu_state
};
```

**只存一个 `saved_sp`**，不存完整寄存器快照——因为寄存器内容已经在进程自己的栈上了，`saved_sp` 指过去就行。这是 Linux 也用的思路（task_struct 里的 `sp` 字段）。

---

## 切换流程（端到端）

### 第一次切换：`sched_start() → do_first_switch()`

```asm
do_first_switch:
    mov 4(%esp), %esp   ; arg1 = 第一个进程的 saved_sp
    popa                ; 恢复 8 个通用寄存器
    iret                ; 弹出 EIP/CS/EFLAGS → 跳进进程入口
```

`sched_start` 选好第一个进程，把它的 `saved_sp` 传进来，`do_first_switch` 加载这个 ESP，`popa; iret` 直接进进程。**从此再也不返回 kernel_main。**

### 后续切换：IRQ 0 中断

PIT 每 10ms 触发 IRQ 0，CPU 跳到 `irq0_handler`：

```asm
irq0_handler:
    pusha               ; ① 保存当前进程的 8 个寄存器到它的栈
    push %esp           ; ② 把"保存后的 ESP"作为参数传给 C
    call irq0_enter     ; ③ C 函数：选下一个进程
    add  $4, %esp       ;   清理参数
    mov  %eax, %esp     ; ④ 切到 C 函数返回的新 ESP
    popa                ; ⑤ 恢复下一个进程的寄存器
    iret                ; ⑥ 跳到下一个进程的 EIP
```

C 部分 `irq0_enter(saved_sp)`：

```c
u32 irq0_enter(u32 saved_sp) {
    if (current_pid >= 0)
        procs[current_pid].saved_sp = saved_sp;  // 存当前进程的栈顶
    next_pid = pick_next();                      // round-robin
    pic_send_eoi(0);                             // 必须 EOI，否则下次中断不来
    if (next_pid == current_pid) return saved_sp; // 没人可切就回去
    current_pid = next_pid;
    return procs[current_pid].saved_sp;           // 返回新进程的栈顶
}
```

**关键点：`irq0_enter` 返回 ESP 给汇编**。汇编用这个返回值切换 ESP。这样 C 负责调度决策，汇编负责实际的栈切换，分工清晰。

### `iret` 在 ring 0 下的行为

我们的 `iret` 弹出 `EIP/CS/EFLAGS`。因为全程 ring 0（CPL 不变），`iret` **不弹 ESP/SS**——同级中断返回只弹 3 个值。这和 ring 3 切换（要弹 5 个值）不一样，简单很多。这也是第一版全程 ring 0 的好处。

---

## 用到的硬件机制

### PIT（8253/8254 定时器）

- 通道 0 接到 PIC 的 IRQ 0 引脚
- 输入时钟固定 1193182 Hz
- `pit_init(hz)` 设置 reload 值，IRQ 0 频率 = 1193182 / reload
- mode 2（rate generator）：周期性脉冲
- 我们用 100 Hz，每 10ms 一次中断

### PIC（8259A 中断控制器）

- `pic_remap()` 已经把 IRQ 0~15 重映射到 IDT[32~47]
- `pic_remap` 结束时**屏蔽了所有 IRQ**，需要单独 unmask
- 新加的 `pic_unmask_irq(0)` 清掉 IRQ 0 的 mask 位，让 PIT 中断能到达 CPU
- 每次 `irq0_enter` 必须调 `pic_send_eoi(0)`——**不 EOI 的话 PIC 不会再发下一次中断**，调度器就卡死了

### IDT 里的"专用 IRQ 0 入口"

复用原来的 `handler_common` 路径不行——它最后是 `iret` 回原地，没法切换栈。所以装了一个**专用的 `irq0_handler`** 到 IDT[32]，绕过通用 trampoline：

```c
idt_set_entry(&idt[IRQ0_VECTOR], irq0_handler, IDT_KERN_INT);
```

`handle_exception` 里也去掉了 `IRQ0_VECTOR` 的分支（IRQ 0 不再走它）。

---

## 设计权衡

### 为什么第一版全程 ring 0？

- 不用碰 TSS、不用强制换栈、不用页表隔离
- 上下文切换就是纯寄存器保存/恢复
- 先把"调度"这个核心机制跑通，不和权限搅在一起
- ring 3 进程以后再加（需要 TSS 的 esp0、每个进程独立页目录、`iret` 弹 5 个值）

### 为什么用 `pusha`/`popa` 而不是手动 push 每个寄存器？

- 一条指令保存/恢复 8 个通用寄存器，简洁
- 布局固定，`struct cpu_state` 可以精确匹配
- 缺点：`pusha` 会压一个无用的 ESP 占位（popa 时丢弃），浪费 4 字节，无所谓

### 为什么 `saved_sp` 而不是保存完整寄存器到 PCB？

- 寄存器内容已经在进程自己的栈上了，不需要在 PCB 里再存一份
- PCB 只记一个指针，更省内存
- 和 Linux 的 `thread_struct.sp` 同思路

---

## 验证过程（QEMU + GDB）

### 1. 构建无错误

`make` 通过，只剩无害的 RWX 段警告（freestanding 内核正常）。

### 2. PIT 确实在触发

在 `irq0_handler`（0xc960）下断点，`continue` 后能反复命中——说明 PIT 中断正常送达，PIC unmask 生效。

### 3. `current_pid` 在 0/1 之间交替

```
IRQ0 #1: current_pid = 0
IRQ0 #2: current_pid = 1
IRQ0 #3: current_pid = 0
IRQ0 #4: current_pid = 1
```

`pick_next()` 的 round-robin 工作正常。

### 4. 进程 A 的计数器跨切换递增

在 `process_a` 的 `printf` 调用点下断点，每次读 `[ebp-0xc]`（变量 `i`）：

```
A prints with i = 0
A prints with i = 1
A prints with i = 2
A prints with i = 3
```

**这是最关键的证据**：`i` 是 `process_a` 的局部变量，存在它的栈上。如果切换回来时 ESP/EBP 没对齐，`i` 就会变成乱码或归零。`i` 持续递增 = 寄存器和栈都被正确保存恢复。

### 5. 第一次切换的栈帧布局正确

单步进入第一次 IRQ0，`pusha` 之后栈上是：

```
ESP+0  : edi, esi, ebp, esp, ebx, edx, ecx, eax   ← pusha 压的
ESP+32 : EIP=b70a, CS=0x08, EFLAGS=0x293           ← CPU 压的
```

EIP 指向 `process_a` 的 spin 循环里——正是被中断的位置。布局和 `struct cpu_state` 一致。

### 6. 30 秒稳定运行

跑 30 秒无 triple fault、无 reset，QEMU 进程一直存活。屏幕上持续出现 A 和 B 的输出。

---

## 文件清单

| 文件 | 作用 |
|------|------|
| `my-kernel/include/sched.h` | PCB / cpu_state 结构体、API 声明 |
| `my-kernel/kernel/sched.c` | 进程表、create_process、irq0_enter、sched_start |
| `my-kernel/include/pit.h` | PIT 端口和 API |
| `my-kernel/kernel/pit.c` | pit_init（mode 2，channel 0） |
| `my-kernel/kernel/idt_handlers.S` | 新增 `do_first_switch` / `irq0_handler` |
| `my-kernel/kernel/idt.c` | 装 irq0_handler 到 IDT[32] |
| `my-kernel/kernel/pic.c` | 新增 `pic_unmask_irq` / `pic_mask_irq` |
| `stage-3-protected-mode/kernel.c` | `process_a` / `process_b` + 装配 |

---

## 和 Linux 内核的对应

| 概念 | 这里 | Linux |
|------|------|-------|
| PCB | `struct pcb` | `struct task_struct` |
| 保存的栈指针 | `pcb.saved_sp` | `task_struct.thread.sp` |
| 上下文切换 | `irq0_handler`（pusha/换ESP/popa/iret） | `__switch_to_asm`（类似思路，rsp 换来换去） |
| 调度触发 | PIT IRQ0 → `irq0_enter` | 时钟中断 → `scheduler_tick` |
| 调度算法 | round-robin | CFS（红黑树，按虚拟运行时间排序） |
| 时钟驱动 | PIT 100Hz | `clocksource` + `hrtimer`（高精度定时器） |
| 进程栈 | 一页（4KB） | 通常 8KB（两页，内核栈） |
| ring | 全程 ring 0 | ring 0/3 切换，TSS 提供 esp0 |

---

## 待改进 / 下一步

- **睡眠/阻塞**：现在进程只能"死循环"，没有"等事件"的能力。需要 `sleep()`/`block()` + 唤醒机制
- **进程退出**：`process_a` 死循环不会退出，没有 `exit()`，也没有回收栈的机制
- **ring 3 进程**：把 ring3 实验和调度器结合——每个用户进程有自己的页目录，TSS 提供内核栈
- **优先级 / 抢占点**：现在是纯轮转，没有优先级概念
- **VMA 管理**：等有多进程地址空间隔离时再做（见 `agentic/13-todo-...md`）

---

## 术语速查

| 术语 | 含义 |
|------|------|
| PCB | Process Control Block，进程的"档案" |
| 上下文切换 | 保存当前进程寄存器 + 恢复下一个进程寄存器 |
| 抢占式 | 时钟中断强制切换，进程不需要主动让出 |
| 协作式 | 进程主动调 `yield()` 让出 CPU |
| round-robin | 轮转调度，按顺序一个个来 |
| 时间片 | 每个进程每次跑多久（我们这里是 10ms） |
| EOI | End of Interrupt，告诉 PIC"处理完了，可以发下一个" |
