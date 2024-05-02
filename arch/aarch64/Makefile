include $(SCRIPT_MAKE_DIR)/build.mk

modules= $(shell find ./* -maxdepth 0 -type d)
all: init $(modules) ${OBJECTS}
	@for mod in $(modules); do $(MAKE) -C $$mod all; done