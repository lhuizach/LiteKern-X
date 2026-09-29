/* LiteKern X — reporting on the video BIOS (VBE).
 *
 * The EeePC's GMA 950 video BIOS has no 1024x600 mode for its own panel, so
 * stage 2 falls back to 800x600. The fix (Phase 1, open item) is to patch the
 * BIOS's mode table in shadow RAM before the mode is set, the way the Linux
 * tool 915resolution does. These reports gather what that patch needs. */
#ifndef LKX_VBE_H
#define LKX_VBE_H

#include "boot/bootinfo.h"

/* One line: VBE version, BIOS name, video memory, how many modes, and which
 * 32 bpp linear-framebuffer modes exist. Always logged. */
void vbe_report(const struct boot_info *bi);

/* Diagnostic builds only (-DLKX_DIAG_VBIOS, `make usb`'s diag image): the
 * full mode list, the chipset's shadow-RAM (PAM) settings, and a read-only
 * dump of the Intel mode table in the video BIOS copy at 0xC0000. */
void vbios_diag(const struct boot_info *bi);

#endif
