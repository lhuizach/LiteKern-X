#include "kernel/irq.h"
#include "kernel/errno.h"
#include "kernel/io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xa0
#define PIC2_DATA 0xa1
#define PIC_EOI   0x20
#define PIC_READ_ISR 0x0b

static struct {
    irq_handler_t handler;
    void *ctx;
    uint32_t count;
} lines[IRQ_COUNT];

static uint16_t mask = 0xffff;

static void io_wait(void)
{
    outb(0x80, 0);          /* unused port: a short delay for old PICs */
}

static void write_mask(void)
{
    outb(PIC1_DATA, mask & 0xff);
    outb(PIC2_DATA, mask >> 8);
}

void irq_init(void)
{
    outb(PIC1_CMD, 0x11);   /* ICW1: init, expect ICW4 */
    io_wait();
    outb(PIC2_CMD, 0x11);
    io_wait();
    outb(PIC1_DATA, IRQ_BASE_VECTOR);       /* ICW2: vector offsets */
    io_wait();
    outb(PIC2_DATA, IRQ_BASE_VECTOR + 8);
    io_wait();
    outb(PIC1_DATA, 0x04);  /* ICW3: slave on IRQ 2 */
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();
    outb(PIC1_DATA, 0x01);  /* ICW4: 8086 mode */
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();
    mask = 0xffff;
    write_mask();
}

int irq_register(unsigned irq, irq_handler_t handler, void *ctx)
{
    if (irq >= IRQ_COUNT || irq == 2 || !handler)
        return -EINVAL;
    if (lines[irq].handler)
        return -EBUSY;
    lines[irq].handler = handler;
    lines[irq].ctx = ctx;
    mask &= ~(1u << irq);
    if (irq >= 8)
        mask &= ~(1u << 2); /* the cascade to the slave PIC */
    write_mask();
    return 0;
}

void irq_unregister(unsigned irq)
{
    if (irq >= IRQ_COUNT || irq == 2)
        return;
    mask |= 1u << irq;
    write_mask();
    lines[irq].handler = 0;
    lines[irq].ctx = 0;
}

static int in_service(uint16_t cmd_port, unsigned bit)
{
    outb(cmd_port, PIC_READ_ISR);
    return inb(cmd_port) & (1u << bit);
}

void irq_dispatch(unsigned irq)
{
    /* Spurious IRQs (7 on the master, 15 on the slave) are not in service
     * and must not get an EOI from the PIC that raised them. */
    if (irq == 7 && !in_service(PIC1_CMD, 7))
        return;
    if (irq == 15 && !in_service(PIC2_CMD, 7)) {
        outb(PIC1_CMD, PIC_EOI);    /* the master did see the cascade */
        return;
    }

    lines[irq].count++;
    if (lines[irq].handler)
        lines[irq].handler(lines[irq].ctx);

    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

uint32_t irq_count(unsigned irq)
{
    return irq < IRQ_COUNT ? lines[irq].count : 0;
}
