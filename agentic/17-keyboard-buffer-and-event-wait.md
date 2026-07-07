# 17 — 键盘缓冲区与事件等待（ring buffer + wait_event）

## 概述

这次做了两件事，为 shell 打地基：

1. **键盘环形缓冲区**：把"按键"从"中断里直接写 VGA"改造成"中断推字符进队列、进程从队列取字符"。生产者和消费者解耦。
2. **事件等待（event-wait）**：给调度器加一个"睡到某事件发生"的原语，让 `getchar` 在缓冲区空时**真睡死**，等键盘中断把它叫醒——而不是轮询烧 CPU。

第二个是这次的概念核心。环形缓冲区只是配角。

最终效果：`getchar()` 调用时如果没键，进程睡死、CPU 跑 idle 的 `hlt`；用户一敲键，键盘 ISR 把字符推进缓冲区并叫醒等待者，进程下次被调度时拿到字符返回。**这就是 Linux `wait_queue` + `wake_up` 的玩具版。**

---

## 一、为什么要把键盘处理从 idt.c 拎出来

改之前，键盘逻辑塞在 `idt.c` 的 `handle_exception` 里：

```c
if (vec == IRQ1_VECTOR)
    kbd_display_scancode(inb(KBD_DATA_PORT));   // 直接把 scancode 写到 VGA 固定位置
```

问题：`idt.c` 是**通用中断分发器**（异常、IRQ、系统调用都走它），键盘是**设备驱动**。把设备代码塞进通用分发器，职责混在一起，将来加鼠标、磁盘、串口，`idt.c` 会越来越乱。

正确分层：**idt.c 只负责"分发到对应处理函数"，设备处理函数放各自的驱动文件。** 所以新建 `my-kernel/kernel/kbd.c` + `include/kbd.h`，把 scancode→ASCII 表、Shift 状态、ISR 全搬过去。`idt.c` 的 IRQ1 那行改成调 `kbd_isr()`。

> 这是"机制与策略分离"的一个小练习：中断分发是机制（在 idt.c），具体怎么处理键盘是策略（在 kbd.c）。

---

## 二、环形缓冲区（ring buffer）

### 数据结构

```
data[0]  data[1]  data[2]  ...  data[127]
  ▲                                  ▲
 head（消费者读这里）            tail（生产者写这里）
```

三个字段：

- `kbd_buf[128]`：定长数组（128，2 的幂）
- `kbd_head`：消费者下一个读的位置
- `kbd_tail`：生产者下一个写的位置
- `kbd_count`：当前有几个字符（用来区分空和满）

**为什么大小要 2 的幂？** 这样索引用 `& (SIZE-1)` 回绕，不用 `%`（取模）。`& 127` 比 `% 128` 快、没分支。这是写 ring buffer 的标准技巧。

**为什么要 `count`？** 光有 head/tail 不够——**空和满时 head 都等于 tail**。加个 count 直接判断：`count==0` 空，`count==SIZE` 满。简单不会错。

### 两个操作

```c
static void kbd_push(u8 c) {        // 生产者:ISR 调
    if (kbd_count == KBD_BUF_SIZE)
        return;                      // 满了就丢(人打字不会比 128 快)
    kbd_buf[kbd_tail] = c;
    kbd_tail = (kbd_tail + 1) & (KBD_BUF_SIZE - 1);
    kbd_count++;
}

int kbd_getchar_nonblocking(void) {  // 消费者:进程调
    u8 c;
    if (kbd_count == 0) return -1;    // 空返回 -1
    c = kbd_buf[kbd_head];
    kbd_head = (kbd_head + 1) & (KBD_BUF_SIZE - 1);
    kbd_count--;
    return c;
}
```

### 为什么需要缓冲区？

**解耦生产者和消费者的速度。**

- 生产者（键盘 ISR）：人手速，突发——一下连按几个键
- 消费者（getchar）：进程什么时候来取不一定

没有缓冲区：ISR 来一个字符就得立刻让人取走，否则丢了。有缓冲区：ISR 只管往里塞，进程什么时候来取都行，塞满之前不丢。

**这还顺带让 event-wait 的 getchar 变简单**——ISR 推完字符、叫醒等待者，等待者醒来直接 pop 就行，不用在中断里把字符递给进程。

---

## 三、核心概念：事件等待（event-wait）

### 先回顾：定时等待（timer-wait）

之前 `sleep(ticks)` 是"睡到第 N 个 tick"：

```c
void sleep(unsigned int ticks) {
    procs[current_pid].wakeup_tick = g_ticks + ticks;  // 醒来的时刻
    procs[current_pid].state = PROC_SLEEPING;
    asm volatile("int $0x20");   // 交出 CPU
}
```

唤醒源是**定时器**：每 tick `wake_sleepers()` 扫表，`wakeup_tick` 到了的进程拉回 READY。

### 这次要的：事件等待（event-wait）

`getchar` 要等的是"**有键按下**"——没有 deadline，不知道什么时候来。唤醒源是**键盘中断**，不是定时器。

第一版设计（**有 bug，下面讲**）：复用 `PROC_SLEEPING` 状态，把 `wakeup_tick` 设成 `0xFFFFFFFF`（"永远到不了"），靠 `wake(pid)` 主动叫醒。

---

## 四、那条链：事件唤醒体现在 4 个地方

"事件唤醒"不是某一个函数，是**一条跨越四个角色的链路**。以"用户按下 'h'"为例：

### ① 进程登记"我在等键盘" — `getchar` 里

```c
int getchar(void) {
    asm volatile("cli");
    while (kbd_count == 0) {
        kbd_waiter = current_pid;    // ← 登记:"我在等键盘事件"
        sched_block();                // ← 睡死
    }
    ...
}
```

`kbd_waiter` 是个全局便条："进程 X 在等键盘，来键了叫他"。这是链的**起点**——进程表达"我在等什么"。

### ② 进程睡到一个不会被定时器吵醒的状态 — `sched_block`

```c
void sched_block(void) {
    procs[current_pid].state = PROC_BLOCKED;   // ← BLOCKED:只等事件
    asm volatile("int $0x20");
}
```

`PROC_BLOCKED` 是"事件等待"的**载体**——`wake_sleepers` 每 tick 扫表时**跳过它**，所以不会被定时器错误叫醒。这保证它**只对事件敏感，不对时间敏感**。

### ③ 事件发生时，触发源去叫醒等的人 — `kbd_isr` 调 `wake`

按下 'h' → IRQ1 → `kbd_isr` 跑：

```c
void kbd_isr(void) {
    u8 scancode = inb(KBD_DATA_PORT);
    ... 翻译成 'h' ...
    kbd_push('h');              // ← 事件本体:字符进 buffer
    kbd_wake_waiter();          // ← 事件通知:叫醒等的人
}

void kbd_wake_waiter(void) {
    if (kbd_waiter >= 0) {
        wake(kbd_waiter);       // ← 拉回 READY
        kbd_waiter = -1;        // 便条撕掉
    }
}
```

`kbd_push` 是事件**本身**（字符到了），`kbd_wake_waiter` 是事件**通知**（叫醒等的人）。**中断处理程序主动去叫醒等待者**——这是 event-wake 最核心的一环。生产者推完数据，反手唤醒消费者。

### ④ 进程被调度器重新选中，从睡着的地方继续 — `getchar` 醒来

`wake` 只把状态设成 READY。真正"接着跑"是调度器下次选它时——`iret` 把它恢复到 `sched_block()` 里 `int $0x20` 之后那条指令，回到 `getchar` 的 while 循环，重检 `kbd_count`，发现不空，pop 出 'h' 返回。

### 一张图

```
进程(getchar)          kbd_waiter(便条)         键盘 ISR           调度器
    │                       │                      │                  │
  ①登记 ──────────► "X 在等键盘"                    │                  │
  ②sched_block                                  │                  │
    │ state=BLOCKED                                │                  │
    │ int $0x20 ──────────────────────────────────────────────────► 选中别人
    │ (睡死)                                        │                  │
    │                                              IRQ1 触发            │
    │                                              ③ kbd_push('h')     │
    │                                              ③ wake(X)           │
    │                    便条撕掉                    │ state=READY       │
    │                                              │ EOI                │
    │ ◄──────────────────────────────────────────────────────────── ④选中 X
  while 重检 buffer → 有 'h' → pop → 返回
```

**少一个环节都不行**：没 ① ISR 不知道叫谁；没 ② 进程会被定时器错误叫醒；没 ③ 事件发生了没人传话；没 ④ 状态变了也没人去跑。

---

## 五、检查-睡眠竞态：为什么 getchar 必须用 cli/sti

看 `getchar` 的流程，假设**没有 cli**：

```
getchar: 检查 buffer → 空 ──────┐
                                │ ← 这一刻键盘中断来了!
                                │   ISR 推字符, 看 kbd_waiter==-1
                                │   (还没登记), 不唤醒任何人
getchar: 继续 ──────────────────┘
        登记 kbd_waiter = 自己
        sched_block
        → 睡死。字符躺在 buffer 里没人叫醒它。
```

**字符丢了唤醒机会，进程永远睡死。** 这就是经典的**丢失唤醒（lost wakeup）**问题。

### 解法：cli 把"检查"和"睡眠"变原子

```c
int getchar(void) {
    asm volatile("cli");              // ← 关中断
    while (kbd_count == 0) {
        kbd_waiter = current_pid;
        sched_block();
    }
    asm volatile("sti");              // ← 开中断
    return kbd_getchar_nonblocking();
}
```

`cli` 后硬件中断进不来，那个竞态窗口就关上了——"检查 buffer 空 + 登记 waiter + sched_block"这几步成了**不被打断的原子区间**。做完 `sti` 重新开中断。

> 这就是 Linux `wait_event()` 要解决的核心问题。Linux 里用自旋锁（spinlock）关抢占，我们 ring-0 玩具内核里用 `cli`/`sti` 关中断，思路一样：**让"检查条件"和"进入睡眠"原子。**

---

## 六、cli / sti 速查

x86 两条指令，控制 CPU 的中断开关：

| 指令 | 全名 | 作用 |
|------|------|------|
| `cli` | CLear Interrupt flag | 关中断 → 硬件中断打不进来 |
| `sti` | SEt Interrupt flag | 开中断 → 硬件中断能打进来 |

CPU 的 EFLAGS 寄存器里有位叫 **IF（Interrupt Flag）**。IF=1 正常响应中断，IF=0 无视可屏蔽中断。`cli` 把 IF 设 0，`sti` 设 1。

**getchar 为什么要用**：检查 buffer 和 sched_block 之间不能被打断（否则 lost wakeup），用 `cli` 制造一个不被打断的原子区间，做完 `sti` 恢复。

**IF 在 `int $0x20` 路径上的流转**（这是最绕的，值得用 GDB 单步看一遍）：

1. `getchar` 里 `cli` → IF=0
2. `sched_block` 里 `int $0x20` → `int` 指令 push 当前 EFLAGS（IF=0），进中断门时 CPU 自动清 IF
3. `irq0_handler` 跑完，`iret` → pop 刚才保存的 EFLAGS → IF 恢复成 0（因为保存时是 0）
4. 进程在 `sched_block` 的 `int $0x20` 之后那条指令醒来 → **此时 IF=0**
5. 回到 `getchar` 的 while 循环重检 buffer → 还是 `cli` 状态
6. 出循环后 `sti` → IF=1

**所以 `sti` 必须放在 getchar 出口**——因为 iret 恢复的是"睡着前的 IF"，而睡着前是 `cli` 过的 IF=0。不 `sti` 的话进程带着关中断跑，后面所有中断都进不来。

---

## 七、那个 bug：哨兵 0xFFFFFFFF 为什么挡不住超时

第一版 `sched_block` 试图复用 `PROC_SLEEPING` + 一个"永远到不了的"哨兵：

```c
procs[current_pid].wakeup_tick = 0xFFFFFFFFu;   // "永远到不了"
procs[current_pid].state = PROC_SLEEPING;
```

注释里写的推理："wakeup_tick=0xFFFFFFFF，`wake_sleepers` 的超时检查永远不满足，所以只能被 wake 叫醒。"

**这个推理是错的。** 看 `wake_sleepers` 的检查：

```c
if (p->state == PROC_SLEEPING &&
    (s32)(g_ticks - p->wakeup_tick) >= 0)   // ← 有符号比较!
    p->state = PROC_READY;
```

那个 `(s32)` 是**有符号**比较。算一下：

- `0xFFFFFFFF` 作为 s32 是 **-1**
- 此刻 `g_ticks = 5`
- `g_ticks - wakeup_tick = 5 - 0xFFFFFFFF`
- 无符号算：`5 - (-1) = 6`（mod 2³²）
- cast 成 s32 是 **+6**
- `+6 >= 0` → **真** → 进程被当成"该醒了"，立刻变 READY

**所以"无期限睡"根本不是无期限——它每个 tick 都被 `wake_sleepers` 错误地叫醒一次。**

### 死循环怎么发生的

`getchar` 走 `cli` → buffer 空 → `sched_block`（设 SLEEPING + 哨兵）→ `int $0x20` 切走 → 下一个 tick `wake_sleepers` 看到哨兵判它"该醒了"→ 变 READY → 调度器又选它 → `getchar` 再 `cli`、再 `sched_block`……

**`cli` 一路带着关中断跑，键盘 IRQ1 永远进不来。** 这就是为什么加 `printf("ISR: %x")` 完全没反应——ISR 根本没机会执行。

### 根因：用"定时等待的状态"去表达"事件等待"

哨兵失效是**症状**，病根是**表达方式选错了**——用一个状态 + 数字哨兵硬塞两种语义（"睡到某 tick" 和 "睡到某事件"），再精巧的哨兵都会漏。

### 修复：分两个状态

```c
enum proc_state {
    PROC_READY    = 0,   // 可运行
    PROC_FINISHED = 1,   // 已退出
    PROC_SLEEPING = 2,   // 定时等待(wake_sleepers 按 tick 唤醒)
    PROC_BLOCKED  = 3,   // 事件等待(无超时,只能被 wake() 唤醒)
};
```

`wake_sleepers` **只看 SLEEPING，完全无视 BLOCKED**——所以 BLOCKED 的进程不会被定时器错误叫醒，只能被 `wake(pid)` 救。干净的分工：

- **SLEEPING** = "睡到某个 tick" → 唤醒源是定时器
- **BLOCKED** = "睡到某个事件" → 唤醒源是 `wake()`

**这正是 Linux 的分工**：timer-wake 和 event-wake 是两条独立的路径。`TASK_INTERRUPTIBLE` 的进程可以被信号、`wake_up` 等多种方式唤醒，但 timer 超时和事件触发是分开的逻辑。

### 教训

这和笔记 16 的"恒等映射是症状、ring 0 + 切 CR3 站在进程栈上是病根"是同一类教训：**别用数字游戏表达语义。语义该分开就分开状态，别让一个值身兼两职。**

还有一条：**注释不是证据。** 我在注释里写"哨兵永远到不了"，但没在脑子里真正推演那个有符号比较。注释描述的是**意图**，不是**事实**——事实得靠 GDB 单步看，或者靠把表达式手算一遍。

---

## 八、和 Linux 的对应

| 概念 | 这里 | Linux |
|------|------|-------|
| 键盘缓冲区 | `kbd_buf[128]` ring buffer | `tty` 的 flip buffer（环形） |
| 生产者 | `kbd_isr` 推字符 | 键盘 ISR → `tty_insert_flip_char` |
| 等待者登记 | `kbd_waiter`（单变量） | `wait_queue_head_t`（等待队列，可多人） |
| 阻塞睡眠 | `sched_block` 设 `PROC_BLOCKED` | `set_current_state(TASK_INTERRUPTIBLE)` + `schedule()` |
| 事件唤醒 | `wake(pid)` | `wake_up_process()` / `wake_up(&queue)` |
| 检查-睡眠原子 | `cli`/`sti` | 自旋锁 `spin_lock_irqsave` 关抢占 |
| 两个唤醒源 | SLEEPING（timer）/ BLOCKED（event） | timer wake / `wake_up` 两条独立路径 |

骨架完全一样。Linux 只是把 `kbd_waiter` 这个单变量换成了**等待队列**（一个事件可以有多个等的人）、把 `wake(pid)` 换成 `wake_up(&queue)`（一次叫醒队列里所有人）。

---

## 九、术语速查

| 术语 | 含义 |
|------|------|
| ring buffer（环形缓冲区） | 定长数组 + head/tail 指针回绕，先进先出 |
| 生产者 / 消费者 | 往缓冲区写的一方 / 从缓冲区读的一方 |
| ISR | Interrupt Service Routine，中断服务程序，这里指 `kbd_isr` |
| 事件（event） | 进程在等的、由别人（通常中断）触发的异步事情 |
| 唤醒（wake） | 把睡着的进程拉回 READY |
| wait_event | "检查条件 + 若不满足则睡眠"的原子模式 |
| lost wakeup（丢失唤醒） | 唤醒发生在检查和睡眠之间，没人接住，进程睡死 |
| cli / sti | 关中断 / 开中断（清/设 IF 标志） |
| IF（Interrupt Flag） | EFLAGS 里的中断标志位，1 开 0 关 |

---

## 十、文件清单

| 文件 | 改动 |
|------|------|
| `my-kernel/include/kbd.h` | 新增：`kbd_isr` / `kbd_getchar_nonblocking` / `kbd_buffer_count` / `getchar` / `kbd_wake_waiter` |
| `my-kernel/kernel/kbd.c` | 新增：128 字节环形缓冲区 + scancode→ASCII 表（从 idt.c 搬来）+ `kbd_isr`（推字符 + 叫醒等待者）+ 阻塞 `getchar`（cli/sti 防 lost wakeup）+ Enter 翻成 `'\n'` |
| `my-kernel/include/sched.h` | 加 `PROC_BLOCKED` 状态；声明 `sched_block` / `wake`；导出 `current_pid` |
| `my-kernel/kernel/sched.c` | `current_pid` 改非 static；`wake_sleepers` 只扫 SLEEPING；新增 `sched_block`（设 BLOCKED + int $0x20）+ `wake`（拉回 READY） |
| `my-kernel/kernel/idt.c` | 删掉内联 `kbd_display_scancode`，IRQ1 改调 `kbd_isr()` |
| `stage-3-protected-mode/Makefile` | 加 `kbd.o` |
| `stage-3-protected-mode/kernel.c` | 加 `keyboard_echo` 进程（阻塞 getchar 验证）+ unmask IRQ1 |

---

## 十一、待改进 / 下一步

- **readline**：在 getchar 上叠行编辑——回车提交、退格删除、echo（敲键时屏幕显示）。`getchar` 是"字符流"，readline 把它攒成"一行"。
- **shell**：prompt → readline → 解析命令 → 执行。到这里"输入 A 回车 → echo A"就通了。
- **多 waiter**：现在 `kbd_waiter` 是单变量，只能一个人等键盘。将来多个进程读 stdin 要换成等待队列。
- **`PROC_BLOCKED` 的更通用抽象**：现在事件源只有键盘。将来 fork 的 `wait()`（等子进程结束）、磁盘 IO 等待都用同一个 BLOCKED + wake。到时可以抽成通用的 `wait_event` 宏。
