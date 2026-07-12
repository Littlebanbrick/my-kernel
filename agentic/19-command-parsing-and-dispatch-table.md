# 19 — 命令解析与分派表：让 shell 真正"执行"命令

## 概述

M3 做完 readline 后,shell 还是个"复读机"——不管输入什么,都只打印 `you typed: ...`。M4 的目标:让 shell **解析命令名 + 参数,分派到对应的处理函数**。

先做三个最朴素的内置命令,全部在单进程内完成,不碰磁盘、不碰 exec:

- `help` — 列出可用命令
- `clear` — 清屏
- `echo <args>` — 原样打印参数

这篇笔记记的核心不是这三个命令本身(它们都很简单),而是**命令分派的两条路**和**为什么选函数指针表**。

---

## 一、从一行字符串到一个函数调用

shell 拿到的是 readline 返回的一行字符串,比如 `"echo hello world"`。要把它变成"调用 `cmd_echo("hello", "world")`",分两步:

### 第 1 步:tokenize —— 切成 argv

把一行按空格切成多个 token(词),就是 Unix 的 `argv`:

```
"echo hello world"  →  argv[0]="echo"  argv[1]="hello"  argv[2]="world"  argv[3]=NULL
```

要点:

- **原地修改**:`tokenize` 直接在被切的字符串里把空格替换成 `'\0'`,`argv[i]` 指向原缓冲区里的子串。不分配新内存(我们也没堆)。
- **空格折叠**:连续多个空格当一个分隔符;前导空格跳过。
- **argc 返回值**:token 数。空行 → argc=0。
- **argv[argc]=NULL**:和 `execve` 的约定一致,handler 可以靠 NULL 终止遍历,不必每个命令都数 argc。

这和 C 的 `main(int argc, char **argv)` 完全同构——handler 的签名就是 `(int argc, char **argv)`。**给所有 handler 统一签名**,是分派能统一的前提。

### 第 2 步:dispatch —— 查表调函数

拿到 `argv[0]`(命令名)后,要找到对应的函数并调用。这是这次真正要选路的点。

---

## 二、两条分派路

### 路 A:`if/strcmp` 链

```c
if (str_equal(argv[0], "help"))  cmd_help(argc, argv);
else if (str_equal(argv[0], "clear")) cmd_clear(argc, argv);
else if (str_equal(argv[0], "echo"))  cmd_echo(argc, argv);
else printf("%s: command not found\n", argv[0]);
```

直观,初学者第一反应。**问题:**

1. **线性增长**:每加一个命令,这条链就长一行。20 个命令就是 20 行 if-else。
2. **容易漏改**:加命令要记得同时改两处——写 handler + 在链里加一行。忘一个就失效。
3. **重复结构**:每个分支结构一样(比名 → 调函数),手写出来纯属冗余。

### 路 B:命令表(函数指针表)

```c
struct builtin {
    const char *name;
    void (*fn)(int argc, char **argv);
};

static const struct builtin builtins[] = {
    { "help",  cmd_help  },
    { "clear", cmd_clear },
    { "echo",  cmd_echo  },
};
```

分派:

```c
for (i = 0; i < NUM_BUILTINS; i++)
    if (str_equal(argv[0], builtins[i].name)) {
        builtins[i].fn(argc, argv);
        return;
    }
printf("%s: command not found\n", argv[0]);
```

**优势:**

1. **加命令零改分派代码**:写 handler + 表里加一行,`dispatch` 一个字都不用动。**数据驱动**——行为由表的内容决定,代码不变。
2. **help 自动同步**:`cmd_help` 就是遍历这张表打印名字。加命令→表里多一行→help 自动列出新命令。**单一数据源**(SSOT),不会出现"加了命令但 help 没列"的不一致。
3. **结构清晰**:表 = 命令清单,一眼看全。

### 这就是真 shell / init 的骨架

Linux `init`、busybox、uefi shell、甚至很多游戏的控制台——都是这套**表 + 遍历分派**。区别只是表项更多(name、handler、帮助文本、补全候选……)。我们这张表是最小形:`{name, fn}`。

**选路 B。** 代价就是学一次函数指针语法 `void (*fn)(int, char**)`,一次学会终身受用。

---

## 三、为什么用函数指针,不直接 switch 命令名

有人会问:命令名是个字符串,没法 `switch`(`switch` 要整数常量)。那能不能给每个命令编个号,switch 号?

能,但**多此一举**——你得维护一张"命令名→编号"的映射表,然后 switch 编号。这等于把"命令名→函数"这件事拆成两步("名→号"+"号→函数"),中间多了个完全不必要的间接层。**直接用名字查函数指针,一步到位。**

函数指针表的本质是:**把"分派逻辑"从代码(控制流)搬到数据(数组)**。代码控制流是死的(编译期固定),数据是活的(运行期可变、可遍历)。把逻辑搬进数据,程序就从"写死的行为"变成"数据驱动的行为"——这是从玩具程序走向"系统"的第一步。

---

## 四、几个实现细节

### 1. `str_equal` 而非 `strcmp`

我们没有 libc,`strcmp` 不存在。但其实连 `strcmp`(返回正负零)都用不上——分派只需要判断"相等不相等"。所以写一个只返回 0/1 的 `str_equal`,语义更窄、意图更清:

```c
static int str_equal(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;   /* 都同时到结尾才算相等 */
}
```

最后一行 `*a == *b` 是关键——循环退出时一个可能是 `'\0'` 另一个不是(长度不等),这时不等。只有两个都到 `'\0'` 才相等。

### 2. 空行不报错

`tokenize` 对空行(或全空格)返回 argc=0。`dispatch` 开头 `if (argc == 0) return;`——直接回提示符,不打印"command not found"。这是 shell 的正确行为:敲空回车不是错误。

### 3. `console_clear` 放在 printf.c

清屏要重置光标,而**光标是 `printf.c` 的 `static` 变量**——别的文件碰不到。所以 `console_clear` 必须定义在 printf.c 里(只有它能合法地动 `console_cursor`)。这又是笔记 18 那条原则的体现:**共享状态的所有权归属于定义它的模块**。

如果 `clear` 命令直接去写 VGA 内存,光标不会归零,下次 printf 会从清屏前的旧位置接着写——屏幕看着清了,实际乱了。**清屏 = 清 VGA + 归零光标,缺一不可,且必须由拥有光标的模块做。**

---

## 五、决策记录

| 问题 | shell 怎么把命令名映射到函数 |
|---|---|
| 选项 | 路 A:if/strcmp 链;路 B:函数指针表 |
| 决定 | 路 B(函数指针表) |
| 理由 | 数据驱动;加命令零改分派代码;help 自动同步(SSOT);和真 shell/init 同构 |
| 代价 | 学一次函数指针语法(一次性) |

---

## 六、和 Linux 的对应

| 概念 | 这里 | Linux |
|------|------|-------|
| 命令表 | `builtins[]` 静态数组 | bash 的 `builtin_struct` 数组;busybox 的 `applet` 表 |
| 命令签名 | `(int argc, char **argv)` | 完全一样(C `main` 同构) |
| tokenize | `tokenize()` 按空格切 | glibc `strtok_r` / bash 词法分析器 |
| 未找到命令 | `printf("...: command not found")` | bash 同样文案 |
| 内置 vs 外部 | 只有内置 | 内置(`cd`/`echo`) + 外部(`/bin/ls`,靠 exec) |

我们目前只有**内置命令**(函数直接在内核里)。真 shell 的 `ls`/`cat` 是**外部程序**——shell fork 出子进程,exec 磁盘上的程序。那要等 M6(exec)+ 磁盘文件系统,**是后面的大里程碑**。

---

## 七、下一步可能的方向

M4 完成后,shell 有了骨架。可选的延伸:

- **更多内置命令**:`reboot`(三重故障 / 8042 复位)、`ps`(打印进程表,复用 sched 的 `procs[]`)、`mem`(打印 buddy 空闲链表)。这些是把已有内核状态暴露给人看的练习,很便宜。
- **真正的外部程序加载**:M5/M6——磁盘读 + 简易 FS + exec。这才是 shell 的"正经用法",也是 fork/exec 的归宿。
- **命令行编辑增强**:左右光标、历史记录。锦上添花,优先级低。

我的建议是先做 `ps`/`mem` 这类"把内核状态可视化"的内置命令——它既是 shell 的练习,又是**调试工具**(以后调 fork 时,`ps` 打印进程表比 GDB 看变量快得多)。
