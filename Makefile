-include ./Makefile.env
ROOT_DIR	= $(shell pwd)
BUILD	?=	$(ROOT_DIR)/build
CONFIG_FILE	:=	$(ROOT_DIR)/Makefile.env
SCRIPT_DIR	:=	$(ROOT_DIR)/script
SCRIPT_CONFIG_DIR	:=	$(SCRIPT_DIR)/config
ARCH	?=	null
CONFIG	?=
SMP		?=	4
DUMP	?=
DUMPFILE	?= $(ROOT_DIR)/objdump.log
MEM_SIZE	?= 128M
SCRIPT_MAKE_DIR		:=	$(SCRIPT_DIR)/make
SCRIPT_LINK_DIR		:=	$(SCRIPT_DIR)/link
ARCH_DIR	:=	$(ROOT_DIR)/arch
INCLUDE_DIR	:=	$(ROOT_DIR)/include
KERNEL_DIR	:=	$(ROOT_DIR)/kernel
MODULES_DIR	:=	$(ROOT_DIR)/modules
Target_ELF		:=	$(BUILD)/kernel.elf
Target_BIN	:=	$(BUILD)/kernel.bin

KERNELVERSION=0.1

Linker	:=	$(SCRIPT_CONFIG_DIR)/$(ARCH)_linker.ld
CC	:=$(CROSS_COMPLIER)gcc
LD	:=$(CROSS_COMPLIER)ld
AR	:=$(CROSS_COMPLIER)ar

OBJCOPY	:=$(CROSS_COMPLIER)objcopy
OBJDUMP	:=$(CROSS_COMPLIER)objdump

ifeq ($(DBG), true)
	CFLAGS	+= -g
	CFLAGS	+= --verbose
endif

ifeq ($(DUMP), true)
	CFLAGS	+= -g
endif

ifeq ($(ARCH), aarch64)
	CONFIG = config_aarch64.json
else ifeq ($(ARCH), x86_64)
	CONFIG = config_x86_64.json
else ifeq ($(ARCH), riscv64)
	CONFIG = config_riscv64.json
else ifeq ($(ARCH), loongarch)
	CONFIG = config_loongarch.json
else ifeq ($(ARCH), null)
$(error the arch is not supportted or haven't configured)
endif
CFLAGS	+= -Wall -Os -nostdlib -nostdinc
CFLAGS	+= -fno-stack-protector -std=c11
CFLAGS	+=	-I $(INCLUDE_DIR) -DNR_CPUS=$(SMP)

LDFLAGS	+=	-T $(SCRIPT_LINK_DIR)/$(ARCH)_linker.ld

ARFLAGS	+=	-rcs

MAKEFLAGS += --no-print-directory

export ARCH KERNELVERSION ROOT_DIR BUILD SCRIPT_MAKE_DIR INCLUDE_DIR CC LD AR CFLAGS ARFLAGS LDFLAGS LIBS DBG MEM_SIZE

all:  init have_config $(Target_BIN) clean

include $(SCRIPT_MAKE_DIR)/qemu.mk
.PHONY:all


build_objs:
	@python3 $(SCRIPT_MAKE_DIR)/gen_makefile.py $(ARCH_DIR) $(KERNEL_DIR) $(MODULES_DIR)
	@$(MAKE) -C $(ARCH_DIR) all
	@$(MAKE) -C $(KERNEL_DIR) all
	@$(MAKE) -C $(MODULES_DIR) all
	
$(Target_ELF): build_objs
	@echo "LD	" $(Target_ELF)
	@${LD} ${LDFLAGS} -o $@ $(shell find $(BUILD) -name *.o)

$(Target_BIN):	$(Target_ELF)
	@echo "COPY	" $(Target_BIN)
	@$(OBJCOPY)	-O binary -S $(Target_ELF) $(Target_BIN)

# @${LD} ${LDFLAGS} -o $@ $(wildcard $(BUILD)/*.o) $(LIBS)

init:
	@$(shell if [ ! -d $(BUILD) ];then mkdir $(BUILD); fi)
have_config:
	@if [ ! -f ${CONFIG_FILE} ]; \
		then echo "No config file,please use make config first" \
		& exit 2; \
		fi

run:qemu

config: clean
	@python3 $(SCRIPT_CONFIG_DIR)/configure.py ${ROOT_DIR} ${SCRIPT_CONFIG_DIR} $(SCRIPT_CONFIG_DIR)/${CONFIG}
# user must get the ARCH info and then use cross complier
user: config
	@python3 $(SCRIPT_CONFIG_DIR)/user.py ${ROOT_DIR} $(SCRIPT_CONFIG_DIR)/user.json
fmt:
	@find . -name '*.c' -print0 | xargs -0 clang-format -i -style=file
	@find . -name '*.h' -print0 | xargs -0 clang-format -i -style=file

#if you want to use dump, please use 'make run DUMP=true'(maybe with other flags) first and then 'make dump'
dump:
	@$(OBJDUMP) -d -S $(Target_ELF) > $(DUMPFILE)

mrproper: clean
	@echo "RM	Makefile.env"
	@-rm -f $(shell find $(ROOT_DIR) -name Makefile.env) 
	@-rm -rf $(BUILD)/*
	@-rm -f $(ROOT_DIR)/include/modules/modules.h

clean:	init
	@echo "RM	OBJS"
	@-rm -f $(shell find $(BUILD) -name *.o)
	@-rm -f $(shell find $(BUILD) -name *.d)
	@-rm -f ./*.log
