include $(SCRIPT_MAKE_DIR)/build.mk
module	:= x86_64

ifeq ($(ARCH), x86_64)
	module = x86_64
else ifeq ($(ARCH), i386)
	module = x86_64
else ifeq ($(ARCH), aarch32)
	module = aarch32
else ifeq ($(ARCH), aarch64)
	module = aarch64
else ifeq ($(ARCH), riscv32)
	module = riscv32
else ifeq ($(ARCH), riscv64)
	module = riscv64
else ifeq ($(ARCH), loongarch)
	module = loongarch
endif

all:init
	@$(MAKE) -C $(module) all
