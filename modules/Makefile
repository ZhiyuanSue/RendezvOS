include $(SCRIPT_MAKE_DIR)/build.mk
include ./Makefile.env
all:init $(modules)
	@for mod in $(modules); do $(MAKE) -C $$mod; done