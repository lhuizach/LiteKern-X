/* LiteKern X — a test app that never returns to the kernel: it opens its
 * window and spins. The watchdog must stop it (10 s without a call). */
#include "sdk/kern86.h"

int main(void)
{
    struct k86_window w;
    k86_window_open(&w);
    k86_log("hang: spinning forever");
    for (volatile int i = 0;; i++)
        ;
}
