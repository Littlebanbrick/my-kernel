# 2026 年 7 月路线图

> 本文整理自 2026-07-16 的一次多源调研：105 个 agent、23 个来源、94 条 claim 中 23 条通过 3-0 对抗验证、2 条被驳回。
> 时间敏感信息（版本号、rc 进度）以天为单位变化，动手前请重新核对一手源。
> 凡标注「一手核实」的结论均来自 kernel.org 官方文档或 `torvalds/linux` 源码树；标注「⚠️ 未核实」的为二手社区叙事，本次环境无法交叉验证。

---

## 一、此刻内核在哪个阶段（2026-07-16 实测）

从 `kernel.org` 一手拉到的快照：

```bash
curl https://www.kernel.org/releases.json
```

| 类别 | 版本 | 发布日期 |
|------|------|----------|
| mainline（开发中） | **7.2-rc3** | 2026-07-12 |
| stable | 7.1.3 | 2026-07-04 |
| stable（已 EOL） | 7.0.14 | 2026-06-27 |
| linux-next | next-20260715 | 2026-07-15 |
| longterm (LTS) | 6.18.38 / 6.12.95 / 6.6.144 / 6.1.177 / 5.15.211 / 5.10.260 | — |

**这意味着什么**：现在内核正处于 **7.2 系列的 rc（release candidate）稳定化阶段**，而不是 merge window。rc3 处于 rc6–rc9 收尾区间之前，属于稳定化早期。这个阶段社区只接受 **bugfix 补丁**，不接受新功能。新功能补丁此刻应该已经在 linux-next 里排队，等下一个 merge window。

> 一手核实：`kernel.org/releases.json` + `finger_banner`（2026-07-16 直接 curl 实测），3-0 验证通过。

---

## 二、开发周期机制（节奏与治理）

### 1. 节奏：2-3 个月一轮

官方 `Documentation/process/2.Process.rst` 原文确认的模型：

- 每个主版本约 **2–3 个月（9–10 周）** 发布一次；
- 每次发布后开约 **2 周 merge window**，期间 Linus 每天可合并近 **1000 个 changeset**；
- 窗口关闭 → 发布 **rc1** → 进入 **6–10 周（通常 6–7 周）的每周 -rc 稳定化**；
- 一般到 **rc6–rc9（常 rc7）** 出最终 stable。

### 2. 治理：Linus 是终点，但几乎不直接接补丁

> "There is exactly one person who can merge patches into the mainline kernel repository: Linus Torvalds. But ... of the over 9,500 patches which went into the 2.6.38 kernel, only 112 (around 1.3%) were directly chosen by Linus himself."

真正把关的是 **subsystem maintainer 的信任链（lieutenant system）**。所以：

> "Sending patches directly to Linus is not normally the right way to go."

**你的补丁应发给对应子系统的 maintainer，不是 Linus。** 找不到 maintainer 时，Andrew Morton（`akpm@linux-foundation.org`）是"最后手段 maintainer"。

### 3. linux-next：下一个 merge window 的预览树

- 由 **Mark Brown** 维护（已从 Stephen Rothwell 交接）；
- 它是"mainline 在下一个 merge window 关闭后的预期快照"；
- **凡是要在某个 merge window 被合并的补丁，理应事先进过 linux-next**；
- Andrew Morton 的 `-mm` 树现在只作为最后手段，处理约 5–10% 的补丁。

### 4. stable / LTS：跟 Linus 无关

stable 和 longterm（LTS）**不由 Linus 维护**，而由 **Greg Kroah-Hartman 与 Sasha Levin** 维护：
- stable 约每周按需发布，只 backport 重要 bugfix；
- longterm 仅 backport 重要 bugfix。

> 以上全部来自 `2.Process.rst` / `releases.html` / `submitting-patches.rst`，3-0 验证通过。

---

## 三、进入社区的正确姿势（重点纠偏）

### ❌ 不要从"改拼写错误 / 小代码风格补丁"入门

这是最容易踩的坑。官方文档明确警告：

> "some developers jump into the creation of patches fixing spelling errors or minor coding style issues. Unfortunately, such patches create a level of noise which is distracting ... increasingly, they are looked down upon."

**这类补丁现在被视为干扰噪音，越来越被嫌弃。** 不要从这里开始。

### ✅ 推荐入口

Andrew Morton 给的 #1 建议：

> "The #1 project for all kernel beginners should surely be: make sure that the kernel runs perfectly at all times on all machines which you can lay your hands on."

具体来说：

1. **让内核在你手头所有机器上跑稳** —— 测试本身就是贡献；
2. **去清当前的 regressions 与 open bugs 列表** —— "gain experience ... building respect"，在积累经验的同时赢得社区尊重；
3. **KernelNewbies 项目**（`kernelnewbies.org`，含网站 + 邮件列表 + IRC）—— 标准新手入口；
4. **Kernel Janitors 项目**（`kernelnewbies.org/KernelJanitors`）—— 列出相对简单的清理任务，可挑着做。

### ⚠️ IRC 频道纠正：是 OFTC，不是 Libera

`#kernelnewbies` 频道在 **OFTC**（`irc.oftc.net`），不是 Libera Chat。注意这个频道**只谈内核开发**，不灌水。

### 找导师：Outreachy

Outreachy 是一个 **带薪（$7,000 USD stipend）、远程、3 个月**的实习项目，把申请者配给 FOSS 社区的资深 mentor。这是有文档记录的、有导师带教的内核贡献入口。2026 年 5 月那一期的初始申请已于 2026-02-06 开放。GSoC（Google Summer of Code）同理关注周期。

> 一手核实：`outreachy.org`、`kernelnewbies.org/IRC`、`howto.rst`，3-0 验证通过。

---

## 四、第一个补丁的机制（端到端流程）

### 1. 找收件人 —— `scripts/get_maintainer.pl`

```bash
scripts/get_maintainer.pl <你的patch文件>
```

把 patch 路径作为参数传入，它会告诉你该发给哪个 subsystem maintainer、Cc 哪些邮件列表。**只发 `linux-kernel@vger.kernel.org` 会被无视。**

### 2. 自检风格 —— `scripts/checkpatch.pl`

```bash
scripts/checkpatch.pl <你的patch文件>
```

官方 `submit-checklist.rst`：

> "Check for trivial violations with the patch style checker prior to submission (scripts/checkpatch.pl). You should be able to justify all violations that remain in your patch."

注意被验证驳回的两条常见误述：
- **checkpatch 是顾问性工具**，有误报，maintainer 仍可行使判断，它不替代人类判断。残留的违规必须能逐条给出理由，而不是"零警告才能提交"。
- **行宽硬上限不是 80 列**。现行偏好 80 列、硬上限已放宽到 100 列；且 `printk` 等用户可见字符串**绝不换行**（会破坏 grep）。

### 3. 生成与发送 —— `git format-patch --base` + `git send-email`

```bash
git format-patch --base=<base-commit> -1 <commit>
git send-email <生成的.patch>
```

关键规则（`submitting-patches.rst`）：

- **纯文本 inline 邮件**，禁止 MIME 附件、压缩文件、链接（"No MIME, no links, no compression, no attachments. Just plain text."）；
- 主题格式：`[PATCH M/N] subsystem: summary phrase`，summary **不超过 70–75 字符**；
- 用 `--base` 记录 base commit，生成 `base-commit:` trailer，让 reviewer 与 CI 能干净 `git am`；
- **正式发送前，先发给自己**，存成含 headers 的纯文本，确认能 `git am` apply。

### 4. Signed-off-by —— 这是法律声明

`Signed-off-by` 行实现 **Developer Certificate of Origin (DCO) 1.1**，是法律上的权利声明（声明你有权按文件所示开源 license 提交该工作）。

```bash
git commit -s   # 自动加 Signed-off-by
```

- SoB 链须反映补丁到达 Linus 的**真实路径**，首个 SoB 标识主要作者；
- 多作者用 `Co-developed-by:`，且**最后一个 SoB 必须是实际发送者**。

### 5. 构建验证门槛（`submit-checklist.rst`）

补丁必须能干净构建：
- 相关/修改的 CONFIG 选项设为 `=y`、`=m`、`=n` 时**均无 gcc/linker warning 或 error**；
- 通过 `allnoconfig` 与 `allmodconfig` 构建。

这是 maintainer 与 kernel test robot 据此判退的硬门槛。

### 6. 代码风格 —— tab = 8 字符

> "Tabs are 8 characters, and thus indentations are also 8 characters. There are heretic movements that try to make indentations 4 (or even 2!) characters deep, and that is akin to trying to define the value of PI to be 3."

**用 tab、8 字符宽。** 4 或 2 字符缩进被显式拒斥。

### 7. 回应 review —— 修正后重发，别防御

`howto.rst` 列的禁忌：不要 `become defensive`，不要 `resubmit the patch without making any of the requested changes`。正确做法：**把所有问题改掉，重发整条 series**。有正当理由就清晰陈述。

> 以上流程部分全部来自 `submitting-patches.rst` / `submit-checklist.rst` / `coding-style.rst` / `howto.rst`，3-0 验证通过。

---

## 五、当前热点与痛点（2026 年中）

分级说明：能从一手源（内核源码树 `torvalds/linux` master）核实的标 ✅；属于二手社区叙事、本次环境无法交叉验证的标 ⚠️，见文末开放问题。

### ✅ Rust-for-Linux 已实质性进入主线

直接拉取 `torvalds/linux` master 的 `MAINTAINERS` 文件 grep `[RUST]`，得到约 **19 个 [RUST] 子系统条目**，包括：

- `CORE DRIVER FOR NVIDIA GPUS [RUST]`、`DRM DRIVER FOR NVIDIA GPUS [RUST]`（即 **Nova** —— Nvidia GSP 驱动，Nouveau 的继任者）；
- `ASIX PHY DRIVER [RUST]`（`drivers/net/phy/ax88796b_rust.rs`）；
- `PCI SUBSYSTEM [RUST]`、`I2C SUBSYSTEM [RUST]`、`BLOCK LAYER DEVICE DRIVER API [RUST]`、`TRACING [RUST]`、`PWM SUBSYSTEM [RUST]`、`DMA MAPPING & SCATTERLIST API [RUST]` 等。

另有专用邮件列表 `rust-for-linux@vger.kernel.org`（lore 归档 2026-07-16 当天仍活跃，单日 25 条新消息）。

rust-for-linux.com 进一步列出**已合入主线的 Rust 驱动**：Android Binder（v6.18-rc1）、Nova GPU、Tyr GPU（Arm Mali）、Null Block、ASIX/AMCC PHY、DRM Panic QR code 生成器等。

**结论：Rust 驱动与抽象层已落地主线（`CONFIG_RUST`），不再是纯实验分支。** 这是机会信号——Rust 子系统还在快速扩张，是相对年轻、相对缺人、review 文化相对友好的入口。

> 一手核实。但"哪些是真正可用的生产级驱动"需进一步核实——PHY/ACPI/bindings 类条目可能只是绑定层而非完整驱动。

### ✅ EEVDF 调度器已是 fair.c 的核心

直接拉取 `kernel/sched/fair.c`：包含 `Earliest Eligible Virtual Deadline First`、`EEVDF selects the best runnable task from two criteria`、`entity_eligible()` / `vruntime_eligible()` / `update_entity_lag()` / `update_deadline()`。文件头注释仍写 `Completely Fair Scheduling (CFS) Class`，但内部选择逻辑已从纯 vruntime 演进为 EEVDF。

> 一手核实（算法存在）。"6.6 引入"这条历史断言未能在一手源核实，故不在此断言版本号。

### ✅ PREEMPT_RT 已是主线 Kconfig 选项

直接拉取 `kernel/Kconfig.preempt`：

```
config PREEMPT_RT
    bool "Fully Preemptible Kernel (Real-Time)"
    depends on EXPERT and ARCH_SUPPORTS_RT and not COMPILE_TEST
    select PREEMPTION
    help
      This option turns the kernel into a real-time kernel by replacing
      various locking primitives ... with preemptible priority-inheritance
      aware variants, enforcing interrupt threading ...
```

MAINTAINERS 中 Real-time Linux (PREEMPT_RT) 由 **Sebastian Andrzej Siewior、Clark Williams、Steven Rostedt** 维护，状态 `Supported`。

> 一手核实（已是主线选项）。"6.12 完全合入、最后障碍是 printk 重写"是二手说法（kernelnewbies），未在一手源核实，故仅陈述为"已是主线 Kconfig 选项"。

### ✅ io_uring 与 BPF 已深度集成

直接拉取 `io_uring/Kconfig`：

- `CONFIG_IO_URING_BPF`（依赖 BPF + NET）；
- `CONFIG_IO_URING_BPF_OPS`（依赖 BPF_SYSCALL + BPF_JIT + DEBUG_INFO_BTF）；
- `CONFIG_IO_URING_ZCRX`（零拷贝 RX，依赖 PAGE_POOL / INET / NET_RX_BUSY_POLL）。

kernelnewbies 的 Linux 7.1 页面记载 7.1 引入了 **BPF-powered io_uring**（可用 BPF 程序驱动 `io_uring_enter(2)`）。

> 一手核实（集成存在）。成熟度与近期争议未核实。

### ⚠️ 没能在一手源核实的社区叙事

以下是你问到的"flame war / 痛点"，但本次环境的 WebSearch/WebFetch 对 `lwn.net` / `phoronix.com` / `wikipedia.org` 全面返回空结果，**无法交叉验证**，所以严格不放进已确认结论：

- Rust drama 的具体现状（Hellwig 与 Rust DMA 维护者之争、Wedson Almeida Filho 离开项目、Christoph Hellwig 反对 Rust 抽象层）；
- LLM/AI 生成补丁争议在 2026 年是否形成了书面政策（`Documentation/process/` 下是否有 AI-use 指引）；
- EEVDF、io_uring 的近期社区辩论细节。

**这些需要你手动查阅 LKML 归档（`lore.kernel.org`）与 lwn.net 文章核实。** 具体的核实入口见文末。

---

## 六、开放问题（留给你后续核实）

1. **Rust 社区张力的 2026-07 现状**：查 `lore.kernel.org/rust-for-linux/` 归档 + lwn.net 的 Rust for Linux 系列文章。
2. **PREEMPT_RT 完全合入的具体版本**：`git log --grep=PREEMPT_RT`，或查 Linus 在 6.12 周报中的声明。
3. **LLM/AI 补丁争议的现行政策**：检索 `Documentation/process/` 是否新增 AI-use 指引，查 LKML 上 "LLM" / "AI" / "DCO" 相关讨论。
4. **2026 年最值得切入的 regressions/bug-bash 清单**：`bugzilla.kernel.org` 与 regressions 列表的当前入口；Outreachy/GSoC 2026 周期是否开放 kernel 名额。

---

## 七、针对已有 toy kernel 经验的本科生的行动路线

你已经能写 boot sector、保护模式、分页、用户态/syscall、调度器——意味着**你已经具备了内核贡献所需的"专业知识"门槛**，剩下的全是流程与社区层面。建议这样推进：

### 第 0 步（本周）：订阅邮件列表，先看不发

- `linux-kernel@vger.kernel.org`（流量极大，建议只看摘要或按主题筛）；
- 你感兴趣的子系统列表（如 `rust-for-linux@vger.kernel.org`、`linux-sched@vger.kernel.org`）；
- 装 `b4` 工具，它能在本地拉取 lore 上的邮件 thread，比网页看舒服得多。

### 第 1 步（本月）：搭内核编译与调试环境

用你做 toy kernel 时练熟的 QEMU + GDB，这次目标是真内核：

```bash
git clone https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git
cd linux
make defconfig
make -j$(nproc)
qemu-system-x86_64 -kernel arch/x86/boot/bzImage -append "console=ttyS0" -nographic
```

### 第 2 步：潜伏 `#kernelnewbies`（OFTC，不是 Libera）

看别人怎么发补丁、怎么被 review。

### 第 3 步（第一个补丁）：走完一遍 dry-run，先发给自己

```bash
scripts/checkpatch.pl -f <你的patch>
scripts/get_maintainer.pl <你的patch>
git format-patch --base=<base> -1 <commit>
git send-email --to=<你自己> <patch>
```

确认能 `git am` 干净 apply。补丁内容从 regressions/open bugs 列表里挑，**不要发拼写补丁**。

### 第 4 步：真发，并跟踪 review

用 `b4 am <message-id>` 跟踪 review thread，按"修正后重发"的规矩迭代。

---

## 来源

一手源（kernel.org 官方文档 / `torvalds/linux` 源码树）：
- [kernel.org releases.json](https://www.kernel.org/releases.json) — 版本快照
- [finger_banner](https://www.kernel.org/finger_banner) — mainline/stable/linux-next
- [2.Process.rst](https://www.kernel.org/doc/html/latest/process/2.Process.html) — 开发周期与治理
- [submitting-patches.rst](https://www.kernel.org/doc/html/latest/process/submitting-patches.html) — 补丁格式/DCO/get_maintainer
- [submit-checklist.rst](https://www.kernel.org/doc/html/latest/process/submit-checklist.html) — 构建验证清单
- [coding-style.rst](https://www.kernel.org/doc/html/latest/process/coding-style.html) — tab=8 等
- [howto.rst](https://www.kernel.org/doc/html/latest/process/howto.html) — 新手入口/Janitors
- [MAINTAINERS (torvalds/linux)](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/MAINTAINERS) — 19 个 [RUST] 子系统
- [kernel/Kconfig.preempt](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/kernel/Kconfig.preempt) — PREEMPT_RT
- [kernel/sched/fair.c](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/kernel/sched/fair.c) — EEVDF
- [io_uring/Kconfig](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/io_uring/Kconfig) — io_uring+BPF

二手/社区源：
- [kernelnewbies.org/IRC](https://kernelnewbies.org/IRC) — OFTC 纠正
- [kernelnewbies.org/KernelJanitors](https://kernelnewbies.org/KernelJanitors) — Janitors 入口
- [rust-for-linux.com](https://rust-for-linux.com) — 已合入主线 Rust 驱动清单
- [lore.kernel.org/rust-for-linux/](https://lore.kernel.org/rust-for-linux/) — Rust 邮件列表归档
- [outreachy.org](https://www.outreachy.org/blog/2026-02-06/may-2026-initial-applications-open/) — Outreachy 带薪实习入口
