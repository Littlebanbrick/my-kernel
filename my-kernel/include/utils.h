// utils.h — Miscellaneous low-level utilities

#ifndef UTILS_H
#define UTILS_H

#include "types.h"

/* Write a byte to an x86 I/O port */
static inline void outb(unsigned short port, unsigned char val)
{
	__asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Read a byte from an x86 I/O port */
static inline unsigned char inb(unsigned short port)
{
	unsigned char val;
	__asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}

#endif