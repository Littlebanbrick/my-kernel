#ifndef INTERRUPT_FRAME_H
#define INTERRUPT_FRAME_H

#include "types.h"

/*
 * CPU 在 32 位保护模式下触发中断时自动压入栈的寄存器。
 *
 * 同级中断（CPL 不变，我们的情况）只压入三个值：
 *   EFLAGS → CS → EIP    栈顶 = EIP
 *
 * handler 通过 frame->eip 可以知道"中断发生时 CPU 正在跑哪条指令"。
 */
struct interrupt_frame {
	u32 eip;
	u32 cs;
	u32 eflags;
} __attribute__((packed));

#endif
