/* LiteKern X — a test app that makes bad calls on purpose: kernel pointers,
 * unknown numbers, bad volumes and paths, a present before the window is
 * open. Each must just return an error (logged "badcalls: <what> <result>"). */
#include "sdk/kern86.h"

#define KERNEL_PTR 0x00100000u      /* the kernel's code */

int main(void)
{
    struct k86_dirent d[4];
    struct k86_header h = { .nbuttons = 99 };
    k86_logf("badcalls: present before open %d", k86_present(0, 0, 10, 10));
    k86_logf("badcalls: unknown call %d", k86_call(99, 0, 0, 0, 0));
    k86_logf("badcalls: list into kernel memory %d",
             k86_call(SYS_FS_LIST, 0, (uint32_t)"/", KERNEL_PTR, 4));
    k86_logf("badcalls: path in kernel memory %d", k86_list(0, (const char *)KERNEL_PTR, d, 4));
    k86_logf("badcalls: bad volume %d", k86_list(99, "/", d, 4));
    k86_logf("badcalls: dot-dot path %d", k86_list(0, "/../x", d, 4));
    k86_logf("badcalls: missing folder %d", k86_list(0, "/nope", d, 4));
    k86_logf("badcalls: bad name %d", k86_create(0, "/", "a:b", 0));
    k86_logf("badcalls: window into kernel memory %d", k86_call(SYS_WINDOW_OPEN, KERNEL_PTR, 0, 0, 0));
    k86_logf("badcalls: header with 99 buttons %d", k86_header(&h));
    k86_logf("badcalls: font into kernel memory %d", k86_call(SYS_FONT, KERNEL_PTR, 0, 0, 0));
    k86_log("badcalls: done");
    return 7;
}
