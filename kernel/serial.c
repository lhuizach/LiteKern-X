#include "kernel/serial.h"
#include "kernel/io.h"

#define COM1 0x3f8

void serial_init(void)
{
    outb(COM1 + 1, 0x00);   /* no interrupts */
    outb(COM1 + 3, 0x80);   /* DLAB on */
    outb(COM1 + 0, 0x01);   /* 115200 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8N1, DLAB off */
    outb(COM1 + 2, 0xc7);   /* FIFOs on, cleared */
}

void serial_putc(char c)
{
    if (c == '\n')
        serial_putc('\r');
    for (int spins = 0; spins < 100000 && !(inb(COM1 + 5) & 0x20); spins++)
        ;
    outb(COM1, (uint8_t)c);
}
