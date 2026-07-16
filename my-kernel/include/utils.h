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

/* Read/write a 16-bit word from/to an x86 I/O port.  Used by the ATA
 * PIO driver: disk data transfers are 16-bit (one word per bus cycle). */
static inline unsigned short inw(unsigned short port)
{
	unsigned short val;
	__asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}

static inline void outw(unsigned short port, unsigned short val)
{
	__asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

/* Copy `n` bytes from `src` to `dst`.  Freestanding code has no libc, so
 * this is the hand-rolled stand-in for memcpy used by copy-heavy paths
 * (fork's page and frame copies).  Returns dst. */
static inline void *kmemcpy(void *dst, const void *src, unsigned int n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	while (n--)
		*d++ = *s++;
	return dst;
}

#endif