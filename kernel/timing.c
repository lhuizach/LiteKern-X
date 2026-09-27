#include "kernel/timing.h"
#include "kernel/io.h"
#include "kernel/printk.h"

#define PIT_HZ          1193182u
#define CALIBRATE_MS    10u     /* counts against the kernel_early budget */

#define MAX_PHASES      16

static uint64_t t0;
static uint64_t tsc_hz;

static struct {
    const char *name;
    uint64_t start, end;
} phases[MAX_PHASES];
static uint32_t phase_count;

/* Time CALIBRATE_MS of PIT channel 2 (gated through port 0x61, speaker
 * off) with the TSC. The N270's TSC runs at a constant rate, so one
 * calibration holds for the whole boot. */
static uint64_t calibrate(void)
{
    const uint16_t count = PIT_HZ * CALIBRATE_MS / 1000;

    uint8_t gate = (inb(0x61) & ~0x02) | 0x01;     /* speaker off, gate on */
    outb(0x61, gate);
    outb(0x43, 0xb0);                               /* ch 2, lo/hi, mode 0 */
    outb(0x42, count & 0xff);
    outb(0x42, count >> 8);
    outb(0x61, gate & ~0x01);                       /* restart the count */
    outb(0x61, gate);

    uint64_t start = rdtsc();
    for (uint32_t spins = 0; !(inb(0x61) & 0x20); spins++)
        if (spins > 50000000)
            panic("TSC calibration: PIT channel 2 never fired");
    uint64_t end = rdtsc();

    return (end - start) * 1000 / CALIBRATE_MS;
}

void timing_init(const struct boot_info *bi)
{
    t0 = bi->tsc[TSC_STAGE1];
    tsc_hz = calibrate();
    if (tsc_hz < 1000000)
        panic("TSC calibration gave an implausible %u Hz", (uint32_t)tsc_hz);
}

uint32_t tsc_mhz(void)
{
    return (uint32_t)(tsc_hz / 1000000);
}

uint32_t uptime_ms(void)
{
    return tsc_to_ms(rdtsc() - t0);
}

uint32_t tsc_to_ms(uint64_t ticks)
{
    return (uint32_t)(ticks * 1000 / tsc_hz);
}

void boot_phase(const char *name, uint64_t start, uint64_t end)
{
    if (phase_count == MAX_PHASES)
        panic("boot_phase: more than %u phases", MAX_PHASES);
    phases[phase_count].name = name;
    phases[phase_count].start = start;
    phases[phase_count].end = end;
    phase_count++;
}

void boot_report(uint64_t ready)
{
    kprintf("[boot] tsc=%u MHz (calibrated against the PIT over %u ms)\n",
            tsc_mhz(), CALIBRATE_MS);
    for (uint32_t i = 0; i < phase_count; i++)
        kprintf("[boot] t=%u phase=%s dt=%u\n", tsc_to_ms(phases[i].end - t0), phases[i].name,
                tsc_to_ms(phases[i].end - phases[i].start));
    kprintf("[boot] ready t=%u\n", tsc_to_ms(ready - t0));
}
