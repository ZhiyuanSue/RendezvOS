include $(SCRIPT_MAKE_DIR)/build.mk
module	:= x86_64

ifeq ($(ARCH), x86_64)
	modules += x86_64
else ifeq ($(ARCH), i386)
	modules += x86_64
else ifeq ($(ARCH), aarch32)
	modules += aarch32
else ifeq ($(ARCH), aarch64)
	modules += aarch64
else ifeq ($(ARCH), riscv32)
	modules += riscv32
else ifeq ($(ARCH), riscv64)
	modules += riscv64
else ifeq ($(ARCH), loongarch)
	modules += loongarch

all:init
	$(MAKE) -C $(module)
