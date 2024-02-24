-include ./Makefile.env
ROOT_DIR	= $(shell pwd)
BUILD	?=	$(ROOT_DIR)/build
CONFIG_FILE	:=	$(ROOT_DIR)/Makefile.env
SCRIPT_DIR	:=	$(ROOT_DIR)/script
SCRIPT_CONFIG_DIR	:=	$(SCRIPT_DIR)/config
CONFIG	?=	config_x86_64.json
SCRIPT_MAKE_DIR		:=	$(SCRIPT_DIR)/make
ARCH_DIR	:=	$(ROOT_DIR)/arch
INCLUDE_DIR	:=	$(ROOT_DIR)/include
KERNEL_DIR	:=	$(ROOT_DIR)/kernel
MODULES_DIR	:=	$(ROOT_DIR)/modules

KERNELVERSION=0.1

CC	:=$(CROSS_COMPLIER)gcc
LD	:=$(CROSS_COMPLIER)ld
AR	:=$(CROSS_COMPLIER)ar

OBJCOPY	:=$(CROSS_COMPLIER)objcopy
OBJDUMP	:=$(CROSS_COMPLIER)objdump

ifeq ($(DBG), true)
	CFLAGS	+= -g
	CFLAGS	+= --verbose
endif

export ARCH KERNELVERSION ROOT_DIR BUILD INCLUDE_DIR CC LD AR CFLAGS ARFLAGS LDFLAGS LIBS DBG

include $(SCRIPT_MAKE_DIR)/qemu.mk
.PHONY:all

all: init have_config kernel modules
	@echo "have config file"
	$(MAKE) -C $(ARCH_DIR)
	$(MAKE) -C $(MODULES_DIR)
	$(MAKE) -C $(KERNEL_DIR)
	@echo "LD	" $(KERNEL)
init:clean
	@$(shell if [ ! -d $(BUILD) ];then mkdir $(BUILD); fi)
have_config:
	@if [ ! -f ${CONFIG_FILE} ]; \
		then echo "No config file,please use make config first" \
		& exit 2; \
		fi

config:
	@python3 $(SCRIPT_CONFIG_DIR)/configure.py ${ROOT_DIR} ${SCRIPT_CONFIG_DIR} $(SCRIPT_CONFIG_DIR)/${CONFIG}

mrproper: clean
	@echo "rm all Makefile.env"
	@rm -f $(shell find $(ROOT_DIR) -name Makefile.env) 
	@rm -rf $(BUILD)/*

clean:
	@echo "rm all obj file under build"
	@rm -f $(shell find $(BUILD) -name *.o)