#include <arch.h>
#include <kdebug.h>
#include <memory.h>
#include <printk.h>
#include <syscall.h>
#include <task.h>
#include "interrupt.h"
#include "mp.h"
#include "serial.h"
#include "task.h"
#include "trap.h"

static uint32_t ioapic_read(uint8_t reg) {
    *((uint32_t *) from_paddr(IOAPIC_ADDR)) = reg;
    return *((uint32_t *) from_paddr(IOAPIC_ADDR + 0x10));
}

static void ioapic_write(uint8_t reg, uint32_t data) {
    *((uint32_t *) from_paddr(IOAPIC_ADDR)) = reg;
    *((uint32_t *) from_paddr(IOAPIC_ADDR + 0x10)) = data;
}

static void ack_irq(void) {
    write_apic(APIC_REG_EOI, 0);
}

static void ioapic_init(void) {
    // symmetric I/O mode
    asm_out8(0x22, 0x70);
    asm_out8(0x23, 0x01);

    // disable all hardware interrupts
    unsigned num = (ioapic_read(IOAPIC_REG_IOAPICVER) >> 16) + 1;
    for (unsigned i = 0; i < num; i++) {
        ioapic_write(IOAPIC_REG_NTH_IOREDTBL_HIGH(i), 0);
        ioapic_write(IOAPIC_REG_NTH_IOREDTBL_LOW(i), 1 << 16 /* masked */);
    }

    ack_irq();
}

void arch_enable_irq(unsigned irq) {
    ASSERT(irq <= 255);
    ioapic_write(IOAPIC_REG_NTH_IOREDTBL_HIGH(irq), 0);
    ioapic_write(IOAPIC_REG_NTH_IOREDTBL_LOW(irq), VECTOR_IRQ_BASE + irq);
}

void arch_disable_irq(unsigned irq) {
    ASSERT(irq <= 255);
    ioapic_write(IOAPIC_REG_NTH_IOREDTBL_HIGH(irq), 0);
    ioapic_write(IOAPIC_REG_NTH_IOREDTBL_LOW(irq), 1 << 16 /* masked */);
}

// Dumps the interrupt frame (for debugging).
static void dump_frame(struct iframe *frame) {
    WARN("RIP = %p CS  = %p  RFL = %p", frame->rip, frame->cs, frame->rflags);
    WARN("SS  = %p RSP = %p  RBP = %p", frame->ss, frame->rsp, frame->rbp);
    WARN("RAX = %p RBX = %p  RCX = %p", frame->rax, frame->rbx, frame->rcx);
    WARN("RDX = %p RSI = %p  RDI = %p", frame->rdx, frame->rsi, frame->rdi);
    WARN("R8  = %p R9  = %p  R10 = %p", frame->r8, frame->r9, frame->r10);
    WARN("R11 = %p R12 = %p  R13 = %p", frame->r11, frame->r12, frame->r13);
    WARN("R14 = %p R15 = %p  ERR = %p", frame->r14, frame->r15, frame->error);
}

void x64_handle_interrupt(uint8_t vec, struct iframe *frame) {
    if (vec == VECTOR_IPI_HALT) {
        // Halt the CPU silently...
        panic_unlock();
        while (true) {
            __asm__ __volatile__("cli; hlt");
        }
    }

    ack_irq();
    bool needs_unlock = true;
    switch (vec) {
        case EXP_PAGE_FAULT: {
            vaddr_t addr = asm_read_cr2();
            pagefault_t fault = frame->error;
            uint64_t ip = frame->rip;

            if (fault & (1 << 3)) {
                PANIC("#PF: RSVD bit violation "
                      "(page table is presumably corrupted!)");
            }

            if (ip == (uint64_t) usercopy1 || ip == (uint64_t) usercopy2) {
                // We don't do lock() here beucase we already have the lock
                // in usercopy functions.
                fault |= PF_USER;
                needs_unlock = false;
            } else if ((fault & PF_USER) == 0) {
                // This will never occur. NEVER!
                panic_lock();
                dump_frame(frame);
                PANIC("page fault in the kernel space (addr=%p)", addr);
            } else {
                lock();
            }

            handle_page_fault(addr, fault);
            break;
        }
        case VECTOR_IPI_RESCHEDULE:
            lock();
            task_switch();
            break;
        default:
            lock();
            if (vec <= 20) {
                WARN("Exception #%d\n", vec);
                dump_frame(frame);
                if (frame->cs == KERNEL_CS) {
                    PANIC("Exception #%d occurred in the kernel space!", vec);
                } else {
                    task_exit(EXP_INVALID_OP);
                }
            } else if (vec >= VECTOR_IRQ_BASE) {
                int irq = vec - VECTOR_IRQ_BASE;
                if (irq == SERIAL_IRQ) {
                    kdebug_handle_interrupt();
                } else {
                    handle_irq(irq);
                }
            } else {
                dump_frame(frame);
                PANIC("Unexpected interrupt #%d", vec);
            }
    }

    if (needs_unlock) {
        unlock();
    }
}

uintmax_t x64_handle_syscall(uintmax_t arg1, uintmax_t arg2, uintmax_t arg3,
                             uintmax_t arg4, uintmax_t arg5, uintmax_t type) {
    lock();
    uint64_t ret = handle_syscall(arg1, arg2, arg3, arg4, arg5, type);
    unlock();
    return ret;
}

void interrupt_init(void) {
    ioapic_init();
}
