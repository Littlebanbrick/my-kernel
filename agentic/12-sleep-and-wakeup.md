# 12 — 睡眠与唤醒（timer-based sleep）

## 起点：进程需要"暂时等一下"

第 11 篇做完进程退出后，状态机是：

```
READY  ──选中──→  RUNNING  ──sched_exit──→  FINISHED
```

`FINISHED` 是"永久退出"。但进程还有**"暂时等一下"**的需求：

- 等时间：动画每 16ms 渲染一帧、TCP 重传退避、限流
- 等事件：`read` 等键盘输入、`accept` 等连接

这些都不是退出——进程**还要继续跑**，只是**现在没事干**。如果它占着 CPU 空转（spin），就浪费了 CPU；正确的做法是**让出 CPU，等到该等的事到了再被叫醒**。这就是**阻塞唤醒**机制。

这篇笔记做最简版本：**timer-based sleep**——"睡 N 个 tick 后叫我"。它自包含（不需要先有键盘驱动/等待队列那一套），单凭 `g_ticks` 就能跑。等做完它，事件阻塞只是把"唤醒源"从"定时器"换成"事件处理函数"——机制完全复用。

---

## 关键认知：进程不想要"一直运行"，它想要"往前推进"

学操作系统时最普遍、最该打破的直觉是"进程想一直占着 CPU"。**不是。**

进程想要的是**往前推进**，运行（占 CPU 空转）和推进是两回事。一个进程如果没活干还占着 CPU，那是 bug。

```c
int x;
scanf("%d", &x);   // 等你敲键盘——此刻进程被挂起，CPU 早切去跑别人了
```

调 `scanf` 时你的程序**没在运行**——它被内核挂起了。等你敲回车，键盘的中断处理函数把它唤醒，`scanf` 才返回。**你写过无数个会"阻塞"的程序，只是没人这么告诉你。**

一个设计良好的程序，**99% 的时间都在某种阻塞里**——等输入、等磁盘、等网络、等下一帧。真正占 CPU 计算的时间是少数。这就是为什么多任务能成立：每个进程大部分时间都在"等"，CPU 可以在它们之间腾挪。**如果每个进程都想"一直运行"，分时系统根本没法工作。**

带着这个修正看 sleep：**进程主动 sleep 不是"放弃运行"，是"我现在没活，让别人跑，N tick 后叫我"**。

---

## 状态机扩展

加一个 `SLEEPING` 状态：

```
READY    ──调度器选中──→  RUNNING
RUNNING  ──sleep(n)──→  SLEEPING  (记下 wakeup_tick = g_ticks + n)
RUNNING  ──sched_exit──→ FINISHED
SLEEPING ──到点──→     READY      (由 irq0_enter 检查并转换)
```

```c
enum proc_state {
    PROC_READY     = 0,
    PROC_FINISHED  = 1,
    PROC_SLEEPING  = 2,
};
```

值不连续是有意的：READY=0、FINISHED=1、SLEEPING=2，方便区分"可运行"和"被阻塞"两类。

`pick_next` 只选 READY，**跳过 FINISHED 和 SLEEPING**。

---

## `sleep()` 的实现

```c
void sleep(unsigned int ticks)
{
    if (current_pid < 0)
        return;

    procs[current_pid].wakeup_tick = g_ticks + ticks;
    procs[current_pid].state = PROC_SLEEPING;

    asm volatile("int $0x20");    // 触发软件 IRQ 0
}
```

三步：

1. **记下何时该醒**：`wakeup_tick = g_ticks + ticks`
2. **标自己 SLEEPING**：`pick_next` 看到 SLEEPING 就跳过
3. **`int $0x20` 主动让出**：和 `sched_exit` 一样的套路——标记 + 软件中断

### 为什么 `int $0x20` 之后 sleep 能"返回"

和 `sched_exit` 不同，`sleep` 要**返回到调用者**继续跑。这怎么做到？

`int $0x20` 执行时，CPU 把"返回到 sleep 末尾"的地址压在**当前进程的栈上**。然后 `irq0_handler` 走流程：`pusha` → 调 `irq0_enter`（看到当前进程 SLEEPING，`pick_next` 选别人）→ 返回新进程的 ESP → 汇编 `mov %eax, %esp` **把 ESP 换走** → `popa; iret` 弹的是**另一个进程**的寄存器和 EIP。

我们的返回地址还在**原来进程的栈上**——和进程的 `saved_sp` 一起被冻结。等 N tick 后该进程被唤醒（state 转回 READY）、被 `pick_next` 选中，`irq0_handler` 把 ESP 换回它的栈，`popa; iret` 弹出的 EIP 就是当时 `int $0x20` 之后那条指令——**sleep 函数返回，调用者继续跑**。

**关键点**：`sleep` 和 `sched_exit` 走的是**同一条路径**（标状态 + `int $0x20`），区别只在标的是什么状态：
- `FINISHED`：永远不会被再选——`int $0x20` 之后那行 `hlt` 是兜底，到不了
- `SLEEPING`：N tick 后会被唤醒回 READY——`int $0x20` 之后 sleep 正常返回

**同一个机制服务两种语义**（永久退出 vs 暂时等待），这是抽象的力量。

### `sleep(0)` 的语义

`sleep(0)` 设 `wakeup_tick = g_ticks + 0 = g_ticks`。下次 `wake_sleepers` 检查时，`(s32)(g_ticks - wakeup_tick) >= 0` 立刻成立（差为 0），进程马上转回 READY。所以 `sleep(0)` = "让出至少一个 tick"——就是 **yield**。

一个机制同时覆盖 sleep 和 yield，没有单独写 `yield()`。

---

## 唤醒：`wake_sleepers()`

睡着的进程自己不会跑，所以"检查该不该醒"必须在**别人**那里做。最自然的位置是 `irq0_enter`——每个 tick 进来时扫一遍：

```c
static void wake_sleepers(void)
{
    int i;
    for (i = 0; i < MAX_PROCS; i++) {
        struct pcb *p = &procs[i];
        if (p->used && p->state == PROC_SLEEPING &&
            (s32)(g_ticks - p->wakeup_tick) >= 0)
            p->state = PROC_READY;
    }
}
```

`irq0_enter` 在 `pick_next` 之前调它：

```c
u32 irq0_enter(u32 saved_sp)
{
    g_ticks++;
    if (current_pid >= 0)
        procs[current_pid].saved_sp = saved_sp;

    wake_sleepers();              // ← 唤醒到点的进程

    next_pid = pick_next();        // ← 再选下一个（可能选到刚醒的）
    pic_send_eoi(0);
    ...
}
```

**顺序很重要**：先唤醒再选——这样刚到点的进程立即参与本轮调度，不会拖到下个 tick。

### 为什么用 `(s32)(g_ticks - wakeup_tick) >= 0` 而不是 `g_ticks >= wakeup_tick`

`g_ticks` 是 `u32`，到 42.9 亿会回绕到 0。直接比大小在回绕那一下会判断错：

```
设 g_ticks = 0xFFFFFFFE，进程设 wakeup_tick = g_ticks + 5 = 3（回绕后）
过了 5 tick，g_ticks = 3
  g_ticks >= wakeup_tick  →  3 >= 3  →  true   ✓ 这下碰巧对了

再设 g_ticks = 0xFFFFFFFD，wakeup_tick = 2（回绕后）
过了 5 tick，g_ticks = 2
  g_ticks >= wakeup_tick  →  2 >= 2  →  true   ✓ 也对

但若 g_ticks = 0xFFFFFFFE，wakeup_tick = 0xFFFFFFFF（差 1）
g_ticks 涨到 0（回绕）
  g_ticks >= wakeup_tick  →  0 >= 0xFFFFFFFF  →  false  ✗ 错！应该醒
```

用**带符号差值**就对了：

```
(s32)(g_ticks - wakeup_tick)
  = (s32)(0 - 0xFFFFFFFF)
  = (s32)0x00000001
  = 1  →  >= 0  →  true  ✓
```

原理：无符号减法 `a - b` 在 mod 2^32 下永远正确表达"从 b 到 a 走了多少步"。把它当成有符号数读，正数 = a 在 b 之后，负数 = a 在 b 之前。这能自然处理回绕。

**这是个值得记的细节**——Linux 的 jiffies 子系统也用同样的 trick（`time_after`/`time_before` 宏）。所有处理"无符号时间戳回绕"的代码都得这么写。

---

## `sleep` vs `sched_wait_tick`：hlt vs spin

我们之前有个 `sched_wait_tick`：

```c
void sched_wait_tick(void)
{
    u32 t = g_ticks;
    while (g_ticks == t)
        asm volatile("pause");    // spin
}
```

这是个 **spin-wait**——进程**占着 CPU 空转**等 g_ticks 变化。CPU 没休息，只是把时间片烧在 `pause` 上。

`sleep` 不一样：

```c
void sleep(unsigned int ticks)
{
    ...
    procs[current_pid].state = PROC_SLEEPING;
    asm volatile("int $0x20");    // 切走，CPU 给别人（或 idle）
}
```

进程**让出 CPU**——`pick_next` 跳过 SLEEPING 的它，选别人。如果没别人可跑，选 idle，idle `hlt` 让 CPU **真正休息**（省电、降温）。

| | `sched_wait_tick` | `sleep` |
|---|---|---|
| 等 | spin（占 CPU） | hlt（让 CPU） |
| 状态 | 一直 READY | SLEEPING |
| 唤醒 | g_ticks 变化（被动） | wakeup_tick 到点（主动记的） |
| CPU 利用率 | 100%（烧时间片） | 接近 0%（idle 时 hlt） |

`sleep` 是 `sched_wait_tick` 的**正确替代**——前者让进程让出 CPU，后者只是"占着茅坑不拉屎"。`sched_wait_tick` 现在可以视为遗留函数，新代码应该用 `sleep`。

### `pause` 指令是什么

`asm volatile("pause")` 是给 CPU 的提示："我在 spin-wait 循环里，你不用担心流水线乱序，省点电吧"。它**不释放 CPU**，只是让 spin 的代价小一点。和 `hlt` 完全不是一个量级——`hlt` 是真睡，`pause` 只是"转得轻一点"。

---

## 验证（QEMU + GDB）

### GDB 追踪 sleep 调用

在 `sleep` 入口下断点，连续命中 4 次：

```
1st sleep: cur=0 ticks=0   A.state=0 A.wakeup=0    B.state=0 B.wakeup=0     idle=0
2nd sleep: cur=1 ticks=1   A.state=2 A.wakeup=5    B.state=0 B.wakeup=0     idle=0
3rd sleep: cur=0 ticks=5   A.state=0 A.wakeup=5    B.state=2 B.wakeup=11    idle=0
4th sleep: cur=0 ticks=10  A.state=0 A.wakeup=10   B.state=2 B.wakeup=11    idle=0
```

（state: 0=READY, 2=SLEEPING）

| 命中 | 当前进程 | 解读 |
|------|---------|------|
| 1st | A | A 第一次调 sleep(5)。断点在入口，state 还没设 |
| 2nd | B | A 已 SLEEPING（wakeup=5），CPU 切到 B。**ticks=1** 说明 A 在第 0 tick 睡下，中间 idle 跑了几 tick，B 在第 1 tick 才开始跑——idle 真正派上用场 |
| 3rd | A | **ticks=5**——正好是 A 的 wakeup_tick！A 睡了 5 tick 后被唤醒，再次调 sleep。此刻 B 已 SLEEPING（wakeup=11） |
| 4th | A | **ticks=10**，A 又睡 5 tick 醒来（5+5=10）。B 还在睡（wakeup=11） |

**关键证据**：A 的 wakeup_tick 从 0→5→10，每次 +5；ticks 在 A 的两次 sleep 之间从 0 涨到 5 又涨到 10——**正好是 sleep 的 5 tick**。证明 sleep 期间 A 真的没在跑（不然 ticks 不会涨）。

### 屏幕输出

```
B: tick 471
A: tick 475      ← A 的间隔 5
A: tick 480
B: tick 481      ← B 的间隔 10
A: tick 485
A: tick 490
B: tick 491
A: tick 495
B: tick 501
B: tick 511      ← 501→511 = 10
B: tick 521
```

观察：

- A 的间隔恒为 5（475→480→485→490→495）
- B 的间隔恒为 10（471→481→491→501→511→521）
- **sleep 时长精确**

---

## 关于"线性扫描"是否够用

`wake_sleepers` 每 tick 扫一遍 `procs[MAX_PROCS]`——O(N)，N=8。这是个**正确但小规模**的解法。

| 规模 | 数据结构 | 复杂度 |
|------|---------|--------|
| 我们（≤8） | 数组线性扫 | O(N)，无所谓 |
| 中等 | 链表分桶（timer wheel） | O(1) 查询 |
| 大规模 | 红黑树 / 最小堆（hrtimer） | O(log N) 插入，O(1) 看最早 |

**这不是"哪种更好"的问题，是"什么规模用什么"的问题。** 现在用数组扫 8 个进程够用、好懂。等真有上千个定时器，再换结构——那时候会有"为什么需要红黑树"的真实动机。

**红黑树不是学出来的，是被规模逼出来的。** 等你真有"线性扫太慢了"的痛点再学，会有"原来它是为这个服务的"的恍然大悟。

### 事件阻塞那边的等待队列

事件阻塞的"唤醒"是**事件来时遍历该事件的等待队列**——这个队列通常很短（等同一个键盘输入的进程一般就一两个），所以**线性扫在事件阻塞里是生产标准做法**，Linux 的 `wait_queue` 就是个链表，唤醒时线性遍历。这里"低效"不构成问题，因为 N 本来就小。

---

## 关于"进程何时该 sleep"

一个常见疑问：进程编写者怎么知道何时 sleep、sleep 多久？生产里真这么干吗？

`sleep(N)` 在生产里**有，但是少数情况**——出现在能明确说出"我等 N 个时间单位后该被叫醒"的场景：

| 场景 | 谁定的 N | 例子 |
|------|---------|------|
| 重试退避 | 协议规范 | TCP 包丢了，等 100ms 重传；失败再等 200ms、400ms |
| 周期任务 | 业务需求 | 心跳包每 30 秒发一次；动画每 16.67ms 渲染一帧 |
| 限流 | 配置 | "每秒最多发 10 条消息" → 发一条 sleep 100ms |

这些 N 不是猜的，要么来自协议规范，要么来自业务需求。

**更常见的模式根本不用 `sleep(N)`**，而是"阻塞在某个事件上，等事件来了再叫我"：

```c
read(fd, buf, 256);     // 阻塞，直到有数据可读——不知道要等多久
accept(sock, ...);      // 阻塞，直到有新连接
```

这里的程序员**没说"睡多久"**，他说的是"**睡到某件事发生**"。`read` 不知道键盘什么时候有输入，所以不能 `sleep(N)`——只能"把自己挂到键盘的等待队列上，等键盘中断处理函数来叫我"。

`sleep(N)` 是"等时间"，事件阻塞是"等事件"——后者才是生产主力，但前者更简单。先做 `sleep(N)` 跑通状态机和唤醒逻辑，事件阻塞只是把"唤醒源"从"定时器"换成"事件处理函数"——**机制完全复用**。

---

## 事件阻塞的"数据给谁"（留给将来）

事件阻塞里有个微妙的点：很多进程在 `read(stdin)` 上等，键盘来个字符 'x'，给谁？

答案是：**内核不预判，把数据放进缓冲区，喊一声"有数据了"，谁先 `read` 取走就给谁。** 决策权在 `read()` 实现里，不在 `wake_up()` 里。

- `wake_up` 只做一件事：把进程状态从 SLEEPING 改回 READY，**不传数据**
- 数据进的是**缓冲区**，不是"某个进程的口袋"
- `read()` 用 `while` 不是 `if`：被唤醒后重新检查缓冲区，避免"白醒一次"
- `wake_up_one` 只唤醒一个（避免惊群效应），`wake_up_all` 全部唤醒
- 终端的**前台进程组**机制用规则排除了"多进程同时等键盘"的情况

这些等做 shell / 事件阻塞时再细讲。现在只要记住：**数据流和唤醒是解耦的**——唤醒只是通知，取数据是 `read` 自己的事。

---

## 文件清单

| 文件 | 改动 |
|------|------|
| `my-kernel/include/types.h` | 加 `typedef signed int s32` |
| `my-kernel/include/sched.h` | 加 `PROC_SLEEPING`、`pcb.wakeup_tick`、`sleep` 声明 |
| `my-kernel/kernel/sched.c` | 新增 `wake_sleepers`、`sleep`；`irq0_enter` 调 `wake_sleepers` |
| `stage-3-protected-mode/kernel.c` | A/B 改成 `printf + sleep(N)` |

---

## 和 Linux 的对应

| 概念 | 这里 | Linux |
|------|------|-------|
| 睡眠 | `sleep(ticks)` 标 SLEEPING + `int $0x20` | `schedule_hrtimeout()` / `msleep()` |
| 唤醒时刻 | `pcb.wakeup_tick` | `hrtimer` 节点的 `expires` 字段 |
| 唤醒扫描 | `wake_sleepers()` 每 tick 线性扫 | hrtimer 红黑树，每 tick 只看树根 |
| 回绕安全比较 | `(s32)(g_ticks - wakeup_tick) >= 0` | `time_after()` 宏 |
| 让出 CPU | `int $0x20`（同 exit 路径） | `schedule()`（直接调用） |
| spin-wait | `sched_wait_tick`（pause） | `cpu_relax()` |
| 真睡 | `idle_task` 的 `hlt` | `cpu_idle_loop` 的 `safe_halt` |

---

## 术语速查

| 术语 | 含义 |
|------|------|
| 阻塞 | 进程因等待某事而暂时不能运行 |
| 唤醒 | 把阻塞中的进程转回 READY |
| spin-wait | 占着 CPU 空转等条件成立 |
| hlt | CPU 睡到下一个中断来，省电 |
| 惊群效应 | 多个等待者被同时唤醒抢一个资源，N-1 个白跑 |
| 回绕 | u32 计数器到上限后回到 0 |
| 时间片 | 每个进程每次跑多久 |

---

## 待改进 / 下一步

- **优先级**：现在 idle 和 A/B 平等 round-robin，A 睡时若 B 也睡，idle 还得轮流占时间片。引入优先级后 idle 永远最低，"没别的可跑才选它"
- **事件阻塞**：把"唤醒源"从定时器换成事件处理函数——键盘 IRQ 里 `wake_up(&kbd_queue)`
- **`sleep` 改 syscall**：现在 ring 0 直接调 `sleep()`，将来 ring 3 时改成 `int $0x80` + syscall 号——逻辑层完全复用，只换调用方式
- **进程回收**：FINISHED 的进程栈没释放，PCB 槽位没清理——做 `wait()`/`reap`
