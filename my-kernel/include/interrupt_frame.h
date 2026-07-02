#ifndef INTERRUPT_FRAME_H
#define INTERRUPT_FRAME_H

#include "types.h"

/*
 * CPU 在触发中断时自动压入栈的寄存器布局。
 * 压栈顺序（从高地址到低地址）：
 *   SS → RSP → RFLAGS → CS → RIP
 *
 * handler 通过指针读到这些值，可以知道"中断发生时 CPU 正在跑哪条指令"
 * 等等现场信息。
 */
struct interrupt_frame {
	u64 rip;
	u64 cs;
	u64 rflags;
	u64 rsp;
	u64 ss;
} __attribute__((packed));

#endif
