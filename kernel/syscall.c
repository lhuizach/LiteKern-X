#include "kernel/syscall.h"
#include "kernel/errno.h"
#include "kernel/printk.h"
#include "kernel/timing.h"
#include "kernel/sys_app.h"
#include "kernel/lkx.h"
#include "kernel/user.h"

static int sys_debug_write(uint32_t buf, uint32_t len)
{
    if (len > DEBUG_WRITE_MAX)
        return -EINVAL;
    if (user_check(buf, len, 0))
        return -EFAULT;
    kprintf("user: ");
    kwrite((const char *)buf, len);
    kprintf("\n");
    return (int)len;
}

void syscall_dispatch(struct int_frame *f)
{
    int ret;
    user_alive();                   /* the watchdog: this app is still responding */
    /* Interrupts back on: input keeps arriving while a call works (FAT32,
     * drawing). The trap stack has room for an IRQ on top. */
    __asm__ volatile("sti");
    switch (f->eax) {
    case SYS_EXIT:
        user_exit((int)f->ebx);
    case SYS_DEBUG_WRITE:
        ret = sys_debug_write(f->ebx, f->ecx);
        break;
    case SYS_UPTIME_MS:
        ret = (int)uptime_ms();
        break;
    case SYS_WAIT_EVENT:
        lkx_wait_event(f);          /* sets eax, or yields until there's an event */
        return;
    default:
        ret = sys_app(f->eax, f->ebx, f->ecx, f->edx, f->esi, f->edi);
        break;
    }
    f->eax = (uint32_t)ret;
}
