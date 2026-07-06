# 15 — 进程回收（reap FINISHED processes）

## 起点：死进程在占地方

第 11 篇做完 `sched_exit` 后，进程能退出——但只是"标记 FINISHED 然后被调度器跳过"。**进程的栈页还在内存里、PCB 槽位还占着 `procs[]` 表**：

```c
struct pcb {
    ...
    u8  *stack;    /* 分配的 4KB 栈页 —— 退出后没释放 */
    u32  used;     /* 还是 1 —— 槽位没清 */
    ...
};
```

`MAX_PROCS = 8`，跑 8 个进程后槽位全占满，第 9 个 `create_process` 会失败。**这是个内存泄漏 + 资源泄漏**——只是因为测试只跑 2-3 个进程，规模小到看不出问题。

但这事该收尾。一个真正"完整"的进程生命周期应该是：

```
创建 ──→ 就绪 ──→ 运行 ──→ 退出 ──→ 回收
                                    ↑
                                  这一步之前缺了
```

---

## 难点：进程不能自己回收自己

直观想法：在 `sched_exit` 里调 `free_page(stack)` + 清槽位。**但这会立刻崩溃**——因为 `sched_exit` 正在**那个栈上跑**：

```c
void sched_exit(void)
{
    procs[current_pid].state = PROC_FINISHED;
    free_page(procs[current_pid].stack);   // ← 释放自己脚下的栈！
    asm volatile("int $0x20");              // ← 这条指令的返回地址在刚被释放的栈上
}
```

释放栈页之后，下一条指令的执行流（包括 `int $0x20` 本身压栈、`pusha` 保存寄存器）都会**写到已释放的内存**——可能被别的分配覆盖、可能引发 undefined behavior。这是 use-after-free。

**所以回收必须由"别人"做、在"进程让出 CPU 之后"做。**

### 谁是"别人"

最自然的"别人"是**定时器中断**。理由：

1. 它**周期性运行**，迟早会扫到死进程
2. 它**不在任何进程的栈上**——`irq0_handler` 在调度器自己的上下文运行，`pusha` 保存的寄存器虽然在当前进程栈上，但 `irq0_enter` 这个 C 函数是用**当前进程栈**调用的……等一下，这其实是个微妙点。

### 微妙点：`irq0_enter` 也在当前进程栈上

看 `irq0_handler`：

```asm
irq0_handler:
    pushal                ; 保存到当前进程的栈
    pushl %esp
    call irq0_enter       ; 调用 C 函数，仍然用当前进程的栈
    ...
```

所以 `irq0_enter` 跑的时候，ESP 还指着**当前进程**的栈。如果当前进程是 FINISHED，我们能在 `irq0_enter` 里立刻释放它的栈吗？

**不能。** 因为 `irq0_enter` 返回后，`irq0_handler` 还要 `mov %eax, %esp; popa; iret`——这些指令的执行虽然在切换 ESP 之后会用新栈，但**函数返回地址、保存的寄存器**还在当前进程的栈上。释放了就崩。

所以**回收当前进程必须推迟**——等到 `current_pid` 切到别人之后，那个进程的栈才真正没人用。

---

## 解法：`reap_finished()`，每个 tick 扫一遍

```c
static void reap_finished(void)
{
    int i;
    for (i = 0; i < MAX_PROCS; i++) {
        struct pcb *p = &procs[i];
        if (p->used && p->state == PROC_FINISHED && i != current_pid) {
            if (p->stack)
                free_page(p->stack);
            p->used = 0;
            p->state = PROC_READY;     /* 干净状态 */
            p->stack = NULL;
            p->saved_sp = 0;
            p->wakeup_tick = 0;
            p->priority = PRIO_USER;
        }
    }
}
```

在 `irq0_enter` 里、`pick_next` 之前调：

```c
u32 irq0_enter(u32 saved_sp)
{
    g_ticks++;
    if (current_pid >= 0)
        procs[current_pid].saved_sp = saved_sp;
    wake_sleepers();
    reap_finished();          // ← 回收死进程
    next_pid = pick_next();
    ...
}
```

### 三个设计点

1. **`i != current_pid` 守卫**：跳过当前进程。哪怕它是 FINISHED（`sched_exit` 刚标的），也**不能回收它**——它的栈还在被 `irq0_handler` 用。它会**下个 tick**、`current_pid` 切到别人后被回收。这是一 tick 的延迟，无所谓。

2. **`if (p->stack)` 防御**：万一某个进程的 `stack` 已经是 NULL（理论上不该，但防御一下），别调 `free_page(NULL)`。

3. **`procs_count` 不递减**：它记录"曾经创建过的进程总数"，不是"当前活着的进程数"。这样 `pick_next` 和 `sched_start` 不需要特判"槽位空了"——`used` 字段已经表达了"是否在用"。如果将来要导出"当前活进程数"，加个 `live_count` 字段单独统计，别污染 `procs_count` 的语义。

### 为什么放在 `pick_next` 之前

释放的槽位对 `pick_next` 没影响（`pick_next` 本来就跳过 FINISHED），但放在前面让"回收"和"调度"的**时序清晰**：先收尸、再选下一个。语义上像"打扫完战场再派下一个兵"。

---

## 进程生命周期完整了

回收做完后，进程状态机是**真正的闭环**：

```
            create_process
                  │
                  ▼
               READY ◄─────────────┐
                  │ sched_exit      │
                  │ picks it        │ wake_sleepers
                  ▼                 │ (tick 到点)
               RUNNING ──sleep(n)──→ SLEEPING
                  │
                  │ sched_exit
                  ▼
               FINISHED
                  │
                  │ reap_finished (下一 tick)
                  ▼
            [slot freed]
            [stack page freed]
```

**槽位可以复用**——一个进程退出后被回收，那个 pid 槽位可以被下一个 `create_process` 重新使用。这是 fork/exec 的前置条件之一（虽然我们现在还没 fork，但回收把"槽位是动态资源"这件事落实了）。

---

## 验证（QEMU + GDB）

让 A/B 跑完 100 次打印（约 55 秒、2000 tick），attach GDB 读所有 PCB：

```
Final: cur=2 ticks=2001
A:   used=0 state=0 stack=0      ← 完全回收
B:   used=0 state=0 stack=0      ← 完全回收
idle: used=1 state=0 stack=1d000  ← 存活
```

- A、B 的 `used=0`、`stack=0`——**栈页释放、槽位清空**
- idle `used=1`——**idle 没被误回收**（它永远 READY，从不 FINISHED，且是 `current_pid` 时也被守卫跳过）
- `cur=2`（idle 在跑）、`ticks=2001`——系统稳定运行 55+ 秒、2000 tick 没崩、没 panic
- **回收逻辑每个 tick 都在跑**（O(MAX_PROCS)=8 次扫描），长期稳定

### GDB 追踪回收瞬间

在 `reap_finished` 下断点，A 退出后下一 tick 命中：

```
reap hit: cur=1 ticks=552
  A.used=0 A.state=0 A.stack=0      ← A 已被回收（之前退出）
  B.used=1 B.state=1 B.stack=1c000   ← B 刚 FINISHED，等下个 tick 回收
```

注意 `cur=1`（B 在跑），B 此刻是 FINISHED——**它不会被这一 tick 回收**（`i == current_pid` 被守卫跳过），要等下个 tick `current_pid` 切到 idle 后才回收 B。

---

## 关于"由父进程 wait 回收" vs "调度器自动回收"

Linux 的做法是**父进程 `wait()` 回收子进程**——子进程退出后变成 zombie（TASK_DEAD 但 PCB 还在），父进程调 `wait()` 时才释放。这样设计是因为：

- 父进程可能想知道子进程的**退出码**——所以 PCB 要留着
- 父进程可能想看子进程的**资源使用统计**——所以 PCB 要留着

我们没父子关系（所有进程都是 `kernel_main` 直接 create 的），也没退出码机制，所以**调度器自动回收**够用。等做 fork/exec 时，自然会引入父子关系，到时候改成"子进程退出变 zombie、父进程 wait 才释放"——**那是 fork/exec 的一部分**，现在不做。

### zombie 的概念预告

Linux 里子进程退出但父进程还没 `wait`，这个中间状态叫 **zombie（僵尸进程）**——进程已经死了，但 PCB 还占着。父进程 `wait` 时回收。如果父进程先于子进程退出，子进程被 init（pid 1）"收养"，init 会负责 wait。

我们现在没有 zombie 状态——FINISHED 立刻被回收。等做 fork 时再加 `TASK_ZOMBIE`。

---

## 文件清单

| 文件 | 改动 |
|------|------|
| `my-kernel/kernel/sched.c` | 新增 `reap_finished()`，`irq0_enter` 调用之 |

无头文件改动——回收是调度器内部细节，不暴露 API。

---

## 和 Linux 的对应

| 概念 | 这里 | Linux |
|------|------|-------|
| 退出 | `sched_exit` 标 FINISHED | `do_exit` 标 `TASK_DEAD` |
| 回收 | `reap_finished` 自动回收 | `release_task`（父进程 `wait` 时调） |
| 中间态 | 无（FINISHED 立刻可回收） | `TASK_ZOMBIE`（等父进程 wait） |
| 回收者 | 调度器（定时器中断） | 父进程（用户态 `wait` 系统调用） |
| 退出码 | 无 | `exit_code` 存 PCB，父进程 wait 取走 |

---

## 术语速查

| 术语 | 含义 |
|------|------|
| reap | 收割，指回收死进程的资源 |
| zombie | 子进程已退出但父进程还没 wait 的中间态 |
| wait | 父进程系统调用，回收子进程 + 取退出码 |
| use-after-free | 释放了内存后还在用，常见安全漏洞 |
| 收养（reparent） | 父进程先死，子进程被 init 接管 |

---

## 待改进 / 下一步

- **退出码 + wait**：等做 fork 时引入——子进程退出存退出码到 PCB，父进程 `wait` 取走
- **zombie 状态**：等做 fork 时引入——子进程退出后变 zombie，等父进程 wait 才真正释放
- **错误退出 vs 正常退出**：现在 `sched_exit` 没区分"正常退出"和"出错退出"，将来加 `exit(code)`
- **进程间关系**：父子、进程组、会话——这些是 shell / 作业控制的基础
