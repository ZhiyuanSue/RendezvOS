#  ---------
#	qemu
#  ---------

SMP	?= 4

Qemulator	:= qemu-system-$(ARCH)
QemuFlags	:= -kernel $(Target_BIN) -smp $(SMP)
ifeq ($(ARCH), x86_64)
	QemuFlags	+= -machine q35
	QemuFlags	+= -nographic	#in qemu , don't use this option
	# QemuFlags	+= -numa
else ifeq ($(ARCH), aarch64)
	QemuFlags	+= -cpu cortex-a72
	QemuFlags	+= -machine virt
	QemuFlags	+= -nographic
else ifeq ($(ARCH), riscv64)
	QemuFlags	+=
	QemuFlags	+= -nographic -machine virt -bios default
else ifeq ($(ARCH), loongarch)
	QemuFlags	+=
endif

ifeq ($(DBG), true)
	QemuFlags	+= -s -S
endif

qemu: all
	@echo "starting Qemu semulation..."
	$(Qemulator) $(QemuFlags)
