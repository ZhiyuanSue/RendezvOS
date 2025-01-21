include $(SCRIPT_MAKE_DIR)/build.mk
-include ./Makefile.env
modules= $(shell find ./* -maxdepth 0 -type d)

all: init $(modules) ${OBJECTS}
	@for mod in $(modules); do $(MAKE) -C $$mod all; done
	@rm -f $(shell find . -mindepth 2 -type f -name 'Makefile' )

-include ${BUILD}/*.d
${BUILD}/%.o: ./%.c $(modules)
	@echo "CC	"$@
	@$(CC) $(CFLAGS) -o $@ -c $< -MD -MF ${BUILD}/$*.d -MP