#ifndef IDT_H
#define IDT_H

#include "types.h"
#include "interrupt_frame.h"

/* =================================================================
 *  IDT 条目结构体 — 16 字节
 *
 *  CPU 查 IDT 时，用中断向量号作为下标找到对应条目，
 *  从中提取 handler 地址、代码段、权限等信息，然后跳转。
 * =================================================================
 *
 * 位布局（Intel Manual Vol.3, Figure 6-2）：
 *
 *  127          96 95 94   93 92 91 90 89 88 87 86 85  80 79    64
 * +--------------+---+--+--+--+--+--+--+--+--+--+--+--+--------+
 * | offset_high   |P |DPL| 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 0 0 0|
 * | (bits 63-32)  |  |   | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 0 0 0|
 * +--------------+---+--+--+--+--+--+--+--+--+--+--+--+--------+
 *  63           48 47    40 39  37 36   32
 * +--------------+--------+------+------+
 * | offset_mid   | flags  | IST  | res  |
 * | (bits 31-16) |        |      |      |
 * +--------------+--------+------+------+
 *  31           16 15             0
 * +--------------+--------------+
 * | selector     | offset_low   |
 * | (0x08)       | (bits 15-0)  |
 * +--------------+--------------+
 *
 *  翻译成人话，每个字段的作用：
 */

struct idt_entry {
	/*
	 * offset_low / offset_mid / offset_high
	 *
	 * 这三个字段拼起来构成 handler 函数的 64 位地址。
	 * 写条目时把 &handler 拆成三段塞进去，CPU 跳转时拼回来。
	 *
	 *   offset_low  = address & 0xFFFF        (位 0-15)
	 *   offset_mid  = (address >> 16) & 0xFFFF (位 16-31)
	 *   offset_high = (address >> 32) & 0xFFFFFFFF (位 32-63)
	 */
	u16 offset_low;

	/*
	 * selector
	 *
	 * 跳转到 handler 时要切换到的代码段选择子。
	 * 我们 GDT64 里内核代码段是第 2 个条目（第 1 个是空描述符），
	 * 索引为 1，RPL=0 → 0x08。
	 */
	u16 selector;

	/*
	 * ist
	 *
	 * Interrupt Stack Table 编号 (0-7)。
	 * 正常情况下填 0（使用当前栈）。
	 * 特殊异常（double fault = 8 号）需要用独立栈，
	 * 那时填 1-7，指向我们专门分配的 IST 栈。
	 *
	 * 位 3-7 保留，必须为 0。
	 */
	u8  ist;

	/*
	 * flags
	 *
	 * 一个字节，由三个部分组成：
	 *
	 *   位 7    = P (Present)     — 1: 条目有效; 0: 无效
	 *   位 6-5  = DPL            — 访问权限: 0=内核, 3=用户
	 *   位 4    = 0              — 保留，必须为 0
	 *   位 3-0  = Type           — 门类型: 0xE=中断门, 0xF=陷阱门
	 *
	 * 我们最常用的：
	 *   P=1, DPL=0, Type=0xE  →  0x8E  (内核中断门)
	 *   P=1, DPL=0, Type=0xF  →  0x8F  (内核陷阱门)
	 */
	u8  flags;

	u16 offset_mid;
	u32 offset_high;

	/*
	 * reserved
	 *
	 * Intel 手册要求这个 32 位字段必须全是 0。
	 * 如果不填 0，未来 CPU 可能用它做别的事，不兼容当前设计。
	 */
	u32 reserved;
} __attribute__((packed));

/*
 * IDT 指针结构体 — 6 字节
 *
 * 传给 lidt 指令，告诉 CPU 我们的 IDT 表在哪、有多大。
 *
 *   base  = IDT 数组的 64 位地址（也就是 &idt[0]）
 *   limit = sizeof(idt) - 1（因为 limit 是"最大偏移"，不是"个数"）
 */
struct idt_ptr {
	u16 limit;
	u64 base;
} __attribute__((packed));

/*
 * flags 的常用组合常量
 */
#define IDT_PRESENT     0x80

/*
 * DPL（Descriptor Privilege Level）
 * 位 6-5，所以 0x00 = 内核，0x60 = 用户
 */
#define IDT_DPL_KERN    0x00
#define IDT_DPL_USER    0x60

/*
 * 门类型（Type）
 * 中断门 = 0xE：进入时自动关中断（IF=0），防止嵌套
 * 陷阱门 = 0xF：不关中断，适合 int3 断点这种
 */
#define IDT_TYPE_INT    0x0E
#define IDT_TYPE_TRAP   0x0F

/* 常用组合：Present + DPL0 + 中断门 = 0x8E */
#define IDT_KERN_INT    (IDT_PRESENT | IDT_DPL_KERN | IDT_TYPE_INT)
#define IDT_KERN_TRAP   (IDT_PRESENT | IDT_DPL_KERN | IDT_TYPE_TRAP)

/* 初始化 IDT，应在进入长模式后、开中断前调用 */
void idt_init(void);

#endif
