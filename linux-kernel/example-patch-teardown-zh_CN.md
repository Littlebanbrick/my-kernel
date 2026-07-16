# 读一个真实补丁 thread：`[PATCH v3 0/4] docs/zh_CN` 是在干嘛

> 一个从 lore.kernel.org/rust-for-linux/ 找到的真实样本，用来校准"补丁邮件长什么样、review 在纠结什么"。
> 对应 [[mailing-list-lore-b4]] 里说的"看什么"之第 1、2 点。

## 一、样本

```
[PATCH v3 0/4] docs/zh_CN: update rust documentation translations
 2026-07-14  8:59 UTC  (6+ messages)
` [PATCH v3 1/4] docs/zh_CN: Update rust/quick-start.rst translation
` [PATCH v3 2/4] docs/zh_CN: Update rust/general-information.rst translation
` [PATCH v3 3/4] docs/zh_CN: Update rust/arch-support.rst translation
` [PATCH v3 4/4] docs/zh_CN: Update rust/testing.rst translation
```

**一句话**：4 个补丁组成的 series，更新 4 个 Rust 文档的简体中文翻译，第 3 次迭代。

## 二、拆解 `[PATCH v3 0/4]`

方括号里三条信息：

| 片段 | 含义 |
|------|------|
| `PATCH` | 这是一封补丁邮件（不是普通讨论） |
| `v3` | 第 3 版——前两版（v1、v2）被 reviewer 提了意见，作者改完重发 |
| `0/4` | cover letter（封面信），共 4 个补丁的第 0 封，说明"这 4 个补丁一起要做什么" |

接着的 `1/4`～`4/4` 是 4 个补丁本身，每个更新一个文件的中文翻译：
- `1/4` → `rust/quick-start.rst`
- `2/4` → `rust/general-information.rst`
- `3/4` → `rust/arch-support.rst`
- `4/4` → `rust/testing.rst`

## 三、`docs/zh_CN` 是什么

内核源码树 `Documentation/` 下有一套翻译镜像机制：每种语言一个子目录，`zh_CN` = 简体中文，`.rst` 文件和英文版一一对应。

英文版随内核演进不断改，**中文版一旦没跟上就脱节**。这个 series 干的事：把这 4 个文件的中文翻译同步更新到当前英文版状态。

## 四、`6+ messages` 是什么

`(6+ messages)` 是 thread 邮件总数：

- 1 封 cover letter（`0/4`）
- 4 封补丁（`1/4`～`4/4`）
- = 5 封

`6+` 说明**至少还有 1 封回复**——这就是 review 环节。

## 五、翻译补丁的 review 在纠结什么

与技术补丁不同，翻译补丁的 review 焦点通常是：

1. **忠实度**：中文是否准确表达英文原文，有无漏译、错译、自作主张加内容。
2. **术语一致性**：`mutex`、`spinlock`、`build`、`toolchain` 等词，全文档、全子系统的译法要统一。"该不该翻成中文"的争论在中文翻译列表里非常常见。
3. **格式**：`.rst`（reStructuredText）语法不能破坏——代码块标记、链接、列表缩进。翻译时动了格式，文档就渲染不出来。
4. **错别字 / 标点**：中文标点（，。）还是英文标点（, .），全角半角——中文翻译特有纠结点。

**v3 说明至少过了两轮 review**，是接近成熟的补丁。

## 六、为什么这是好的入门样本

对比 [[contributing-roadmap-2026-07]] 里说的"别发拼写/风格补丁"——**翻译补丁不完全属于被嫌弃的那类噪音**：

- 拼写补丁没人 ownership、价值低 → 被嫌弃；
- 翻译补丁有**专门 maintainer**（中文翻译维护者），有明确目的（让中文用户能读文档），是真实的活儿。

翻译补丁能让你**完整跑一遍流程**（`git format-patch` / `get_maintainer.pl` / `git send-email` / 应对 review / 发 v2 v3），又不需要先吃透某个复杂子系统。价值不在"难度"，在"把流程走熟"。

## 七、用 `b4` 把原文拉到本地看（待配置 b4 后动手）

在 lore 网页点进 `0/4` 那封邮件，找到 `Message-ID`（邮件头里那串 `xxx@yyy`），然后：

```bash
b4 am <message-id>
```

会把整个 thread（cover letter + 4 个补丁 + review 回复）拉下来整理成 `.mbx`：

```bash
b4 am <message-id> -o thread.mbox
less thread.mbox                       # 看原始邮件（含 review 讨论）
cd linux/
git am /path/to/thread.mbox            # apply 到本地内核树，看 diff
git log                                # 4 个补丁出现在历史里
git show <hash>                        # 看每个补丁改了哪些中文字
```

`git am` 之后 `git show` 看到的 diff，比网页浏览直观一百倍——会亲眼看到一个翻译补丁的 diff 长什么样。
