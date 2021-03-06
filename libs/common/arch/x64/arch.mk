libcommon_objs += arch/x64/memcpy.o

# GRUB_PREFIX = <empty by default>
QEMU  ?= qemu-system-x86_64
BOCHS ?= bochs

ifeq ($(shell uname),Darwin)
GRUB_PREFIX ?= i386-elf-
endif

CFLAGS += --target=x86_64 -mcmodel=large -fno-omit-frame-pointer
CFLAGS += -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mno-avx -mno-avx2

QEMUFLAGS += -m 512 -cpu IvyBridge,rdtscp -rtc base=utc -serial mon:stdio
QEMUFLAGS += -no-reboot -boot d -device isa-debug-exit
QEMUFLAGS += -netdev user,id=net0,hostfwd=tcp:127.0.0.1:1234-:80
QEMUFLAGS += -device e1000,netdev=net0,mac=52:54:00:12:34:56
QEMUFLAGS += -object filter-dump,id=fiter0,netdev=net0,file=network.pcap
QEMUFLAGS += $(if $(SMP), -smp $(SMP))
QEMUFLAGS += $(if $(GUI),,-nographic)

.PHONY: run
run: $(kernel_image)
	cp $(kernel_image) $(BUILD_DIR)/resea.qemu.elf
	./tools/make-bootable-on-qemu.py $(BUILD_DIR)/resea.qemu.elf
	$(QEMU) $(QEMUFLAGS) -kernel $(BUILD_DIR)/resea.qemu.elf

.PHONY: bochs
bochs: $(BUILD_DIR)/resea.iso
	$(PROGRESS) BOCHS $<
	$(BOCHS) -qf tools/bochsrc

.PHONY: iso
iso: $(BUILD_DIR)/resea.iso

$(BUILD_DIR)/resea.iso: $(kernel_image)
	mkdir -p $(BUILD_DIR)/isofiles/boot/grub
	cp tools/grub.cfg $(BUILD_DIR)/isofiles/boot/grub/grub.cfg
	cp $(kernel_image) $(BUILD_DIR)/isofiles/resea.elf
	$(PROGRESS) GRUB $@
	$(GRUB_PREFIX)grub-mkrescue --xorriso=$(PWD)/tools/xorriso-wrapper \
		-o $@ $(BUILD_DIR)/isofiles
