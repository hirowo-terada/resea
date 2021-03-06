#include <arch.h>
#include <printk.h>
#include <task.h>
#include "serial.h"

static void serial_write(char ch) {
    while ((asm_in8(IOPORT_SERIAL + LSR) & TX_READY) == 0) {}
    asm_out8(IOPORT_SERIAL, ch);
}

void arch_printchar(char ch) {
    serial_write(ch);
    if (ch == '\n') {
        serial_write('\r');
    }
}

int kdebug_readchar(void) {
    if ((asm_in8(IOPORT_SERIAL + LSR) & 1) == 0) {
        return -1;
    }

    return asm_in8(IOPORT_SERIAL + RBR);
}

void serial_init(void) {
    int baud = 9600;
    int divisor = 115200 / baud;
    asm_out8(IOPORT_SERIAL + IER, 0x00);  // Disable interrupts.
    asm_out8(IOPORT_SERIAL + DLL, divisor & 0xff);
    asm_out8(IOPORT_SERIAL + DLH, (divisor >> 8) & 0xff);
    asm_out8(IOPORT_SERIAL + LCR, 0x03);  // 8n1.
    asm_out8(IOPORT_SERIAL + FCR, 0x00);  // No FIFO.
    asm_out8(IOPORT_SERIAL + IER, 0x01);  // Enable interrupts.
}

void serial_enable_interrupt(void) {
    arch_enable_irq(SERIAL_IRQ);
}
