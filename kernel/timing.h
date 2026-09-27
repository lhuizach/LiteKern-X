/* LiteKern X — boot timing (docs/BOOT-BUDGET.md).
 * All times are relative to T0 (stage 1 entry) and printed as
 *   [boot] t=<ms since T0> phase=<name> dt=<ms for this phase> */
#ifndef LKX_TIMING_H
#define LKX_TIMING_H

#include <stdint.h>
#include "boot/bootinfo.h"

/* Calibrate the TSC against the PIT and remember T0. */
void timing_init(const struct boot_info *bi);

uint32_t tsc_mhz(void);
uint32_t tsc_to_ms(uint64_t ticks);

/* Log one phase that ran from TSC value `start` to `end`. */
void boot_phase(const char *name, uint64_t start, uint64_t end);

#endif
