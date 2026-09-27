/* LiteKern X — ring 3 / syscall / paging self-test. Linked into test builds
 * only (-DLKX_SELFTEST_USER, with tests/kernel/user_programs.asm); see
 * tests/kernel/test-kernel.sh.
 *
 * Runs each program in tests/kernel/user/ and checks how it ended. Every
 * program that misbehaves must be killed while the kernel carries on. If any
 * check fails the kernel panics, so on real hardware the screen turns red. */
#include "kernel/errno.h"
#include "kernel/pmm.h"
#include "kernel/printk.h"
#include "kernel/user.h"

#define PROGRAM(name) extern const uint8_t user_prog_##name[], user_prog_##name##_end[]
PROGRAM(hello);
PROGRAM(badptr);
PROGRAM(nosys);
PROGRAM(uptime);
PROGRAM(exit7);
PROGRAM(readkernel);
PROGRAM(nullptr);
PROGRAM(port_io);
PROGRAM(cli);

#define RUN(name, res) \
    user_exec(user_prog_##name, (uint32_t)(user_prog_##name##_end - user_prog_##name), (res))

static int passed, failed;

static void check(int ok, const char *what)
{
    kprintf("selftest: %s %s\n", ok ? "ok  " : "FAIL", what);
    if (ok)
        passed++;
    else
        failed++;
}

static int exited(const struct user_result *r, int code)
{
    return !r->killed && r->exit_code == code;
}

static int killed_by(const struct user_result *r, uint32_t vector)
{
    return r->killed && r->vector == vector;
}

void selftest_user_run(void)
{
    struct user_result r;
    uint32_t free_before = pmm_free_frames();

    check(RUN(hello, &r) == 0 && exited(&r, 42), "ring 3 program makes a syscall and exits(42)");
    check(RUN(badptr, &r) == 0 && exited(&r, 0),
          "kernel/null/unmapped/guard/wrapping/straddling pointers all get -EFAULT");
    check(RUN(nosys, &r) == 0 && exited(&r, -ENOSYS), "unknown syscall returns -ENOSYS");
    check(RUN(uptime, &r) == 0 && !r.killed && r.exit_code > 0 && r.exit_code < 60000,
          "uptime_ms returns a plausible value");

    check(RUN(readkernel, &r) == 0 && killed_by(&r, 14), "reading kernel memory: killed by #PF");
    check(RUN(nullptr, &r) == 0 && killed_by(&r, 14), "null-pointer write: killed by #PF");
    check(RUN(port_io, &r) == 0 && killed_by(&r, 13), "port I/O from ring 3: killed by #GP");
    check(RUN(cli, &r) == 0 && killed_by(&r, 13), "cli from ring 3: killed by #GP");

    int all_ok = 1;
    for (int i = 0; i < 100; i++)
        all_ok &= RUN(exit7, &r) == 0 && exited(&r, 7);
    check(all_ok, "100 programs in a row all run and exit");
    check(pmm_free_frames() == free_before, "no frames leaked across all runs");

    static const uint8_t one_byte[1] = { 0xf4 };
    check(user_exec(one_byte, 0, &r) == -EINVAL, "an empty image is refused");
    check(user_exec(one_byte, USER_IMAGE_MAX + 1, &r) == -EINVAL, "an oversized image is refused");

    kprintf("selftest: user %d/%d passed\n", passed, passed + failed);
    if (failed)
        panic("user self-test: %d checks failed", failed);
}
