/* kernel.c — Minimal 64-bit C kernel
 *
 * Called from stage3.S after entering long mode.
 * Writes "64" to the VGA text-mode framebuffer at 0xB8000.
 */

void kernel_main(void)
{
    /* VGA text-mode framebuffer: 80 columns × 25 rows
     * Each cell = 2 bytes (ASCII char + attribute byte)
     * Attribute 0x07 = light-grey on black background */
    volatile unsigned short * const vga = (unsigned short *)0xB8000;
    unsigned int i;

    /* Clear the screen: fill every cell with a space */
    for (i = 0; i < 80 * 25; i++)
        vga[i] = 0x0720;     /* 0x07 = attribute, 0x20 = space */

    /* Write "64" at the top-left corner */
    vga[0] = 0x0736;         /* '6' = 0x36 */
    vga[1] = 0x0734;         /* '4' = 0x34 */

    /* Loop forever */
    while (1)
        ;
}
