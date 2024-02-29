-include ./Makefile.env
ROOT_DIR	= $(shell pwd)
BUILD	?=	$(ROOT_DIR)/build
CONFIG_FILE	:=	$(ROOT_DIR)/Makefile.env
SCRIPT_DIR	:=	$(ROOT_DIR)/script
SCRIPT_CONFIG_DIR	:=	$(SCRIPT_DIR)/config
CONFIG	?=	config_x86_64.json
SCRIPT_MAKE_DIR		:=	$(SCRIPT_DIR)/make
SCRIPT_LINK_DIR		:=	$(SCRIPT_DIR)/link
ARCH_DIR	:=	$(ROOT_DIR)/arch
INCLUDE_DIR	:=	$(ROOT_DIR)/include
KERNEL_DIR	:=	$(ROOT_DIR)/kernel
MODULES_DIR	:=	$(ROOT_DIR)/modules

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
CFLAGS	+= -Wall -O3 -nostdlib -nostdinc
CFLAGS	+= -fno-stack-protector -fno-builtin -fno-PIC
CFLAGS	+=	-I $(INCLUDE_DIR)

export ARCH KERNELVERSION ROOT_DIR BUILD SCRIPT_MAKE_DIR INCLUDE_DIR CC LD AR CFLAGS ARFLAGS LDFLAGS LIBS DBG

include $(SCRIPT_MAKE_DIR)/qemu.mk
.PHONY:all

all:  init have_config kernel modules
	@$(MAKE) -C $(ARCH_DIR) all
	@$(MAKE) -C $(KERNEL_DIR) all
	@$(MAKE) -C $(MODULES_DIR) all
	@echo "LD	" $(KERNEL)
	@${LD} ${LDFLAGS} -o $@ $(wildcard $(BUILD)/*.o) $(LIBS)
init:
	@$(shell if [ ! -d $(BUILD) ];then mkdir $(BUILD); fi)
have_config:
	@if [ ! -f ${CONFIG_FILE} ]; \
		then echo "No config file,please use make config first" \
		& exit 2; \
		fi

run:qemu

config:
	@python3 $(SCRIPT_CONFIG_DIR)/configure.py ${ROOT_DIR} ${SCRIPT_CONFIG_DIR} $(SCRIPT_CONFIG_DIR)/${CONFIG}

mrproper: clean
	@echo "rm all Makefile.env"
	@rm -f $(shell find $(ROOT_DIR) -name Makefile.env) 
	@rm -rf $(BUILD)/*

clean:
	@echo "rm all obj file under build"
	@rm -f $(shell find $(BUILD) -name *.o)
	@rm -f $(shell find $(ROOT_DIR) -name *.d)