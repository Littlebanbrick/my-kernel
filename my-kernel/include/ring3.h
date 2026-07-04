// ring3.h — Ring 3 (user-mode) initialisation for 32-bit protected mode

#ifndef RING3_H
#define RING3_H

/* Set up GDT with user segments + TSS, map user code/stack pages,
 * then iret to ring 3.  Never returns on success. */
void setup_ring3(void);

#endif
