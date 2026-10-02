#include "kernel/user.h"
#include "kernel/errno.h"
#include "kernel/pmm.h"
#include "kernel/printk.h"
#include "kernel/string.h"
#include "kernel/timing.h"
#include "kernel/vmm.h"

#define WATCHDOG_MS 10000   /* an app that makes no call for this long is stopped */
#define USER_CS (0x18 | 3)
#define USER_DS (0x20 | 3)

/* kernel/usermode.asm */
int user_enter(const struct int_frame *f);
void user_return(int code) __attribute__((noreturn));

static struct user_proc *current;
static int ended;                   /* set before user_return: it ended, not yielded */

int user_check(uint32_t ptr, uint32_t len, int write)
{
    /* ptr >= USER_TOP must be rejected first: otherwise USER_TOP - ptr
     * underflows and a pointer near 4 GiB passes the length check. */
    if (ptr < USER_BASE || ptr >= USER_TOP || len > USER_TOP - ptr)
        return -EFAULT;
    if (len == 0)
        return 0;
    uint32_t need = PTE_PRESENT | PTE_USER | (write ? PTE_WRITE : 0);
    for (uint32_t page = ptr & ~(PAGE_SIZE - 1); page < ptr + len; page += PAGE_SIZE)
        if ((vmm_lookup(page) & need) != need)
            return -EFAULT;
    return 0;
}

static int map_fresh_page(uint32_t virt)
{
    uint32_t frame = pmm_alloc();
    if (!frame)
        return -ENOMEM;
    int err = vmm_map_user(virt, frame, PTE_WRITE);
    if (err)
        pmm_free(frame);
    return err;
}

int user_create(struct user_proc *p, const void *image, uint32_t size, uint32_t bss, uint32_t entry)
{
    if (!size || size > USER_IMAGE_MAX || bss > USER_IMAGE_MAX ||
        entry < USER_BASE || entry >= USER_BASE + size)
        return -EINVAL;
    memset(p, 0, sizeof(*p));
    p->pd = vmm_space_create();
    if (!p->pd)
        return -ENOMEM;

    /* Build it in its own space; whatever was current comes back after. */
    uint32_t was = vmm_space_current();
    vmm_space_switch(p->pd);
    int err = 0;
    uint32_t total = size + bss;
    for (uint32_t off = 0; off < total && !err; off += PAGE_SIZE) {
        err = map_fresh_page(USER_BASE + off);
        if (!err && off < size) {
            uint32_t n = size - off < PAGE_SIZE ? size - off : PAGE_SIZE;
            /* The kernel reaches the frame through its identity map. */
            memcpy((void *)(vmm_lookup(USER_BASE + off) & ~0xfffu),
                   (const uint8_t *)image + off, n);
        }
    }
    for (uint32_t i = 1; i <= USER_STACK_PAGES && !err; i++)
        err = map_fresh_page(USER_STACK_TOP - i * PAGE_SIZE);
    vmm_space_switch(was);
    if (err) {
        vmm_space_destroy(p->pd);
        p->pd = 0;
        return err;
    }

    /* Its first resume "returns" to the entry point, every register zero. */
    p->frame.gs = p->frame.fs = p->frame.es = p->frame.ds = USER_DS;
    p->frame.eip = entry;
    p->frame.cs = USER_CS;
    p->frame.eflags = 0x202;        /* IF on (input keeps arriving), IOPL 0 */
    p->frame.user_esp = USER_STACK_TOP;
    p->frame.user_ss = USER_DS;
    p->last_call_ms = uptime_ms();
    return 0;
}

int user_resume(struct user_proc *p)
{
    if (current)
        panic("user_resume: a user program is already running");
    uint32_t was = vmm_space_current();
    vmm_space_switch(p->pd);
    current = p;
    ended = 0;
    p->last_call_ms = uptime_ms();  /* time waiting for input doesn't count */
    int code = user_enter(&p->frame);
    current = 0;
    vmm_space_switch(was);
    if (!ended)
        return 1;
    if (!p->res.killed)
        p->res.exit_code = code;
    return 0;
}

void user_destroy(struct user_proc *p)
{
    if (p->pd)
        vmm_space_destroy(p->pd);
    p->pd = 0;
}

int user_exec(const void *image, uint32_t size, struct user_result *res)
{
    struct user_proc p;
    int err = user_create(&p, image, size, 0, USER_BASE);
    if (err)
        return err;
    while (user_resume(&p))
        ;                           /* these never wait for anything */
    *res = p.res;
    user_destroy(&p);
    return 0;
}

struct user_proc *user_current(void)
{
    return current;
}

int user_map(uint32_t virt, uint32_t bytes)
{
    if (!current || virt < USER_BASE || virt >= USER_TOP || bytes > USER_TOP - virt)
        return -EINVAL;
    for (uint32_t off = 0; off < bytes; off += PAGE_SIZE) {
        if (vmm_lookup(virt + off) & PTE_PRESENT)
            continue;                       /* already there */
        int err = map_fresh_page(virt + off);
        if (err)
            return err;
    }
    return 0;
}

int user_map_shared(uint32_t virt, uint32_t phys, uint32_t bytes)
{
    if (!current || virt < USER_BASE || virt >= USER_TOP || bytes > USER_TOP - virt)
        return -EINVAL;
    for (uint32_t off = 0; off < bytes; off += PAGE_SIZE) {
        if (vmm_lookup(virt + off) & PTE_PRESENT)
            continue;
        int err = vmm_map_user(virt + off, phys + off, PTE_WRITE | PTE_SHARED);
        if (err)
            return err;
    }
    return 0;
}

void user_yield(const struct int_frame *f)
{
    current->frame = *f;
    user_return(0);
}

void user_alive(void)
{
    if (current)
        current->last_call_ms = uptime_ms();
}

void user_watchdog(const struct int_frame *f)
{
    if (!current || uptime_ms() - current->last_call_ms < WATCHDOG_MS)
        return;
    kprintf("user: no response for %u s at eip=0x%08x; stopped\n", WATCHDOG_MS / 1000, f->eip);
    current->res.hung = 1;
    user_kill(f);
}

void user_kill(const struct int_frame *f)
{
    if (!current)
        panic("user_kill: ring 3 fault with no user program running");
    current->res.killed = 1;
    current->res.vector = f->vector;
    ended = 1;
    user_return(0);
}

void user_exit(int code)
{
    ended = 1;
    user_return(code);
}
