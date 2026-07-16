# linux-kernel/

存放与 **上游 Linux 内核**（upstream Linux kernel）相关的文档，区别于本仓库的 toy kernel：

- Linux 内核源码树的结构、子系统、机制阅读笔记
- Linux 内核社区文化、贡献流程、开发周期、邮件列表礼仪
- 向上游提交补丁的实操记录、复盘与追踪

## 目录索引

- [contributing-roadmap-2026-07.md](contributing-roadmap-2026-07.md) — 2026 年 7 月贡献路线图：开发周期、社区入口、首个补丁机制、当前热点
- [mailing-list-lore-b4.md](mailing-list-lore-b4.md) — 邮件列表 / lore / b4 怎么用：为什么用邮件、三种订阅姿势、到底看什么
- [example-patch-teardown-zh_CN.md](example-patch-teardown-zh_CN.md) — 读一个真实补丁 thread：`[PATCH v3 0/4] docs/zh_CN` 在干嘛

## 与其他目录的分工

| 目录 | 内容 |
|------|------|
| `my-kernel/` | 自己从零写的 toy kernel 源码 |
| `agentic/` | 围绕自己 toy kernel 的学习笔记与概念整理 |
| `linux-kernel/` | 上游 Linux 内核源码与社区/文化文档（本目录） |

> 真正 clone 上游内核源码树时，建议放到本目录之外（如项目根下的 `linux-src/` 或仓库外），避免与文档混淆。
