#include "kernel/user.h"
#include "kernel/errno.h"
#include "kernel/pmm.h"
#include "kernel/printk.h"
#include "kernel/string.h"
#include "kernel/vmm.h"

/* kernel/usermode.asm */
int user_run(uint32_t entry, uint32_t user_esp);
void user_return(int code) __attribute__((noreturn));

static int running, killed;
static uint32_t kill_vector;

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
    int err = vmm_map_user(virt, frame, 1);
    if (err)
        pmm_free(frame);
    return err;
}

int user_exec(const void *image, uint32_t size, struct user_result *res)
{
    if (running)
        panic("user_exec: a user program is already running");
    if (!size || size > USER_IMAGE_MAX)
        return -EINVAL;

    int err = 0;
    for (uint32_t off = 0; off < size && !err; off += PAGE_SIZE) {
        err = map_fresh_page(USER_BASE + off);
        if (!err) {
            uint32_t n = size - off < PAGE_SIZE ? size - off : PAGE_SIZE;
            /* The kernel reaches the frame through its identity map. */
            memcpy((void *)(vmm_lookup(USER_BASE + off) & ~0xfffu),
                   (const uint8_t *)image + off, n);
        }
    }
    for (uint32_t i = 1; i <= USER_STACK_PAGES && !err; i++)
        err = map_fresh_page(USER_STACK_TOP - i * PAGE_SIZE);
    if (err) {
        vmm_user_teardown();
        return err;
    }

    running = 1;
    killed = 0;
    int code = user_run(USER_BASE, USER_STACK_TOP);
    running = 0;

    res->killed = killed;
    res->vector = killed ? kill_vector : 0;
    res->exit_code = killed ? 0 : code;
    vmm_user_teardown();
    return 0;
}

void user_kill(const struct int_frame *f)
{
    if (!running)
        panic("user_kill: ring 3 fault with no user program running");
    killed = 1;
    kill_vector = f->vector;
    user_return(0);
}

void user_exit(int code)
{
    user_return(code);
}
