include $(SCRIPT_MAKE_DIR)/build.mk
-include ./Makefile.env
all:init $(modules)
	@for mod in $(modules); do $(MAKE) -C $$mod all; done
	@rm -f $(shell find . -mindepth 2 -type f -name 'Makefile' ! -path './user/*' )