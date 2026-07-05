# 11 — 进程退出与 idle task（ring 0）

## 起点：进程不会自己停下来

第 10 篇的调度器跑起来后，`process_a` / `process_b` 是**死循环**：

```c
for (i = 0; ; i++) {
    printf("A: %d\n", i);
    for (spin = 0; spin < 200000; spin++) ;
}
```

进程永远不会结束，调度器也没有"进程退出"的概念。这带来两个问题：

1. **没法收尾**——两个进程都跑不死，机器要么一直转、要么被强制 kill
2. **`g_ticks` 成了摆设**——它定义在 `kernel.c` 里，但 IRQ 0 改走专用 `irq0_handler` 后，**根本没人自增它**（旧路径 `handle_exception` 里的 `g_ticks++` 被删了，新路径忘了加回来）

这篇笔记讲两件事：怎么让进程**主动退出**、退光之后系统**该停在哪**。

---

## 关键认知：进程退出后，"返回主函数"是不可能的

最初的想法可能是："进程跑完 100 次，return 回 `kernel_main` 收尾。" 但这在物理上做不到。

看 `idt_handlers.S` 里的 `do_first_switch`：

```asm
do_first_switch:
    movl 4(%esp), %esp   ; 把 ESP 换成进程 0 的栈
    popa
    iret
```

`movl ... %esp` 这一步把 ESP 从 `kernel_main` 的栈换成了进程 0 的栈——**`kernel_main` 的栈帧从此就丢了**。进程函数 return 时，它 return 到哪儿？返回地址在它自己的栈上（`iret` 弹出的 EIP），那个地址由 `create_process` 设成了进程入口——根本不是 `kernel_main`。

所以"进程结束后回到 kernel_main"在机制上做不到。**等价的做法**是：进程退出后跳到一个收尾函数，做"主函数本来该做的收尾"。

### 这对应 Linux 的什么

Linux 的 `start_kernel`（相当于我们的 `kernel_main`）跑完就把控制权交出去，**再也不返回**。它甚至把整个 `.init` 段（启动代码所在的内存）释放掉了——启动器是一次性脚手架。

真正"永远不返回"的是 **idle loop**：

```c
// Linux: init/main.c (简化)
cpu_idle_loop() {
    while (1) {
        cpu_idle();     // 进程都睡了就 hlt
        schedule();     // 否则切到下一个进程
    }
}
```

我们的 `sched_start()` 就是这个角色的简化版：启动第一个进程，永不返回。

**核心观念**：内核不是"一个会结束的程序"，内核是**一个永远不会主动停止的事件循环**。进程来来去去，循环永不止息。唯一的退出条件是关机。

---

## 进程退出：`sched_exit()`

### 机制

```c
void sched_exit(void)
{
    if (current_pid >= 0)
        procs[current_pid].state = PROC_FINISHED;
    asm volatile("int $0x20");        // 触发软件 IRQ 0
    while (1) asm volatile("hlt");    // 永远到不了
}
```

两步：

1. **标记自己 FINISHED**——`pick_next()` 看到 FINISHED 就跳过，再也不会选它
2. **`int $0x20` 主动触发中断**——强迫 CPU 走 `irq0_handler`，进入调度器

### `int $0x20` 是什么

`int` 是软件中断指令：让 CPU **假装发生了一个中断**，走和硬件中断一模一样的流程（压 EFLAGS/CS/EIP → 查 IDT → 跳处理程序）。和硬件中断的唯一区别是触发时机——硬件异步、`int` 同步。

`0x20` = 32 = `IRQ0_VECTOR`（`pic_remap` 把 IRQ 0 重映射到 IDT[32]）。所以：

- **PIT 硬件**触发 IRQ 0 → PIC 翻译成向量 0x20 → IDT[0x20] → `irq0_handler`
- 进程执行 `int $0x20` → 直接查 IDT[0x20] → `irq0_handler`

**两条路汇合到同一个处理程序**。这就是 `idt.c` 里这一行的意义：

```c
idt_set_entry(&idt[IRQ0_VECTOR], irq0_handler, IDT_KERN_INT);
```

### 为什么 `int $0x20` 之后"不返回"

`int` 指令把"返回到 `while` 那行"的地址压在**当前进程的栈上**。然后 `irq0_handler` 走流程：

1. `pusha` 保存寄存器
2. 调 `irq0_enter`——发现当前进程 FINISHED，`pick_next` 选了另一个进程
3. 返回新进程的 `saved_sp`
4. 汇编 `mov %eax, %esp` **把 ESP 换走了**
5. `popa; iret` 弹的是**另一个进程**的寄存器和 EIP

我们的返回地址还在原来的栈上，但那个栈被冻结了——再也不会切回来。所以 `int $0x20` 之后那行 `while (1) hlt;` 永远不会执行，只是兜底保险。

### 对应 Linux

`int $0x20` = **协作式让出（cooperative yield）**的最小实现。Linux 里：

- **被动调度**：时钟中断 → `scheduler_tick` → `schedule()`（对应我们的 PIT IRQ 0）
- **主动调度**：进程调 `sched_yield()` / `sleep()` → `schedule()`（对应我们的 `int $0x20`）

两条路最终都进 `schedule()`。**同一套调度代码，两个入口**——这是这个设计最优雅的地方。

---

## 修 bug：`g_ticks` 没人自增

退出机制做好后，顺手修了一个之前埋的 bug：

- 旧路径：IRQ 0 走 `handle_exception`，里面 `g_ticks++`
- 新路径：IRQ 0 改走专用 `irq0_handler`，**忘了把 `g_ticks++` 搬过来**

修法：在 `irq0_enter` 开头加 `g_ticks++`。同时把 `g_ticks` 的定义从 `kernel.c` 移到 `sched.c`（因为它现在是调度器内部状态，不是 kernel.c 的）。

```c
u32 irq0_enter(u32 saved_sp)
{
    g_ticks++;                          // ← 修好的 bug
    if (current_pid >= 0)
        procs[current_pid].saved_sp = saved_sp;
    next_pid = pick_next();
    pic_send_eoi(0);
    ...
}
```

**教训**：重构时如果删掉一条代码路径，要逐行检查它身上有什么"副作用"被搬走了没有。`g_ticks++` 就是个被遗漏的副作用。

---

## 进程状态机（第一版）

引入退出后，状态机是：

```
READY  ──调度器选中──→  RUNNING
RUNNING ──sched_exit──→  FINISHED
```

`pick_next` 只选 READY，跳过 FINISHED。

### 但是有个漏洞

当**所有**进程都 FINISHED 时，`pick_next` 返回 -1，`irq0_enter` 调 `sched_finish()`：

```c
static void sched_finish(void)
{
    printf("sched: all processes finished - halting.\n");
    asm volatile("cli");
    for (;;) asm volatile("hlt");
}
```

`cli` = clear interrupt flag，屏蔽所有可屏蔽硬件中断。`hlt` 之后 CPU 睡死，连时钟中断都叫不醒。**系统进入"植物人"状态**——这才是真正的死机。

这破坏了"内核是永不停止的事件循环"这个原则。**真正的操作系统永远不会因为'暂时没活干'就停机**。没活干的时候它应该**等活**，而不是死掉。

---

## idle task：永远在场的兜底进程

### 设计

引入一个特殊的进程，**永远 READY、永远不退出**，没事干就 `hlt` 等中断：

```c
void idle_task(void)
{
    for (;;)
        asm volatile("hlt");
}
```

就这么多。它**永远是 `pick_next` 的兜底选择**。这样 `sched_finish` 永远不会被触发——因为 `pick_next` 永远至少能返回 idle。

`hlt` 的妙处：CPU 睡着时如果来一个中断（键盘、定时器），**中断会先唤醒 CPU**，然后正常走中断处理。所以 idle task 表面上在死循环 `hlt`，实际上每次中断来都会被打断，处理完中断回到 `hlt` 循环。**这就是"事件循环"的硬件实现。**

### 装配

在 `sched_start` 里**自动**创建 idle，不需要用户操心：

```c
void sched_start(void)
{
    create_process(idle_task, "idle");   // ← 自动兜底
    ...
    do_first_switch(procs[first].saved_sp);
}
```

### 状态机更新

```
READY  ──调度器选中──→  RUNNING
RUNNING ──sched_exit──→  FINISHED
（idle 永远在 READY ↔ RUNNING 之间循环，永不进 FINISHED）
```

### `sched_finish` 变成 panic

有了 idle 兜底，`pick_next` 不可能返回 -1。如果真返回了，说明 idle 自己挂了——那是 bug，不是正常退出。所以 `sched_finish` 改成 panic：

```c
static void sched_finish(void)
{
    printf("sched: PANIC — no runnable process (idle exited?)\n");
    asm volatile("cli");
    for (;;)
        asm volatile("hlt");
}
```

理论上永远到不了。到得了说明内核有 bug。**这是从"正常路径"到"不该到达的兜底"的语义升级**。

---

## 已知低效：idle 抢时间片

idle 和 A、B **平等参与 round-robin**，所以 A 跑一次、B 跑一次、idle `hlt` 一次，**每 3 个 tick 才轮到 A 一次**。本来 A/B 各 100 次打印只需要 ~200 tick，现在需要 ~600 tick。

正确做法是给 idle **最低优先级**——只有"没别的可跑"时才选它。但这需要优先级机制，留到以后做。这里先记着这个低效的来源。

---

## "进程 C 怎么来"——idle 让答案变简单

之前有个问题：A/B 都退出后系统停了，这时想再跑个进程 C 怎么办？

有了 idle 之后答案简单了：**直接 `create_process(process_c, "C")` 就够了**。

`create_process` 把 process_c 塞进 `procs[]` 表，标记 READY。**下一次 IRQ 0 来的时候，`pick_next` 自然会发现它**（跳过 FINISHED 的，但不跳过 READY 的），就切过去了。不需要"手动汇编"、不需要再调 `sched_start`。

`sched_start` 那套"伪造中断帧 + 第一次切换"的特殊处理只用于启动第一个进程——那是唯一一次"CPU 还没进过中断路径"的时刻。之后的进程一律走标准 IRQ 0 路径。

那 `create_process` 这一行谁来执行？三种典型来源：

| 来源 | 例子 | 对应 Linux |
|------|------|-----------|
| 另一个进程主动创建 | 进程 A 调 `create_process(C)` | `fork()` |
| 中断处理程序创建 | 键盘 IRQ 处理函数起一个新程序 | shell 的 fork+exec |
| idle 自己轮询 | idle 检查"有没有新工作待办"队列 | kernel thread 启动 |

最自然的是第二种——做 shell 时，用户敲命令、按回车，键盘中断里 `create_process` 起一个进程跑那个命令。

---

## 文件清单

| 文件 | 改动 |
|------|------|
| `my-kernel/include/sched.h` | 加 `enum proc_state`、`pcb.state`、`sched_exit` 声明 |
| `my-kernel/kernel/sched.c` | `pick_next` 跳 FINISHED、`irq0_enter` 加 `g_ticks++`、新增 `sched_exit`、`idle_task`、`sched_finish` 改 panic |
| `stage-3-protected-mode/kernel.c` | A/B 改成循环 N 次 + `sched_exit`、`g_ticks` 移走 |
| `my-kernel/kernel/idt.c` | 删无用的 `extern g_ticks` |

---

## 验证（QEMU + GDB）

### 屏幕输出

```
sched: starting, first pid = 0
A: hello
B: hello
sched: all processes finished - halting.    ← 第一版（无 idle）
```

加 idle 后没有这行 halting 了——系统靠 idle 持续运转。

### GDB 追踪 PCB state

在 `sched_exit` 下断点，看 PCB 的 state 字段：

```
1st sched_exit: pid0.state=0 pid1.state=0    ← A 调 sched_exit，state 还没设
2nd sched_exit: pid0.state=1 pid1.state=0    ← A 已 FINISHED，B 调 sched_exit
3rd (sched_finish): pid0.state=1 pid1.state=1  ← 两个都 FINISHED → halt
```

加 idle 后再追：

```
Final: cur=2 ticks=1012 A.state=1 B.state=1 idle.state=0
PC = 0xdd96 (inside idle_task, NOT sched_finish)
```

**A、B 都 FINISHED 后，CPU 坐在 idle_task 的 hlt 循环里**，ticks 还在涨，系统活着——这就是"待机"而不是"停机"。

---

## 和 Linux 的对应

| 概念 | 这里 | Linux |
|------|------|-------|
| 进程退出 | `sched_exit()` 标 FINISHED | `do_exit()` 标 `TASK_DEAD` |
| 主动让出 | `int $0x20`（软件 IRQ 0） | `schedule()`（直接函数调用） |
| idle 进程 | `idle_task()` 永远 hlt | `cpu_idle_loop()` |
| "不该到达" | `sched_finish` panic | `BUG()` / `panic()` |
| 启动器 | `kernel_main` / `sched_start` | `start_kernel` / `rest_init`（启动后 `.init` 段释放） |

---

## 术语速查

| 术语 | 含义 |
|------|------|
| `cli` | CLear Interrupt-flag，IF=0，屏蔽可屏蔽中断 |
| `hlt` | Halt，CPU 睡到下一个中断来 |
| panic | 内核遇到不该发生的情况，打印信息后停机 |
| idle | "没活干"的兜底进程，永远 READY |
| 协作式让出 | 进程主动调 `yield`/`exit` 让出 CPU |
| 抢占式 | 时钟中断强制切换，进程不需要主动让 |

---

## 待改进 / 下一步

- **优先级**：让 idle 永远最低，不再和用户进程抢时间片（见 12 篇后的 sleep 也会受这个低效影响）
- **阻塞唤醒**：`FINISHED` 是"永久退出"，但进程还有"暂时等一下"的需求——等时间（sleep）、等事件（read）。这是 12 篇的主题
- **进程回收**：FINISHED 的进程栈现在没释放，PCB 槽位也没清理。要做 `wait()`/`reap`
