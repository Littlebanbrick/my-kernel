/* idt_handlers.S — 256 interrupt trampolines (32-bit protected mode)
 *
 * Each trampoline pushes the vector number (and a dummy error code
 * if the CPU doesn't supply one), then jumps to handler_common
 * which saves registers and calls handle_exception() in C.
 *
 * Stack layout after trampoline (NOERR), from ESP:
 *   +0:  vector      (4 B, our push)
 *   +4:  0           (4 B, dummy error code)
 *   +8:  EIP         (CPU-pushed, same-PL interrupt)
 *   +12: CS
 *   +16: EFLAGS
 *
 * After saving 7 general-purpose registers (28 B):
 *   +28: vector      \_ passed as arg1 and arg2 to C
 *   +32: error       /
 *   +36: EIP          \
 *   +40: CS           |>  struct interrupt_frame (frame → +36)
 *   +44: EFLAGS      /
 */

.code32

/* ---- Trampoline templates ---- */

.macro HANDLER_NOERR vec
.align 4
.globl handler_\vec
.type handler_\vec, @function
handler_\vec:
	pushl	$0		/* dummy error code */
	pushl	$\vec		/* vector number */
	jmp	handler_common
.endm

.macro HANDLER_ERR vec
.align 4
.globl handler_\vec
.type handler_\vec, @function
handler_\vec:
	pushl	$\vec		/* CPU already pushed the real error code */
	jmp	handler_common
.endm

/* ---- Generate all 256 trampolines ---- */

.irp vec, 0,1,2,3,4,5,6,7
	HANDLER_NOERR \vec
.endr

HANDLER_ERR 8		/* Double Fault */

HANDLER_NOERR 9

.irp vec, 10,11,12,13,14
	HANDLER_ERR \vec
.endr

.irp vec, 15,16
	HANDLER_NOERR \vec
.endr

HANDLER_ERR 17		/* Alignment Check */

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

/* ---- Common handler entry point ---- */

.align 8
handler_common:
	/* Save general-purpose registers (cdecl: EAX, ECX, EDX caller-saved) */
	pushl	%eax
	pushl	%ecx
	pushl	%edx
	pushl	%ebx
	pushl	%ebp
	pushl	%esi
	pushl	%edi

	/*
	 * Call: handle_exception(vec, error_code, &frame)
	 * cdecl: push rightmost arg first.
	 *
	 * Offsets after 7 pushes (28 B):
	 *   ESP+28:  vector
	 *   ESP+32:  error code
	 *   ESP+36:  EIP  ← frame pointer
	 *   ESP+40:  CS
	 *   ESP+44:  EFLAGS
	 *
	 * cdecl pushes arguments right-to-left.  Each push shifts ESP
	 * down by 4, so the offset of the next argument stays at +36:
	 *
	 *   After 0 pushes: frame at ESP+36
	 *   After 1 push:   error at (ESP+4)+32 = ESP+36
	 *   After 2 pushes: vec   at (ESP+8)+28 = ESP+36
	 */
	leal	36(%esp), %eax		/* arg3: frame ptr = address of saved EIP */
	pushl	%eax
	movl	36(%esp), %eax		/* arg2: error code */
	pushl	%eax
	movl	36(%esp), %eax		/* arg1: vector */
	pushl	%eax
	call	handle_exception
	addl	$12, %esp		/* pop 3 args */

	/* Restore registers */
	popl	%edi
	popl	%esi
	popl	%ebp
	popl	%ebx
	popl	%edx
	popl	%ecx
	popl	%eax

	/* Pop vector and error code */
	addl	$8, %esp

	/* Return to interrupted code */
	iret

/* ==================================================================== */
/*  Scheduler entry points                                               */
/* ==================================================================== */

/* do_first_switch(saved_sp) — switch into the very first process.
 *
 *   arg1 = saved_sp (passed on the stack, cdecl)
 *
 *   Load the saved stack pointer, popa the fake cpu_state laid out
 *   by create_process(), then iret into the process entry point.
 *   Never returns. */
.align 8
.globl do_first_switch
.type do_first_switch, @function
do_first_switch:
	movl	4(%esp), %esp		/* arg1 = saved_sp */
	popal				/* restore 8 GP registers */
	iret				/* pop EIP, CS, EFLAGS → run process */

/* irq0_handler — dedicated IRQ 0 entry point (vector 32).
 *
 * Stack on entry:
 *   +0  EFLAGS       (CPU-pushed)
 *   +4  CS           (CPU-pushed)
 *   +8  EIP          (CPU-pushed)
 *
 * We `pusha` to save the 8 GP registers, turning the stack into a
 * struct cpu_state (see sched.h).  We pass ESP (the address of that
 * saved state) to irq0_enter(); it returns the ESP to switch to
 * (the next process's saved cpu_state).  We switch to it, popa,
 * iret — control resumes in the next process. */
.align 8
.globl irq0_handler
.type irq0_handler, @function
irq0_handler:
	pushal				/* save 8 GP regs → struct cpu_state */
	pushl	%esp			/* arg1 = saved_sp (addr of cpu_state) */
	call	irq0_enter
	addl	$4, %esp		/* discard arg */
	movl	%eax, %esp		/* switch to the returned stack */
	popal				/* restore the next process's regs */
	iret				/* pop EIP, CS, EFLAGS → resume */


/* ---- Exported handler-address table ---- */

.globl handler_addrs
handler_addrs:
.irp vec, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	.long handler_\vec
.endr
.irp vec, 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	.long handler_\vec
.endr
.irp vec, 32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
	.long handler_\vec
.endr
.irp vec, 48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63
	.long handler_\vec
.endr
.irp vec, 64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79
	.long handler_\vec
.endr
.irp vec, 80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95
	.long handler_\vec
.endr
.irp vec, 96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111
	.long handler_\vec
.endr
.irp vec, 112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127
	.long handler_\vec
.endr
.irp vec, 128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143
	.long handler_\vec
.endr
.irp vec, 144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159
	.long handler_\vec
.endr
.irp vec, 160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175
	.long handler_\vec
.endr
.irp vec, 176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191
	.long handler_\vec
.endr
.irp vec, 192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207
	.long handler_\vec
.endr
.irp vec, 208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223
	.long handler_\vec
.endr
.irp vec, 224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239
	.long handler_\vec
.endr
.irp vec, 240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
	.long handler_\vec
.endr
