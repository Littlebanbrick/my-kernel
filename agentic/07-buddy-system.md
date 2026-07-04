# 07 — Buddy System 实现笔记

## 概述

用经典 buddy system 替换了早期的 bitmap 线性扫描分配器。核心思想是把物理内存按 2 的幂大小分块（order），分配时向上取整到 2 的幂，释放时自动合并相邻的 buddy 块。

---

## Buddy System 的原理

### 数据结构

```
free_lists[0]:  4KB  块链表  (2^0 页)
free_lists[1]:  8KB  块链表  (2^1 页)
free_lists[2]:  16KB 块链表  (2^2 页)
...
free_lists[15]: 128MB 块链表 (2^15 页 = 全部物理内存)
```

另外有一个 `page_order[]` 数组（每页 1 字节，共 32KB），记录每页当前所属块的 order：

| 值 | 含义 |
|---|------|
| `0xFF` | ORDER_RESERVED — 不可分配（栈、内核、VGA 等） |
| `0x80 \| o` | 属于一个**空闲**块，块的 order = o |
| `o` (0-15) | 属于一个**已分配**块，块的 order = o |

### Buddy 的定义

对于 order o，一个块的 buddy 是物理地址上相邻的、同样大小的块。

**buddy 计算公式**：`buddy_page = page ^ (1 << order)`

```
例子：order 0（单页）
  页 4 的 buddy = 4 ^ 1 = 5
  页 5 的 buddy = 5 ^ 1 = 4

例子：order 1（2 页）
  块 {4,5} 的 buddy = {6,7}，因为 4 ^ 2 = 6
```

### 分配过程

```
alloc_pages(3)  向上取整 → 需要 4 页 → order 2

1. 从 free_lists[2] 找空块 → 没有
2. 从 free_lists[3] 拿 8 页 → 拆成两块 4 页
   - 一块挂回 free_lists[2]
   - 一块继续往下拆（如果 order 不够）
```

### 释放与合并

```
free_pages(addr)

1. 查 page_order[] 知道这是 order 几的块
2. 用 XOR 算 buddy 地址
3. 如果 buddy 也在 free_lists[order] 里：
   - 把 buddy 从链表移除
   - 两块合并成 order+1
   - 重复第 2 步（尝试继续合并）
4. 把合并后的块挂到对应 free list
```

---

## 初始化过程

1. `page_order[]` 全部设为 0（暂态"未处理"标记）
2. 标记保留区域（ORDER_RESERVED = 0xFF）
3. 扫描所有页，把连续空闲区域拆成 2 的幂大小的块，挂到对应 free list

保留区域和 bitmap 版本一致：

| 范围 | 用途 |
|------|------|
| 0x000000 - 0x006FFF | IVT + BDA + 栈缓冲区 |
| 0x007000 - 0x007FFF | 栈 + boot sector |
| 0x008000 - _end | 内核镜像（含 .bss 的 page_order[]） |
| 0x0A0000 - 0x0C0000 | VGA 视频 + 文本 + ROMs |
| 0x0C0000 - 0x100000 | 扩展 ROM / BIOS |

---

## 和旧 bitmap 的对比

| 特性 | Bitmap | Buddy |
|------|--------|-------|
| 分配策略 | 线性扫描（O(n)） | 分块查找（O(log n)） |
| 释放策略 | 逐位清零（O(n)） | 合并 buddy（O(log n)） |
| 外碎片 | 严重 | 最多 50% 浪费 |
| 碎片合并 | 不合并 | 自动 coalesce |
| 内部元数据 | 4KB bitmap | 32KB page_order[] |
| 对齐约束 | 无（任意连续页） | 块必须 2 的幂对齐 |

---

## 测试验证

```
buddy: 16 order levels, 32768 pages total
kernel  _end = 0x18864
buddy: a=0x19000  b=0x1a000        ← 分配两个单页
buddy: freed both -- should coalesce to order-1  ← 释放（应合并）
buddy: alloc_pages(2)=0x1c000      ← 分配 2 页（从高层 split）
buddy: OK                          ← 写入/读回一致
```

---

## 和 Linux 内核的对应

| 概念 | 这里 | Linux |
|------|-----|-------|
| 空闲链表 | 单链表头插 | `struct free_area` + `struct page` 链表 |
| page_order | 独立 `u8` 数组 | `struct page` 中的 `private` 字段 |
| 分配 | 取链表头 | `__rmqueue()` 带 per-CPU 缓存 |
| 释放 | 移除 buddy，合并 | `__free_one_page()` |
| 最大 order | 15（128MB） | 10（4MB）或 11（8MB）取决于 PAGE_SIZE |
| 外部碎片缓解 | 无 | `vmalloc`、slab、内存压缩 |

Linux 的 buddy 是更精细的版本——per-CPU 缓存减少锁竞争，`pageblock_order` 用于迁移类型分组，`compaction` 在后台整理碎片。但核心算法（buddy 定义、分配时 split、释放时 coalesce）完全相同。

---

## 局限性

- **内碎片**：请求 3 页实际分配 4 页，第 4 页浪费
- **不可分配小内存**：最小粒度 4KB（slab allocator 解决这个问题）
- **虚拟地址空间管理**：仍需要 paging 层把散落物理页映射成连续虚拟地址
