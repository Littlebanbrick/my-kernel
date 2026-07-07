// readline.h — line-buffered input built on getchar

#ifndef READLINE_H
#define READLINE_H

/* Read one line of input into `buf` (up to maxlen-1 characters, NUL
 * terminated).  Returns the number of characters stored (excluding the
 * NUL).
 *
 * Behaviour:
 *   - Echoes each typed character to the screen as it is entered.
 *   - Backspace ('\b') erases the last stored character and moves the
 *     cursor back, redrawing the cell with a space.
 *   - Enter ('\n') ends the line: a newline is echoed and the buffer
 *     is NUL-terminated and returned.
 *   - When the buffer is full, further characters (other than Enter
 *     and Backspace) are silently dropped.
 *
 * Blocks inside getchar() when no input is available. */
int readline(char *buf, int maxlen);

#endif
