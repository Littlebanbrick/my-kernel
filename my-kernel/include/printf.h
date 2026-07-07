#ifndef PRINTF_H
#define PRINTF_H

void printf(const char* fmt, ...);

/* Emit one character to the shared console cursor (handles '\n' and
 * '\b').  Used by readline's echo and any direct char output. */
void putchar_one(unsigned char c);

#endif
