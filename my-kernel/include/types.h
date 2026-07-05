// types.h

#ifndef TYPES_H
#define TYPES_H

typedef unsigned char      u8;    // 1 字节，0-255
typedef unsigned short     u16;   // 2 字节
typedef unsigned int       u32;   // 4 字节
typedef unsigned long long u64;   // 8 字节
typedef signed char        s8;    // 1 字节，-128 到 127
typedef signed int         s32;   // 4 字节，-2^31 到 2^31-1

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif