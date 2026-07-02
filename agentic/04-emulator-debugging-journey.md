# 模拟器调试之旅：EFER.LME 与磁盘加载问题

> 记录 2026-07-02 的调试过程：在 QEMU、VirtualBox、Bochs 三个模拟器上排查长模式启动失败的原因。

## 背景

stage-3-long-mode 进入 64 位长模式的代码在 QEMU 10.2.1 + KVM 上因为 wrmsr 写 EFER.LME 被静默拒绝而不能工作（已知 issue）。这次尝试用 VirtualBox 和 Bochs 绕过这个硬件虚拟化 bug。

## 磁盘扇区偏移 bug（已修复）

### 错误
在 boot.S 中，`int 0x13, AH=0x02` 的 CL 参数被误改为 1，导致读到的是 boot 扇区本身而不是 stage3 负载。

### 正确值
```asm
movb    $0x02, %cl   /* CL = 2: 从第二个扇区开始读，即 stage3.pad 的开头 */
```

### disk.img 布局
```
扇区 0 (CHS 0/0/1): boot.bin (512B)
扇区 1 (CHS 0/0/2): stage3.pad 开头 = PML4 页表
扇区 2-60: stage3.pad 剩余内容
```

## VirtualBox 结果：失败

VirtualBox 7.2.6（Ubuntu 源）使用 vboxdrv 内核模块，仍然走 VT-x 硬件虚拟化路径。EFER.LME 写入被过滤，日志显示 triple fault。

- 需要将 raw 磁盘镜像（disk.img）转为 `.vdi` 格式
- 磁盘太小（30KB）会报 `VERR_VD_INVALID_SIZE`，需要垫大到 4MB
- UUID 管理问题：每次重建设备文件后需 `closemedium` 清理注册表

## Bochs 结果：失败

Bochs 3.0（GitHub 快照 2025-02-16，自编译）是纯软件模拟器，理论上不应有 VT-x 过滤问题。

### 遇到的一系列问题

1. **磁盘几何参数**：30KB 的磁盘导致 `PCHS=0/16/63`（0 柱面），无法寻址任何扇区。修复：垫大到 63 扇区，设几何参数 `cylinders=1, heads=1, spt=63`。

2. **PDPTR 检查失败**：Bochs 在 32 位 PAE 模式下要求所有 4 个 PDPT 条目有效。由于 EFER.LME=0，CPU 使用 32 位 PAE 页表格式，CR3 被解释为 PDPT 指针。我们的 PML4[1-3]（即 PDPT[1-3]）为 0（not present），导致检查失败。

   修复：在 stage3.S 中填充 PML4[1-3] 为有效条目（0xA003）：
   ```asm
   movl    $0xA003, 0x8008   /* PML4[1]/PDPT[1] */
   movl    $0xA003, 0x8010   /* PML4[2]/PDPT[2] */
   movl    $0xA003, 0x8018   /* PML4[3]/PDPT[3] */
   ```

3. **wrmsr 静默丢弃**：Bochs 的 `core2_penryn_t9600` CPU 模型需要 `msrs="msrs.def"` 文件定义 MSR 行为。缺少此文件时，所有 wrmsr 操作被静默丢弃。

4. **msrs.def 限制**：Bochs 3.0 快照自带的 `msrs.def` 仅包含 4 个条目（0x017、0x02c、0x1b1、0xdb2），且解析器不支持 `0xC0000080`（EFER MSR）这样的大索引号，报 "MSR index is too big"。

### 最终结论
这个 Bochs 3.0 快照版的 CPU 模型对 EFER MSR 的支持不完整，无法用于测试长模式代码。

## QEMU TCG 最终测试：仍然失败

在修复了所有已知 bug（CL=2、磁盘几何）后，使用 `-accel tcg -cpu qemu64`：
```
SetCR0(): PDPTR check failed !
```

说明 TCG 模式下 EFER.LME 也没有被正确设置。三个模拟器全部失败，排除虚拟化层的问题指向 QEMU 10.2.1 本身的 bug。

## 验证了的正确路径

- **最小 32 位 stage3**（不设页表、不进长模式，直接写 VGA）在 Bochs 上正常运行 ✅
- boot 扇区加载（`int 0x13`）正确 ✅
- 跳转到 0xB000 正确 ✅

## 当前状态

代码逻辑是正确的，但没有可用的模拟环境来验证。可能的方向：
1. 降级 QEMU 到 7.x 或 8.x
2. 在 32 位保护模式下继续开发内核功能
3. 后续换电脑或等 QEMU/Linux 更新修复这个 bug
