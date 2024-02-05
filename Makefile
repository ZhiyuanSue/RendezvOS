ROOT_DIR	= $(shell pwd)
BUILD	?=	$(ROOT_DIR)/build
CONFIG	:=	$(ROOT_DIR)/config
SCRIPT_DIR	:=	$(ROOT_DIR)/script
SCRIPT_CONFIG_DIR	:=	$(SCRIPT_DIR)/config
SCRIPT_MAKE_DIR		:=	$(SCRIPT_DIR)/make

include $(SCRIPT_MAKE_DIR)/qemu.mk

# set arch


.PHONY:all

all: have_config
	@echo "have config file"

have_config:
	@if [ ! -f ${CONFIG} ]; \
		then echo "No config file,please use make config first" \
		& exit 2; \
		fi

config:
	@python3 $(SCRIPT_CONFIG_DIR)/configure.py ${SCRIPT_CONFIG_DIR} ${1}
clean:
	rm $(BUILD)/*.o
