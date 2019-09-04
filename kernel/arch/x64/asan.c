#include <arch.h>
#include <debug.h>

bool arch_asan_is_kernel_address(vaddr_t addr) {
    return addr < 0xffff8000fec00000; // APIC registers.
}

extern char __kernel_image_start[];
extern char __kernel_image_end[];
void arch_asan_init(void) {
    size_t image_len = (size_t) __kernel_image_end
                       - (size_t) __kernel_image_start;

    // Kernel image.
    asan_init_area(ASAN_VALID, (void *) __kernel_image_start, image_len);
    // Text-mode VGA.
    asan_init_area(ASAN_VALID, from_paddr(0xb8000), 0x1000);
    // MP table.
    asan_init_area(ASAN_VALID, from_paddr(0xf0000), 0x10000);
    // Kernel boot stacks.
    asan_init_area(ASAN_VALID, (void *) KERNEL_BOOT_STACKS_ADDR,
                  KERNEL_BOOT_STACKS_LEN);
    // CPUVARs.
    asan_init_area(ASAN_VALID, (void *) CPU_VAR_ADDR, CPU_VAR_LEN);
}