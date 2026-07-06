# 16 — 地址空间隔离（per-process page directory）

## 概述

给每个进程分配一个**独立的页目录（PD）**，调度器在切换进程时换 CR3。同一个虚拟地址在不同进程的视角里落到不同的物理页——这就是"地址空间隔离"。

最终效果（GDB 里看到的）：

```
切换前:  CR3 = 0x1d000  (A 的 PD)  →  0x80000000 翻译到 A 的私有物理页
切换后:  CR3 = 0x82000   (B 的 PD)  →  0x80000000 翻译到 B 的私有物理页
```

同一个 `0x80000000`，A 写进去 'A'、B 写进去 'B'，互相看不到。**这是 fork/exec 的地基**：fork 要复制父进程的 PD，exec 要替换进程的 PD——前提是每个进程先得有自己的 PD。

---

## 起点：之前所有进程共用一个 PD

改之前的内存视图：

```
所有进程 ──→ CR3 = kernel_page_dir ──→ 同一张翻译表
```

`paging_init` 建了 `kernel_page_dir`，恒等映射前 4 MiB。所有进程的 `CS = 0x08`、CR3 从来不变。后果：

- 虚拟地址 `0x12345000` 对 A 和 B 来说**指向同一个物理页**
- 没有"A 能看到、B 看不到"的内存——大家看的是同一张地图
- 进程之间没有任何地址隔离

要做 fork，第一个障碍就在这：fork 的语义是"复制父进程的地址空间"，可现在根本没有"进程自己的地址空间"可言。所以第一步是**让每个进程拥有自己的 PD**。

---

## 关键认知：CR3 = "当前用哪个 PD"的开关

CPU 每次访问内存，地址翻译链是：

```
虚拟地址  →  CR3 寄存器  →  页目录(PD)  →  页表(PT)  →  物理页
                ↑
          当前用哪个 PD，由 CR3 决定
```

**CR3 是一个 CPU 寄存器，里面装着"当前页目录的物理地址"。整台机器只有一个 CR3，所以任意时刻只有一个 PD 是"生效"的。** 谁的 PD 在 CR3 里，CPU 看到的内存就是谁的视角。

所以"切换地址空间" = "换 CR3"。调度器在 `irq0_enter` 切进程时多做一件事——把 CR3 换成下一个进程的 PD 物理地址：

```c
current_pid = next_pid;
__asm__ volatile("mov %0, %%cr3" : : "r"(procs[current_pid].page_dir_phys));
return procs[current_pid].saved_sp;
```

这就是"内核掌控地址空间切换"的本质：**内核决定 CR3 当前指向谁，就决定了 CPU 用谁的视角看内存。**

---

## 三个核心改动

### 1. `clone_kernel_page_dir` —— 克隆 PD，共享内核 PT

第一个直觉可能是"给每个进程建一个空 PD"。**不行**——进程要能调 `printf`、`alloc_page`、访问 VGA，这些都在内核映射里。空 PD 会让进程一跑就 #PF。

正确做法：**克隆内核 PD 的 PDE，但不复制 PT。**

```c
u32 *clone_kernel_page_dir(void)
{
    u32 *pd = (u32 *)alloc_page();
    zero_page(pd);
    for (int i = 0; i < PD_ENTRIES; i++)
        if (kernel_page_dir[i] & PAGE_PRESENT)
            pd[i] = kernel_page_dir[i];   /* 只抄了 PDE 这个指针 */
    return pd;
}
```

关键：抄的是 **PDE（一个 4 字节指针）**，不是 PT 本身。PDE 指向某个 PT 物理页，所有进程的 PD 里内核那几个 PDE 都指向**同一批内核页表**。

```
kernel_page_dir               A 的 PD                    B 的 PD
┌──────────┐                  ┌──────────┐               ┌──────────┐
│ PDE[0]   │──┐               │ PDE[0]   │──┐            │ PDE[0]   │──┐
│ PDE[1]   │  │               │ PDE[1]   │  │            │ PDE[1]   │  │
│ ...      │  │   克隆         │ ...      │  │            │ ...      │  │
│ PDE[511] │  │  ───────→     │ PDE[511] │  │            │ PDE[511] │  │
└──────────┘  │               └──────────┘  │            └──────────┘  │
              │                             │                          │
              ▼                             │                          │
        ┌──────────┐  内核 PT（共享）       │                          │
        │ PT[0]    │  ←─────────────────────┘──────────────────────────┘
        └──────────┘   所有 PD 的内核 PDE 指向同一个 PT 物理页
```

**为什么这样是对的？** 内核映射（代码、`.data`/`.bss`、buddy 元数据、VGA）所有进程都要能访问——共享是设计目标，不是 bug。省内存也是好处：不复制 1024 个 PT，只多了一个 4 KiB 的 PD 页。

**私有映射再加在克隆之上**——只存在于那个进程自己的 PD 里，别的 PD 看不见。

### 2. 私有页 —— 同虚拟地址，不同物理页

每个进程在 `USER_PRIVATE_BASE = 0x80000000`（PDE 索引 512）挂一个私有物理页：

```c
p->priv_phys = (u32)alloc_page();          /* 自己的物理页 */
p->priv_pt   = (u32 *)alloc_page();        /* 自己的 PT 页 */
for (i = 0; i < PT_ENTRIES; i++)
    p->priv_pt[i] = 0;
map_page_in(p->page_dir, USER_PRIVATE_BASE, p->priv_phys,
            PAGE_PRESENT | PAGE_WRITE);
```

这条 `PDE[512] → PT → 物理页` 的链条**只装在 `p->page_dir` 里**。别的进程的 PD 在 `0x80000000` 这个槽位是 0（不存在）。

```
A 的 PD                              B 的 PD
┌──────────┐                         ┌──────────┐
│ PDE[511] = 0  (没映射) │           │ PDE[511] = 0  (没映射) │
│ PDE[512] ──→ A 的 PT ──→ A 的物理页 │           │ PDE[512] = 0  (没映射!) │
└──────────┘                         └──────────┘
```

所以 A 在 `0x80000000` 写 'A'，走 A 的 PD → A 的物理页。B 读 `0x80000000`，走 B 的 PD → **B 的 PD 在 PDE[512] 是空的，#PF**（如果 B 真去读的话——demo 里 B 写自己的私有页，不会碰这条）。

**同一虚拟地址、不同物理页**——这就是地址空间隔离的可见效果。

### 3. `irq0_enter` 切 CR3

```c
current_pid = next_pid;
__asm__ volatile("mov %0, %%cr3" : : "r"(procs[current_pid].page_dir_phys));
return procs[current_pid].saved_sp;
```

切完 CR3，`irq0_handler` 里 `mov %eax, %esp; popa; iret` 之后就跑在下一个进程的地址空间里了。

---

## 一个隐藏坑：`sched_start` 必须手动切 CR3

第一版没在 `sched_start` 里切 CR3，结果**第一个进程一跑就 #PF**。

原因：`do_first_switch` 是直接 `mov esp; popa; iret`，**绕过了 `irq0_enter`**：

```asm
do_first_switch:
    movl 4(%esp), %esp    /* arg1 = saved_sp */
    popal
    iret
```

所以 `irq0_enter` 里那个 `mov cr3` **没机会执行**——这是"第一次切换"的特殊路径。第一个进程带着内核的 PD 跑，访问 `0x80000000` 时内核 PD 没这个映射 → #PF。

修复：`sched_start` 在调 `do_first_switch` 前手动切 CR3：

```c
__asm__ volatile("mov %0, %%cr3" : : "r"(procs[first].page_dir_phys));
do_first_switch(procs[first].saved_sp);
```

**教训**：任何"绕过正常路径"的特殊入口（`do_first_switch` 就是），都要手动补上正常路径里的副作用——**CR3 切换就是这种副作用**。Linux 也有同样的问题：第一个进程的 `context_switch` 之前的初始化要手动设 CR3。

---

## 隔离不彻底的真相：根因是 ring 0，恒等映射只是症状

这个改动做完后，"隔离"是真的——A 够不到 B 的物理页。但**它算不上"安全防护"**。要分清两件事：

### 根本原因：进程跑在 ring 0（内核态）

所有进程的 `CS = 0x08`，CPL=0，最高权限。ring 0 下进程能执行：

| 指令/操作 | 后果 |
|----------|------|
| `mov %eax, %cr3` | **进程能自己改 CR3！** 把任意物理地址塞进 CR3，那个 PD 里映射的所有内存它都能访问 |
| 直接读写 buddy 元数据 | 改自己的 PCB、改内核的 PD——全不拦 |
| `cli`/`sti`/端口 IO | 随便用 |

也就是说，隔离现在是**靠进程自觉**的（process_a 没去偷看 process_b 的内存），不是**靠硬件强制**的。A 只要愿意，写一行 `asm("mov %0, %%cr3" : : "r"(某物理地址))` 就能突破隔离。

> 类比：给每人一个独立保险柜，但保险柜是纸糊的，人人手里还有万能钥匙。"隔离"只在于大家约定好不去撬别人的柜子。

### 真正的隔离需要 ring 3

切到 ring 3（用户态）后，硬件强制：

| 指令/操作 | ring 0 | ring 3 |
|----------|--------|--------|
| `mov cr3` | 可以 | **#GP 直接异常** |
| 访问没带 `PAGE_USER` 的页 | 可以 | **#PF 页错误** |
| `cli`/`sti`/端口 IO | 可以 | **#GP** |
| 访问没在自己 PD 里映射的地址 | / | **#PF** |

ring 3 下，进程**无法**改自己的 CR3，**无法**访问内核没显式给它的页。这时候页目录才从"方便"变成"硬件边界"。

### 那恒等映射算怎么回事？

恒等映射（virt == phys，0–4 MiB 一对一映射）**本身不是安全漏洞的根因，它是 ring 0 设计的一个必然产物**。具体来说：

问题在 `irq0_enter` 那行 `mov cr3`：

```c
current_pid = next_pid;
__asm__ volatile("mov %0, %%cr3" : : "r"(procs[current_pid].page_dir_phys));
return procs[current_pid].saved_sp;   /* 这几条还在旧栈上跑 */
```

切完 CR3 之后、`irq0_handler` 里 `mov %eax, %esp` 之前，CPU 还在**旧进程的栈上**执行这几条指令。如果新 PD 不映射旧栈那个虚拟地址，这几条指令一执行就 #PF 了。

**解决方法**：让栈所在的虚拟地址在所有 PD 里都映射到同一个物理页。最省事的做法就是恒等映射前 4 MiB——所有 PD 都抄了内核 PDE，所以前 4 MiB 在每个进程视角里都一样。

→ 所以**恒等映射是为了让 CR3 切换在中途不崩**而存在的妥协，它是 ring 0 + "IRQ handler 站在进程栈上切 CR3" 这个设计的**症状**，不是病根。

### ring 3 之后恒等映射会被什么取代

1. **内核高半映射**（kernel high-half）：内核映像放在高地址（如 `0xC0000000+`），只在内核 PD 视角里映射、不带 `PAGE_USER`。用户进程的 PD 里要么没这些 PDE、要么有但不带 USER 标志 → 用户态访问就 #PF。
2. **用户态映射**：用户进程的代码/数据/栈映射在低地址，带 `PAGE_USER`。
3. **内核栈与用户栈分离**：中断进入 ring 0 时，CPU 通过 TSS 自动切到内核栈（`ss0:esp0`），不再站在用户进程的栈上切 CR3。这样切 CR3 就不需要恒等映射兜底了。

所以恒等映射确实是要被干掉的东西——但它是**果**不是**因**。因是"进程和内核同处 ring 0"。先把权限分开（ring 3），恒等映射才有理由被替换成内核高半映射。

### 一句话总结隔离的层次

| 层次 | 提供者 | 强度 |
|------|--------|------|
| 现在的 per-process PD | 内核约定 + CR3 切换 | **软隔离**——靠进程自觉，ring 0 能绕过 |
| 真正的隔离 | ring 3 + `PAGE_USER` + 内核高半 | **硬隔离**——硬件强制，用户态绕不过 |

---

## 进程退出后回收地址空间

`reap_finished` 现在多释放三样东西（PD、私有 PT、私有物理页）：

```c
if (p->priv_phys) free_page((void *)p->priv_phys);
if (p->priv_pt)   free_page(p->priv_pt);
if (p->page_dir)  free_page(p->page_dir);
```

**不释放共享内核 PT**——它们还在被内核和别的 PD 用。一个进程退出只回收它独占的部分（PD 页 + 私有 PT 页 + 私有物理页），共享部分不动。

`i != current_pid` 守卫依旧必要：FINISHED 的当前进程不能在这一 tick 回收，它的栈还被 `irq0_handler` 用着——下一 tick 切到别人后再回收。

---

## 验证（QEMU + GDB）

### GDB：CR3 在进程间切换

`objdump -t build/stage3.elf | grep procs` 找到 `procs` 基址，在 `irq0_enter` 下断点，连续 tick 打印 `$cr3` 和各进程的 `page_dir_phys`：

```
1st: cur=0 cr3=1d000  A.pd_phys=1d000  B.pd_phys=82000
2nd: cur=1 cr3=82000   ← 切到 B，CR3 变了
3rd: cur=2 cr3=87000   ← 切到 idle，CR3 又变
4th: cur=0 cr3=1d000   ← 切回 A，CR3 回到 1d000
```

**CR3 严格跟随当前进程的 `page_dir_phys`**——地址空间真的在切换。

### 屏幕：隔离生效

跑 60+ 秒，输出全部是：

```
A sees: A
B sees: B
A sees: A
...
```

**A 永远只看到 A，B 永远只看到 B**。如果隔离失败（共享私有页），会看到 `A sees: B`——一次都没出现。同一虚拟地址 `0x80000000` 在 A 和 B 的视角里指向**不同的物理页**，这正是地址空间隔离的本质。

### 稳定性

- 60+ 秒无 triple fault、无 panic
- A/B 都退出后，reap 正确回收了 PD + PT + 私有页，idle 继续待机

---

## 文件清单

| 文件 | 改动 |
|------|------|
| `my-kernel/include/paging.h` | 声明 `clone_kernel_page_dir`、`map_page_in`；加 `USER_PRIVATE_BASE` |
| `my-kernel/kernel/paging.c` | `map_page` 拆成 `map_page_in(pd, ...)` + 薄壳；新增 `clone_kernel_page_dir` |
| `my-kernel/include/sched.h` | `pcb` 加 `page_dir`/`page_dir_phys`/`priv_pt`/`priv_phys` |
| `my-kernel/kernel/sched.c` | `create_process` 克隆 PD + 分配私有页；`irq0_enter` 切 CR3；`sched_start` 切 CR3；`reap_finished` 释放地址空间页 |
| `stage-3-protected-mode/kernel.c` | A/B 写私有页、循环打印 `X sees: <char>` |
| `stage-3-protected-mode/Makefile` / `boot.S` | SECTORS 60→80（内核镜像超了 60 扇区） |

---

## 和 Linux 的对应

| 概念 | 这里 | Linux |
|------|------|-------|
| 进程地址空间 | per-process PD | `struct mm_struct`（含 `pgd`） |
| 内核映射共享 | 克隆内核 PDE（共享 PT） | 所有进程的内核 PDE 指向同一批内核 PT（内核高半） |
| 切换地址空间 | `irq0_enter` 里 `mov cr3` | `switch_mm()` 里 `load_cr3()` |
| 私有映射 | `map_page_in` 装 PDE | `vm_area_struct` + `pte` |
| 地址空间销毁 | `reap_finished` 释放 PD/PT/私有页 | `exit_mmap()` + `pgd_free()` |
| 用户态隔离 | 无（全 ring 0） | ring 3 + `PAGE_USER` + 内核高半 |

---

## 术语速查

| 术语 | 含义 |
|------|------|
| 页目录（PD） | 4 KiB 页，1024 个 PDE，是地址翻译的根 |
| 页表（PT） | 4 KiB 页，1024 个 PTE，由 PDE 指向 |
| CR3 | CPU 寄存器，存"当前生效的 PD 物理地址" |
| 恒等映射 | virt == phys，让内核在开分页前后都能跑 |
| 内核高半 | 把内核映像放高地址（如 0xC0000000+），用户态访问不到 |
| ring 0 / ring 3 | 内核态 / 用户态，硬件特权级 |
| PAGE_USER | PDE/PTE 标志位，允许 ring 3 访问 |
| clone（PD） | 复制 PDE 指针，共享底层 PT——不复制 PT 本身 |

---

## 待改进 / 下一步

- **fork**：复制父进程的 PD（不是克隆内核 PD），子进程得到父进程私有映射的副本（COW 或即时复制）
- **exec**：扔掉当前 PD，建一个新的、只映射内核 + 新程序镜像的 PD
- **栈隔离**：现在栈在恒等映射的 4 MiB 里，所有进程的栈 VA 都在那儿——没真正隔离。需要 ring 3 + 内核高半后才能把栈挪到用户区
- **ring 3**：硬隔离的前提——`mov cr3` 拦住、`PAGE_USER` 拦住越界访问。fork/exec 的"用户可见"版本依赖它
- **缺页处理**：现在 #PF 直接死机。fork/exec 的 COW、demand paging 都需要一个能修映射再重执行的 #PF handler
