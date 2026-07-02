/* idt_handlers.S — 256 interrupt trampolines
 *
 * Each trampoline pushes the vector number (and a dummy error code
 * if the CPU doesn't supply one), then jumps to handler_common
 * which saves registers and calls handle_exception() in C.
 *
 * Why not just use __attribute__((interrupt))?
 * That attribute has no way to tell the C function WHICH vector fired.
 * We need distinct entry points so each one can hard-code its number.
 *
 * Note: .altmacro is deliberately NOT used; in altmacro mode $0 and $n
 * are positional argument references, shadowing immediate operands. */

.code64

/* ---- Trampoline templates ---- */

/* For vectors that DO NOT have a hardware error code.
 * Push $0 as a placeholder so the stack layout is uniform. */
.macro HANDLER_NOERR vec
.align 4
.globl handler_\vec
.type handler_\vec, @function
handler_\vec:
	push	$0
	push	$\vec
	jmp	handler_common
.endm

/* For vectors that DO have a hardware error code (8, 10-14, 17).
 * The CPU already pushed the error code; we only push the vector number. */
.macro HANDLER_ERR vec
.align 4
.globl handler_\vec
.type handler_\vec, @function
handler_\vec:
	push	$\vec
	jmp	handler_common
.endm

/* ---- Generate all 256 trampolines ---- */

/* 0-7: no error code */
.irp vec, 0,1,2,3,4,5,6,7
	HANDLER_NOERR \vec
.endr

HANDLER_ERR 8		/* Double Fault */

HANDLER_NOERR 9

/* 10-14: error code */
.irp vec, 10,11,12,13,14
	HANDLER_ERR \vec
.endr

/* 15-16: no error code */
.irp vec, 15,16
	HANDLER_NOERR \vec
.endr

HANDLER_ERR 17		/* Alignment Check */

/* 18-255: no error code (in blocks of 16 for readability) */
.irp vec, 18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33
	HANDLER_NOERR \vec
.endr
.irp vec, 34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49
	HANDLER_NOERR \vec
.endr
.irp vec, 50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65
	HANDLER_NOERR \vec
.endr
.irp vec, 66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81
	HANDLER_NOERR \vec
.endr
.irp vec, 82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97
	HANDLER_NOERR \vec
.endr
.irp vec, 98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113
	HANDLER_NOERR \vec
.endr
.irp vec, 114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129
	HANDLER_NOERR \vec
.endr
.irp vec, 130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145
	HANDLER_NOERR \vec
.endr
.irp vec, 146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161
	HANDLER_NOERR \vec
.endr
.irp vec, 162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177
	HANDLER_NOERR \vec
.endr
.irp vec, 178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193
	HANDLER_NOERR \vec
.endr
.irp vec, 194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209
	HANDLER_NOERR \vec
.endr
.irp vec, 210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225
	HANDLER_NOERR \vec
.endr
.irp vec, 226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241
	HANDLER_NOERR \vec
.endr
.irp vec, 242,243,244,245,246,247,248,249,250,251,252,253,254,255
	HANDLER_NOERR \vec
.endr

/* ---- Common handler entry point ----
 *
 * Stack layout after the push sequence above (RSP offsets):
 *   RSP+0:   vector number (pushed last by our trampoline)
 *   RSP+8:   error code (0 or real)
 *   RSP+16:  RIP       \
 *   RSP+24:  CS         |>  interrupt_frame (CPU-pushed)
 *   RSP+32:  RFLAGS    |
 *   RSP+40:  RSP       |
 *   RSP+48:  SS       / */

.align 8
handler_common:
	/* Save all general-purpose registers */
	push	%rax
	push	%rcx
	push	%rdx
	push	%rbx
	push	%rbp
	push	%rsi
	push	%rdi
	push	%r8
	push	%r9
	push	%r10
	push	%r11
	push	%r12
	push	%r13
	push	%r14
	push	%r15

	/*
	 * Call: handle_exception(vec, error_code, &frame)
	 *
	 * Calling convention: %rdi = arg0, %rsi = arg1, %rdx = arg2
	 *
	 * After 15 register pushes (120 bytes), our two pushed values
	 * sit at RSP+120 (vec) and RSP+128 (error_code), followed by
	 * the 5×8-byte CPU frame at RSP+136.
	 */
	movq	120(%rsp), %rdi
	movq	128(%rsp), %rsi
	leaq	136(%rsp), %rdx
	call	handle_exception

	/* Restore registers (reverse order) */
	pop	%r15
	pop	%r14
	pop	%r13
	pop	%r12
	pop	%r11
	pop	%r10
	pop	%r9
	pop	%r8
	pop	%rdi
	pop	%rsi
	pop	%rbp
	pop	%rbx
	pop	%rdx
	pop	%rcx
	pop	%rax

	/* Discard the vector number and error code we pushed */
	add	$16, %rsp

	/* Return to the interrupted code */
	iretq

/* ---- Exported handler-address table ----
 *
 * C code uses this as: extern void *handler_addrs[256];
 * Note: all vectors use the same table; both HANDLER_NOERR and
 * HANDLER_ERR for a given n define the same symbol name handler_n. */

.globl handler_addrs
handler_addrs:
.irp vec, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	.quad handler_\vec
.endr
.irp vec, 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	.quad handler_\vec
.endr
.irp vec, 32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
	.quad handler_\vec
.endr
.irp vec, 48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63
	.quad handler_\vec
.endr
.irp vec, 64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79
	.quad handler_\vec
.endr
.irp vec, 80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95
	.quad handler_\vec
.endr
.irp vec, 96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111
	.quad handler_\vec
.endr
.irp vec, 112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127
	.quad handler_\vec
.endr
.irp vec, 128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143
	.quad handler_\vec
.endr
.irp vec, 144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159
	.quad handler_\vec
.endr
.irp vec, 160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175
	.quad handler_\vec
.endr
.irp vec, 176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191
	.quad handler_\vec
.endr
.irp vec, 192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207
	.quad handler_\vec
.endr
.irp vec, 208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223
	.quad handler_\vec
.endr
.irp vec, 224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239
	.quad handler_\vec
.endr
.irp vec, 240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
	.quad handler_\vec
.endr
