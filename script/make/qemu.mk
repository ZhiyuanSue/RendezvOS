#  ---------
#	qemu
#  ---------

SMP	?= 4

Qemulator	:= qemu-system-$(SRCARCH)
QemuFlags	:= -kernel $(Target) -smp $(SMP)
ifeq ($(SRCARCH), x86_64)
	QemuFlags	+= -machine q35
	# QemuFlags	+= -nographic	#in qemu , don't use this option
	# QemuFlags	+= -numa
else ifeq ($(SRCARCH), i386)
	QemuFlags	+= -machine q35
	# QemuFlags	+= -nographic	#in qemu , don't use this option
	# QemuFlags	+= -numa
else ifeq ($(SRCARCH), aarch64)
    QemuFlags	+= -kernel $(Target)
else ifeq ($(SRCARCH), riscv64)
    QemuFlags	+= -kernel $(Target)
	QemuFlags	+= -nographic -machine virt -bios default
else
	echo "ERROR:The ARCH must be one of the 'x86_64' 'i386' 'aarch64' 'arm64' 'riscv64'"
endif

ifeq ($(DBG), true)
	QemuFlags	+= -s -S
endif

qemu: all
	@echo "starting Qemu semulation..."
	$(Qemulator) $(QemuFlags)