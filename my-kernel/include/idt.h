#ifndef IDT_H
#define IDT_H

#include "types.h"
#include "interrupt_frame.h"

/*
 * IDT 条目结构体 — 8 字节（32 位保护模式）
 *
 *  63           48 47   40 39  37 36  31           16 15             0
 * +--------------+-------+------+------+--------------+--------------+
 * | offset_high  | flags | 0x00 | zero | selector     | offset_low   |
 * | (bits 31-16) |       |      |      | (0x08)       | (bits 15-0)  |
 * +--------------+-------+------+------+--------------+--------------+
 */
struct idt_entry {
	/*
	 * offset_low / offset_high
	 *
	 * 两个 16 位字段拼起来构成 handler 函数的 32 位地址。
	 *   offset_low  = address & 0xFFFF          (位 0-15)
	 *   offset_high = (address >> 16) & 0xFFFF  (位 16-31)
	 */
	u16 offset_low;

	/* selector: 代码段选择子（0x08 = 内核代码段，索引 1, RPL=0） */
	u16 selector;

	/*
	 * zero
	 *
	 * 在 32 位 IDT 中这个字节必须为 0（64 位时是 IST 字段）。
	 * 位 3-7 也为 0。
	 */
	u8  zero;

	/*
	 * flags
	 *
	 *   位 7    = P (Present)
	 *   位 6-5  = DPL
	 *   位 4    = 0（保留）
	 *   位 3-0  = Type（0xE = 中断门, 0xF = 陷阱门）
	 *
	 * 最常用：P=1, DPL=0, Type=0xE  →  0x8E
	 */
	u8  flags;

	u16 offset_high;
} __attribute__((packed));

/*
 * IDT 指针结构体 — 6 字节
 *
 * 传给 lidt 指令。32 位模式下 base 是 32 位地址。
 *
 *   base  = &idt[0]（IDT 数组的线性地址）
 *   limit = sizeof(idt) - 1
 */
struct idt_ptr {
	u16 limit;
	u32 base;
} __attribute__((packed));

/*
 * flags 常量
 */
#define IDT_PRESENT     0x80
#define IDT_DPL_KERN    0x00
#define IDT_DPL_USER    0x60
#define IDT_TYPE_INT    0x0E
#define IDT_TYPE_TRAP   0x0F

#define IDT_KERN_INT    (IDT_PRESENT | IDT_DPL_KERN | IDT_TYPE_INT)
#define IDT_KERN_TRAP   (IDT_PRESENT | IDT_DPL_KERN | IDT_TYPE_TRAP)
#define IDT_USER_INT    (IDT_PRESENT | IDT_DPL_USER | IDT_TYPE_INT)

/* Handler-address table exported from idt_handlers.S */
extern void *handler_addrs[256];

void idt_init(void);

#endif
