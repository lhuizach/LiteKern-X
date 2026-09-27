/* LiteKern X — boot timing (docs/BOOT-BUDGET.md).
 *
 * Phases are recorded while booting and printed together by boot_report(),
 * so no measurement includes the (slow, in the VMs) serial output. Lines:
 *   [boot] t=<ms since T0> phase=<name> dt=<ms for this phase> */
#ifndef LKX_TIMING_H
#define LKX_TIMING_H

#include <stdint.h>
#include "boot/bootinfo.h"

/* Calibrate the TSC against the PIT and remember T0. */
void timing_init(const struct boot_info *bi);

uint32_t tsc_mhz(void);

/* Milliseconds since T0 (stage 1 entry). */
uint32_t uptime_ms(void);
uint32_t tsc_to_ms(uint64_t ticks);

/* Record one phase that ran from TSC value `start` to `end`. */
void boot_phase(const char *name, uint64_t start, uint64_t end);

/* Print the calibration, every recorded phase, and "[boot] ready t=...". */
void boot_report(uint64_t ready);

#endif
