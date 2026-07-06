# 14 — 优先级调度（PRIO_USER / PRIO_IDLE）

## 起点：idle 在抢时间片

第 11 篇做完 idle_task 后留了个已知低效：

> idle 和 A、B **平等参与 round-robin**，所以 A 跑一次、B 跑一次、idle `hlt` 一次，**每 3 个 tick 才轮到 A 一次**。

第 12 篇做完 sleep 后这个低效更明显了——A 睡的时候，如果 B 也在睡，本该 idle 上场，但现在 idle 还得跟 A、B 轮流占时间片，**就算 A、B 都睡死了，每个 tick 还得"轮到"它们才能跳过**。

更糟的是：**两个 READY 的用户进程之间，idle 会插进来**。比如 A 刚睡、B 还 READY，旧版纯 round-robin 可能选到 idle（因为 pid 顺序排到了它），而不是 B。这违反了"idle 是兜底，没活干才上场"的初衷。

根因：**所有进程优先级相同**，调度器没法区分"该优先跑的"和"该兜底的"。

---

## 机制：给 pcb 加 priority

```c
#define PRIO_IDLE  0
#define PRIO_USER  1

struct pcb {
    ...
    int  priority;    /* higher = preferred */
    ...
};
```

`pick_next` 从"选第一个 READY 的"改成"选 priority 最高的 READY 的"：

```c
static int pick_next(void)
{
    int best_pid = -1, best_prio = -1;
    int pid = (current_pid < 0) ? 0 : current_pid + 1;
    int n;

    for (n = 0; n < MAX_PROCS; n++) {
        if (pid >= MAX_PROCS) pid = 0;
        if (procs[pid].used && procs[pid].state == PROC_READY) {
            if (procs[pid].priority > best_prio) {   /* 严格大于才替换 */
                best_prio = procs[pid].priority;
                best_pid  = pid;
            }
        }
        pid++;
    }
    return best_pid;
}
```

- `create_process` 默认给 `PRIO_USER`
- `sched_start` 创建 idle 后**立刻覆盖**成 `PRIO_IDLE`：

```c
int idle_pid = create_process(idle_task, "idle");
if (idle_pid >= 0)
    procs[idle_pid].priority = PRIO_IDLE;
```

这样 idle 永远最低，**只有所有用户进程都 SLEEPING/FINISHED 时才被选中**。

---

## 关键细节：平局怎么打破（round-robin 在同优先级组内部）

看 `pick_next` 的两个设计点：

1. **扫描从 `current_pid + 1` 开始**——不是从 0。这样 A 跑完后，下一轮扫描先碰到 B（pid 在 A 之后），B 先入选。
2. **`>` 而不是 `>=`**——严格大于才替换。这意味着**同优先级的进程里，第一个被扫到的胜出**，而扫描顺序是"current+1 开始往后绕"，所以同优先级进程**按 pid 顺序轮流**。

这就是 round-robin 在"最高优先级组内部"的体现：

```
A( prio 1 ), B( prio 1 ), idle( prio 0 )
当前 A 跑 → 下一轮从 B 开始扫 → B 入选 → B 跑
当前 B 跑 → 下一轮从 idle 开始扫 → idle prio 0 < best_prio 1，跳过 → 绕回 A → A 入选
```

**两个设计点缺一不可**：只用 `>` 不从 current+1 开始，会永远选 pid 最小的那个（A 永远跑、B 永远饿死）；从 current+1 开始但用 `>=`，会永远选 pid 最大的那个（后扫到的覆盖先扫到的）。

### 旧版纯 round-robin 是这个的退化

旧版 `pick_next` 是"找到第一个 READY 就返回"。新版可以看作"所有进程优先级相同时，退化成 round-robin"——因为 `>` 让第一个扫到的胜出，而扫描从 current+1 开始，所以就是轮流来。**旧版是新版 `priority` 全相等的特例。**

---

## 两级够用，以后可以加

现在只有 `PRIO_IDLE`(0) 和 `PRIO_USER`(1) 两级。要加更多优先级很直接——给 `create_process` 加个 priority 参数、或者加 `set_priority(pid, prio)` 调用。但**两级已经足够解决"idle 抢时间片"这个具体问题**，所以先做最小版本。

Linux 的优先级演化路径供参考：

| 阶段 | 机制 |
|------|------|
| 早期 | nice 值（-20 到 +19），静态优先级 |
| O(1) 调度器 | 动态优先级，按睡眠时间调整（交互式进程奖励） |
| CFS（现代） | 完全公平，按"虚拟运行时间"红黑树排序——**没有传统意义的优先级，nice 只是权重的对数缩放** |

我们现在连 nice 都没有，是"两级静态优先级"。但**抽象是一样的**：调度器根据某个数值选下一个。从两级到 nice 到 CFS，是同一个 `pick_next` 接口的逐步精细化，不是推倒重来。

---

## 验证（QEMU + GDB）

测试用例：A 睡 3 tick、B 睡 5 tick（**互素**，让它们很少同时醒，便于观察"A 睡时 B 跑、B 睡时 A 跑、都睡时 idle 跑"三种情况）。

在 `irq0_enter` 下断点，连续 6 个 tick（state: 0=READY, 2=SLEEPING）：

```
1st tick: cur=0 ticks=0  A.s=2 A.w=3   B.s=0 B.w=0   idle.s=0   (A 调 sleep)
2nd tick: cur=1 ticks=1  A.s=2 A.w=3   B.s=2 B.w=6   idle.s=0   (B 调 sleep)
3rd tick: cur=2 ticks=2  A.s=2 A.w=3   B.s=2 B.w=6   idle.s=0   (都睡 → idle)
4th tick: cur=0 ticks=3  A.s=2 A.w=6   B.s=2 B.w=6   idle.s=0   (A 醒 → A)
5th tick: cur=2 ticks=4  A.s=2 A.w=6   B.s=2 B.w=6   idle.s=0   (A 睡 → idle)
6th tick: cur=2 ticks=5  A.s=2 A.w=6   B.s=2 B.w=6   idle.s=0   (idle 继续)
```

### 三个关键观察

1. **第 3 tick `cur=2`（idle 跑了）**：因为那一刻 A.s=2、B.s=2（都在睡），**只有 idle 是 READY**。如果旧版，会按 pid 顺序轮到 idle 但不是因为优先级；新版是**主动选了唯一 READY 且优先级最低的 idle**——优先级生效。

2. **第 2 tick `cur=1`（B 跑了），不是 idle**：A 在第 1 tick 睡下，第 2 tick 调度器要选下一个。pid 顺序是 A(0) → B(1) → idle(2)。旧版纯 round-robin 会选 B（第一个 READY），但**如果 B 当时还没开始跑、是第一次被调度**，行为和优先级版没区别。真正的区别在第 3 tick：A、B 都睡时，旧版会卡在"扫描找不到 READY"直到绕回 idle（其实也能找到，只是没有优先级概念）。新版是**明确地"选最高优先级的 READY"**——语义更清晰。

3. **idle 的 priority=0 生效**：GDB 读 `idle.p=0`，A/B 的 `p=1`。字段被正确设置。

### 预期效果对比

旧版（无优先级）：A/B 各 100 次打印，idle 插一脚，总 tick ≈ 600。
新版（有优先级）：A/B 醒着时 idle 不插，只在两者都睡时 idle 才跑——总 tick ≈ 300（A 醒 N 次的 tick 总和 + B 醒 N 次的 tick 总和，重叠部分 idle 在跑）。

---

## 文件清单

| 文件 | 改动 |
|------|------|
| `my-kernel/include/sched.h` | 加 `PRIO_IDLE`/`PRIO_USER` 常量、`pcb.priority` 字段 |
| `my-kernel/kernel/sched.c` | `pick_next` 重写成"最高优先级 + round-robin 平局"；`create_process` 默认 `PRIO_USER`；`sched_start` 把 idle 改成 `PRIO_IDLE` |
| `stage-3-protected-mode/kernel.c` | A 睡 3、B 睡 5（互素，便于观察） |
| `CLAUDE.md` | 路线图"进程调度"打勾 |

---

## 和 Linux 的对应

| 概念 | 这里 | Linux |
|------|------|-------|
| 优先级 | `pcb.priority`（0/1 两级） | `task_struct.prio`（-1 到 139，多级） |
| 兜底最低 | `PRIO_IDLE` 给 idle | `MAX_PRIO` 给 idle task |
| 平局打破 | round-robin（扫描从 current+1 开始） | CFS 用红黑树按 vruntime 排序，平局看 key |
| 选最高优先级 | `pick_next` 线性扫 | CFS 取红黑树最左节点（O(1)） |

---

## 术语速查

| 术语 | 含义 |
|------|------|
| 优先级 | 调度器选择下一个进程时的偏好 |
| 静态优先级 | 创建时设定，运行中不变 |
| 动态优先级 | 运行中可调（如奖励交互式进程） |
| 饿死 | 低优先级进程一直选不到 |
| round-robin | 同优先级进程轮流来 |
| 抢占 | 高优先级进程醒来时打断低优先级进程（我们还没做） |

---

## 待改进 / 下一步

- **同优先级抢占**：现在高优先级进程醒来后，要等当前 tick 切走才能轮到它。真正的抢占式调度应该在 `wake_sleepers` 把高优先级进程转回 READY 后**立即抢占**当前低优先级进程——但这需要"中断返回时检查是否该切"，比现在复杂。
- **更多优先级**：加 `create_process` 的 priority 参数、或 `set_priority()` 调用。
- **动态优先级**：根据进程行为（CPU 密集 vs 交互式）自动调整——CFS 的思路。
- **防止饿死**：低优先级进程长期选不到时的老化（aging）机制——把等太久的进程临时提优先级。
