#  ---------
#	qemu
#  ---------

SMP	?= 4

Qemulator	:= qemu-system-$(ARCH)
QemuFlags	:= -kernel $(Target) -smp $(SMP)
ifeq ($(ARCH), x86_64)
	QemuFlags	+= -machine q35
	# QemuFlags	+= -nographic	#in qemu , don't use this option
	# QemuFlags	+= -numa
else ifeq ($(ARCH), i386)
	QemuFlags	+= -machine q35
	# QemuFlags	+= -nographic	#in qemu , don't use this option
	# QemuFlags	+= -numa
else ifeq ($(ARCH), aarch64)
    QemuFlags	+= -kernel $(Target)
else ifeq ($(ARCH), riscv64)
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