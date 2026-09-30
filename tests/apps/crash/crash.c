/* LiteKern X — a test app that crashes on purpose (tests/kernel/test-kernel.sh):
 * it opens its window, then writes to address 0. The kernel must stop it and
 * carry on. */
#include "sdk/kern86.h"

int main(void)
{
    struct k86_window w;
    k86_window_open(&w);
    k86_log("crash: about to write to address 0");
    *(volatile int *)0 = 1;
    k86_log("crash: still running (should never happen)");
    return 0;
}
