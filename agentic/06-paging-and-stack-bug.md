# 06 — 分页实现笔记

## 概述

在 32 位保护模式下实现了标准 x86 分页（2 级页表），把物理内存 bitmap 分配的页通过页表映射到连续的虚拟地址空间。同时发现并修复了一个因栈和物理页分配冲突导致 Triple Fault 的 bug。

---

## 分页初始化（`paging_init`）

### 做了什么

1. 从 bitmap 分配一页（4KB）做页目录（Page Directory, PD）
2. 从 bitmap 分配一页做页表（Page Table, PT）
3. 在页表里填 1024 个条目，把 0x00000000 ~ 0x003FFFFF 逐页 identity map（虚拟地址 = 物理地址）
4. 把页目录物理地址加载到 CR3
5. 设置 CR0.PG = 1，开启分页

### Identity Map 覆盖范围

```
0x000000 ─┬─ IVT + BDA
0x007000 ─┤─ 栈
0x008000 ─┤─ 内核代码（~34KB）
0x0A0000 ─┤─ VGA framebuffer
0x0C0000 ─┤─ 扩展 ROM / BIOS
0x100000 ─┤─ 扩展内存（页目录/页表所在）
0x400000 ─┘─ 共 4MB
```

### 为什么需要 Identity Map

开启分页后，CPU 取指令的下一条地址（EIP）必须能通过页表翻译到正确的物理地址。如果页表不包含当前 EIP 所在页的映射，CPU 会立即触发 Page Fault。

把内核自身所在区域（0~4MB）identity map 是最简单的方案——EIP 不变，但 CPU 现在走页表翻译，访问的效果和以前一样。

---

## 页表映射 API

### `map_page(vaddr, paddr, flags)`

把虚拟地址 `vaddr` 映射到物理地址 `paddr`：

1. 从 `vaddr` 提取 PDX（高 10 位）和 PTX（中 10 位）
2. 查页目录 `kernel_page_dir[PDX]`：
   - 如果没分配页表 → 从 bitmap 分配一页，清零，填进 PDE
   - 如果已有页表 → 从 PDE 中读出页表物理地址
3. 在页表 `pt[PTX]` 填入 `paddr`
4. `invlpg` 刷 TLB

### 地址翻译过程（CPU 自动完成）

```
虚拟地址 0x400000:
  PDX = 0x400000 >> 22        = 1
  PTX = (0x400000 >> 12) & 0x3FF = 0

  CR3 → PD[1] → 页表物理地址
         PT[0] → 物理页 0x13000
         物理地址 = 0x13000 + 0 = 0x13000
```

### `valloc_pages(count)`

简单的 bump allocator，从 0x400000 开始每次分配 `count * 4KB`。不回收、不管理空洞，但对现在的场景足够。

### `tlb_flush_all()`

重新加载 CR3，使整个 TLB 失效。在修改多个页表项后需要调用（`map_page` 内部用 `invlpg` 单页刷，不需要这个）。

---

## 测试验证

```
phys pages: 0x13000  0x14000  0x15000     ← 三个不连续的物理页
virt block: 0x400000 - 0x402fff            ← 连续的虚拟地址块
phys after virt write:
  [0] phys 0x13000 = 0xdeadbeef           ← 通过虚拟地址写，物理地址读回
  [1] phys 0x14000 = 0xcafebabe
  [2] phys 0x15000 = 0x12345678
```

验证逻辑：
1. `alloc_page()` 三次 → 三个物理页（来自 bitmap，地址不连续）
2. `valloc_pages(3)` → 一段连续的虚拟地址
3. `map_page()` 三次 → 散落物理页映射到连续虚拟地址
4. 通过虚拟地址写三个 32-bit 值
5. 通过物理地址读回，值一致 → 映射正确

---

## Stack/Page 碰撞 Bug

### 症状

输出到 `virt block:` 之后程序崩溃（Triple Fault），无错误信息。

### 根因

ESP 初始值 = 0x7000（`stage3.S` 中设置），栈向下生长。`bitmap_init` 只标记了 0x7000~0x7FFF 为已占用，但 0x6000~0x6FFF 被标记为空闲。

调用链 `kernel_main → paging demo → map_page → alloc_page → zero_page` 使 ESP 下降到 0x6Fxx 附近。`map_page` 调用 `alloc_page()` 返回 0x6000 作为新页表，然后 `zero_page(0x6000)` 把 0x6000~0x6FFF 全部清零——**栈上的返回地址和局部变量被清空**。从 `zero_page` 返回时弹出地址 0 → Triple Fault。

### 修复

在 `bitmap_init` 中把 0x000000~0x007000 全部标记为已占用：

```c
mark_used(0x000000, 0x007000);    /* IVT + BDA + 栈缓冲区 */
```

代价：浪费物理内存 28KB（128MB 的 0.02%），换来所有 `alloc_page()` 返回的地址都在 0x100000+，远离栈。

---

## 术语

| 缩写 | 全称 | 作用 |
|------|------|------|
| PD | Page Directory | 页目录，1024 个 PDE，每个管 4MB 虚拟空间 |
| PT | Page Table | 页表，1024 个 PTE，每个管 4KB 物理页 |
| PDE | Page Directory Entry | 页目录条目（4 字节），存页表物理地址 |
| PTE | Page Table Entry | 页表条目（4 字节），存物理页地址 |
| TLB | Translation Lookaside Buffer | CPU 内部的页表缓存 |
| ESP | Extended Stack Pointer | 栈顶指针，向下生长 |
| Triple Fault | — | 异常处理中又发生异常，且持续三次，CPU 复位 |
| Identity Map | — | 虚拟地址 = 物理地址的映射 |

---

## 和 Linux 内核的对应

| 概念 | 这里 | Linux |
|------|-----|-------|
| 物理页分配 | Bitmap 线性扫描 | Buddy System（2 的幂分裂/合并） |
| 虚拟地址分配 | Bump（从 0x400000 递增） | VMA 红黑树（mmap/munmap） |
| 映射时机 | 立即填页表 | Lazy allocation（缺页才分配物理页） |
| 栈保护 | 手动 `mark_used` 保留低位内存 | `CONFIG_VMALLOC` 和 guard page 硬件隔离 |
