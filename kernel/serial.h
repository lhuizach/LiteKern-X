/* LiteKern X — COM1 debug output. On the EeePC there is no UART: writes are
 * ignored and the transmit wait is bounded, so this is always safe to call. */
#ifndef LKX_SERIAL_H
#define LKX_SERIAL_H

void serial_init(void);
void serial_putc(char c);

#endif
