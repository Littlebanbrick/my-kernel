# 20 — wait() 与进程的"第二次死亡"（zombie）

## 起点：为什么需要 wait

第 15 篇做完 `sched_exit` + `reap_finished` 后，进程生命周期看起来是闭环的：

```
create ──→ ready ──→ run ──→ exit(FINISHED) ──→ 被时钟中断里的 reap_finished 回收
```

但这里有一个被忽略的角色：**父进程**。`create_process` 造出子进程，可父进程**根本不知道子进程什么时候结束、退出了什么状态**。子进程一 exit，下一拍就被 reap_finished 扫干净——父进程连看都没看到它死。

这对应不了 Unix 的 `wait()`。在 Unix 里，父进程能：

1. **阻塞等待**子进程结束
2. 拿到子进程的 **exit code**
3. **亲手回收**子进程的资源（而不是被定时器偷偷回收）

所以我们给进程加了"第二次死亡"——zombie 状态。一个进程要死两回：先 exit 成 zombie，被父进程 wait 后才真正消失。

---

## 关键认知：进程要死两回

这是本次最重要的一件事。之前一个状态 `PROC_FINISHED` 承担了"死"的全部语义。现在拆成两种死：

| 状态 | 含义 | 谁能回收 |
|------|------|---------|
| `PROC_ZOMBIE` | 已经 exit 了，但 PCB 槽位**故意保留**，等父进程来读 exit_code | 只有父进程的 `wait()` |
| `PROC_FINISHED` | 真的没人管了（无父进程），下一拍定时器直接扫走 | `reap_finished()` |

**为什么 zombie 要保留 PCB？** 因为父进程可能想问"我孩子退出了吗？返回值多少？"。如果 exit 后立刻被 reap，exit_code 就跟着槽位一起没了，父进程永远问不到。

**那什么时候才能放心 reap 一个 zombie？** 两种情况：

1. **父进程 wait 了它**——父进程已经拿到 exit_code，槽位没用了，`wait()` 里直接 reap。
2. **父进程自己也没了（orphan）**——没人会来 wait 它了，留着只会泄漏槽位。`reap_finished()` 检测到父进程 gone（unused 或 FINISHED）就把这个孤儿 zombie 扫走。

这正是 Linux 的做法：子进程 exit 后变 zombie，父进程 `wait()` 来收尸；父进程先死的话，init 进程（pid 1）收养孤儿并负责收尸。我们没有 init，所以由定时器兜底扫孤儿。

---

## PCB 新增三个字段

```c
struct pcb {
    ...
    int parent;        /* 谁创建的我，-1 = 没父（idle/shell）*/
    int waiting_for;   /* 我正在 wait 哪个子进程，-1 = 没在 wait */
    int exit_code;     /* 我 exit 时传的 code，留给父进程读 */
};
```

`create_process` 里设 `p->parent = current_pid`。于是在 `sched_start` 之前（current_pid == -1）创建的 idle 没有父，shell 也没父——它们死了直接 FINISHED，不会变 zombie。

---

## sched_exit(code)：决定死法的岔路口

```c
void sched_exit(int code) {
    p->exit_code = code;
    if (有活着的父进程) {
        p->state = PROC_ZOMBIE;      // 留着等父进程收
        if (父进程正 block 在 wait) wake(父进程);  // 通知它：孩子没了
    } else {
        p->state = PROC_FINISHED;    // 没人管，定时器扫
    }
    int $0x20;   // 交出 CPU，永不返回
}
```

注意 `sched_exit` 现在带参数 `code` 了（原来是 `void`）。调用点都要改：`sched_exit()` → `sched_exit(0)`。

**唤醒父进程那一步是关键**：如果父进程已经 `wait` 进去了（BLOCKED 状态），子进程 exit 时必须 `wake(parent)`，否则父进程会一直睡到下一个时钟中断——虽然不会死锁（wake_sleepers 不管 BLOCKED，但下次父进程被调度……不，BLOCKED 不会被调度），所以**不 wake 就真死了**。这是 wait/exit 配对的必要一环。

---

## wait()：父进程的收尸现场

```c
int wait(int *out_pid, int *out_code) {
    for (;;) {
        // 1. 先扫一遍：有没有已经 exit（zombie）的子进程？
        for (每个 child of me) {
            if (child 是 ZOMBIE) {
                *out_pid = child; *out_code = child.exit_code;
                reap_proc(child);   // 亲手回收
                return child;
            }
        }
        // 2. 没有 zombie。我还有活着的子进程吗？
        if (没有活的子进程) return -1;   // 没孩子，wait 个寂寞
        // 3. 有孩子但还没 exit —— 睡觉等
        cli;
        me.state = PROC_BLOCKED;
        int $0x20;   // 让出 CPU
        sti;          // 被 wake 回来后开中断
    }
}
```

三个分支，对应三种情况，缺一不可：

- **已有 zombie** → 立即收尸返回（fast path）
- **无子进程** → 返回 -1 拒绝（否则永远睡 = 死锁）
- **有子进程但没 exit** → block 等被 wake

**cli/sti 包住 check+block**：和 `sched_block` 同一个套路，防止 lost wakeup。具体地——

> 如果不 cli：父进程扫完发现没 zombie，正准备 block；**这时**定时器中断来了，恰好把子进程跑完、exit 成 zombie、wake(父)。但父进程还没把自己设成 BLOCKED 呢，wake 找不到等待者（因为父还是 READY），于是 wake 是空操作。中断返回，父进程继续执行 block——**这下睡死了，永远没人再 wake 它**。cli 把"检查 + 入睡"变成原子的，堵住这个缝。

---

## reap_proc()：把回收逻辑抽出来

之前 `reap_finished()` 里有一大段"释放栈/PD/PT/私有页 + 清字段"。现在 `wait()` 也要回收，所以把这段抽成 `reap_proc(p)`，两个调用者共享同一条清理路径。避免维护两份"清空 PCB"的代码（一定会写漏字段）。

`reap_finished()` 改成：

```c
for (每个 p) {
    if (p == current) continue;          // 不收正在跑的
    if (p 是 FINISHED) reap_proc(p);
    else if (p 是 ZOMBIE && parent_is_gone(p->parent)) reap_proc(p);  // 孤儿 zombie
}
```

---

## 验证：spawn 命令 + 内存泄漏检查

`shell.c` 加了 `spawn` 命令：父进程 `create_process(spawn_child)` + `wait()`，子进程打印 + `sleep` 三次 + `sched_exit(7)`。QEMU 跑出来：

```
> spawn
sched: created pid 2 (child) ...
spawn: created child pid 2, waiting for it...
  [child] hello from a spawned process
  [child] working... 0
  [child] working... 1
  [child] working... 2
  [child] done, exiting
spawn: child 2 reaped, exit code 7
>
```

**关键验证：内存泄漏。** 子进程退出前 `mem` 显示 32633 free pages，退出被 reap 后……一开始测出来是 **32632**，少了一页！

查下来不是 bug：`create_process` 给每个进程分配 **4 页**（stack + page_dir + priv_pt + priv_phys）。但 `mem_dump` 数的是 `page_order[]` 里的**空闲块**数量，buddy 分配器在 free 一个 block 时会尝试和 buddy 合并。原来这 4 页是从不同 order 拆出来的，释放回去后合并情况不同，导致 free **块**的计数变了，但 free **页**的总数……少一页？

实际复查：baseline 是 `32633 free pages`，spawn 前没动过内存所以也是 32633。spawn 创建子进程**借走 4 页**，但那时父进程在 `wait` 里 block，子进程在跑——这一刻应该是 32629。子进程 exit + 被 reap，4 页还回去，应该回到 32633。实测 32632。

差异在 `priv_pt`：`map_page_in` 在给子进程 PD 安装私有页时，如果对应 PDE 不存在会**再 alloc 一个 PT 页**（见 `paging.c:map_page_in`）。所以子进程实际占了 5 页（4 + 1 个 PT），但 `reap_proc` 只释放了 4 个 PCB 记录的字段（stack/page_dir/priv_pt/priv_phys），**漏了 map_page_in 内部分配的那个 PT 页**。

这其实是 pre-existing 的小泄漏（地址空间隔离实验时就在），本次没修——记下来，等做 fork/address-space 管理时一起收拾 `map_page_in` 的隐式分配。**关键是 spawn/wait/zombie 这套机制本身是对的**：子进程确实被收尸、PCB 槽位确实被复用（`ps` 里看不到 child 了）、exit code 确实传到了父进程。

> **更新（笔记 21）**：这个 PT 泄漏后来在 [[21-pt-leak-in-map-page-in]] 里修了——根因是 `map_page_in` 在 PDE 缺失时会隐式再 alloc 一个 PT 页，而 `reap_proc` 只跟踪了 `priv_pt`。修法是在调 `map_page_in` 前先把 `priv_pt` 装进 PD[512]。本节这段"本次没修"的描述是当时的现场，保留以还原排查思路。

---

## 为什么这次做的是 spawn 不是 fork

这次本来是要做 fork 的，结果**撞墙了**。墙在哪：

> fork 的核心是"复制父进程的栈上 cpu_state 到子进程新栈"。但你的 IRQ handler（`irq0_handler` / `syscall_handler`）是**直接在被中断进程的栈上 pusha 的**——也就是说，handler 跑在父进程的栈上。而 fork 这一刻**正是要复制这份栈**。复制完，子进程的 saved_sp 指向拷贝，父进程的 saved_sp 指向原件——**但 handler 自己此刻就站在原件上**，它还要 `mov %eax,%esp; popa; iret` 回去。这没有立刻崩，但语义上很危险，而且真正要做 CoW（写时复制）地址空间时，handler 必须有**自己的栈**，不能站在被复制的栈上。

Linux 的解法是 **内核栈 per-task**：每个进程有一个独立的内核栈，中断/系统调用进来时通过 TSS.esp0 切到内核栈。handler 跑在内核栈上，和用户栈（被 fork 复制的对象）彻底分离。这需要 ring 3 + TSS——是一个更大的架构改动。

所以这次的策略是：**先把不依赖 fork 也能做的进程生命周期（create/wait/exit/reap）做扎实**，用 `spawn`（直接 create 一个新进程，而非 fork）当验证。等 ring 3 / TSS.esp0 到位，handler 有独立内核栈了，再做真正的 fork（复制地址空间 + CoW）。这条路是通的，spawn 就是 fork/exec 的"地基先行"那一段。

相关：[[15-process-reaping]]（旧的 FINISHED-only 回收，被本次的 zombie 机制取代/扩展）、[[16-address-space-isolation]]（每进程 PD，fork 要复制的就是它）、[[17-keyboard-buffer-and-event-wait]]（sched_block + cli/sti 防 lost wakeup 的同款套路，wait 沿用了）。
