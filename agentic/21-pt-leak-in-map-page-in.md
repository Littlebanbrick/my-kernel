# 21 — PT 页泄漏:map_page_in 的隐式分配

## 现象

`spawn` 命令完整跑一遍(create 子进程 → 子进程 exit → 父进程 wait 收尸)后,`mem` 显示的 free pages 比跑之前**少 1 页**。机制本身是对的:子进程确实被收尸、PCB 槽位确实被复用、exit code 确实传回了父进程。所以泄漏不在 spawn/wait/zombie 这套机制里,而在更底层的**地址空间建立**那一步。

这个泄漏不是这次 spawn 才引入的——地址空间隔离实验(`[[16-address-space-isolation]]`)就在,只是那时没有 spawn+reap 的完整闭环来暴露它。spawn 把它端到端跑出来才看见。

---

## 根因:map_page_in 会偷偷再 alloc 一个 PT

看 `create_process` 里给子进程装私有页的那段(修之前):

```c
p->priv_phys = (u32)alloc_page();   // 私有物理页
p->priv_pt   = (u32 *)alloc_page(); // 装 PTE 的页表页
...
for (i = 0; i < PT_ENTRIES; i++)
    p->priv_pt[i] = 0;
map_page_in(p->page_dir, USER_PRIVATE_BASE, p->priv_phys,
            PAGE_PRESENT | PAGE_WRITE);
```

看起来 priv_pt 已经 alloc 了,只差把 PTE 填进去。**但 `map_page_in` 不知道 priv_pt 的存在**——它只接收一个 PD 指针和虚拟地址,然后自己判断"这个虚拟地址对应的 PDE 在不在"。看 `paging.c:map_page_in`:

```c
if (!(page_dir[pdx] & PAGE_PRESENT)) {
    pt = (u32 *)alloc_page();      // ← 隐式分配!
    zero_page(pt);
    page_dir[pdx] = PAGE_ENTRY((u32)pt, PAGE_PRESENT | PAGE_WRITE);
} else {
    pt = (u32 *)(page_dir[pdx] & 0xFFFFF000);
}
pt[ptx] = PAGE_ENTRY(paddr, flags);
```

此刻 `p->page_dir[512]` 还是 0(PDE absent,因为 priv_pt 虽然分配了,却还没被装进 PD)。于是 `map_page_in` 走进 if 分支,**又 alloc 了一个 PT 页**,把它装进 PD[512],然后往这个新 PT 里填 PTE。

结果:子进程实际占用 **5 页**(stack + page_dir + priv_pt + priv_phys + map_page_in 隐式分配的那个 PT),但 PCB 只记录了前 4 个字段。`reap_proc` 释放前 4 个,**漏了 map_page_in 内部分配的第 5 个**。一个进程漏 1 页。

**核心错配**:priv_pt 这个页表页被分配了两次、各装一份——一份是 `create_process` 显式 alloc 的(priv_pt),一份是 `map_page_in` 在不知道前者存在时隐式 alloc 的。PD[512] 最后指向的是后者(覆盖关系由调用顺序决定:map_page_in 写 PDE 时把 priv_pt 那份挤掉了)。priv_pt 成了孤儿,既没被 PD 引用、也没被 reap。

---

## 修法:让 priv_pt 成为 PT 的唯一主人

思路很简单:**在调 map_page_in 之前,先把 priv_pt 装进 PD[512]**。这样 map_page_in 进去一看 PDE 已存在,直接走 else 分支拿到 priv_pt,只填 PTE,不再 alloc。

```c
for (i = 0; i < PT_ENTRIES; i++)
    p->priv_pt[i] = 0;
p->page_dir[USER_PRIVATE_BASE >> 22] =
    PAGE_ENTRY((u32)p->priv_pt, PAGE_PRESENT | PAGE_WRITE);  // 先装 PDE
map_page_in(p->page_dir, USER_PRIVATE_BASE, p->priv_phys,
            PAGE_PRESENT | PAGE_WRITE);                        // 再填 PTE
```

现在 priv_pt 是 PD[512] 指向的那个唯一 PT 页,被 PCB 的 `priv_pt` 字段跟踪,`reap_proc` 释放它就完整回收了私有地址空间。map_page_in 退化成"纯填 PTE + invlpg",零分配。

`USER_PRIVATE_BASE >> 22` = `0x80000000 >> 22` = 512,正是 PD 里 priv_pt 该装的那个槽(见 `paging.h` 里 USER_PRIVATE_BASE 的注释)。

---

## 验证

跑 `spawn` 前后 `mem` 对比:

```
baseline:  32635 free pages
after spawn+reap: 32635 free pages   ← 完全相等,net 0
```

注意 buddy 分配器的 free **块**计数变了(baseline 是 13 个块,跑完是 17 个块)——那只是因为释放回去的页分散在不同 order,合并情况不同,块数变了但**页**总数守恒。看的是 `free pages` 那一栏,不是 `free blocks`。

---

## 教训:隐式分配是泄漏的温床

`map_page_in` 这种"按需自动分配页表页"的接口很方便,但**调用方和被调用方对"谁拥有 PT 页"的认知必须一致**。这里崩就崩在:`create_process` 以为自己 alloc 了 priv_pt 就拥有 PT 的所有权,而 `map_page_in` 自顾自又分配了一个,两边各持一份,字段只跟踪一份。

Linux 里 `map_page_in` 的对应物是各种 `pte_alloc_map` / `pmd_alloc`,它们也是按需分配,但分配出来的页表页会被挂进进程的 mm_struct(`pgd→p4d→pud→pmd→pte` 那条链),进程退出时沿着这条链**完整遍历回收**,不会漏。我们没有那条显式的页表链,靠的是 PCB 里几个 `priv_*` 字段手工跟踪——所以凡是用到隐式分配的地方,都得保证 PCB 字段能覆盖到。

更彻底的修法是给进程加一个"私有页表页列表",让 `map_page_in` 分配的 PT 也登记进去(像 Linux 的 pmd 链)。但那是要做 fork/地址空间管理时的大改,现在这个私有页只占一个 PDE、一个 PT 就够,手工 pre-install 是最小正确的修法。等做 fork 时如果要让进程映射多个离散虚拟页,再上"页表页链表"那套。

相关:[[16-address-space-isolation]](每进程 PD 与私有页,这次修的就是它的回收路径)、[[20-wait-and-zombie]](spawn+wait 把这个泄漏端到端暴露出来,那篇笔记里"本次没修"的标注现已过时——见本篇)。
