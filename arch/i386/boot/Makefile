include $(SCRIPT_MAKE_DIR)/build.mk
modules= $(shell find ./* -maxdepth 0 -type d)

all: init $(modules) ${OBJECTS}
	@for mod in $(modules); do $(MAKE) -C $$mod all; done

-include *.d
${BUILD}/boot.o: ./boot.s $(modules)
	@echo "CC	${BUILD}/boot.o"
	@$(CC) $(CFLAGS) -o $@ -c $< -MD -MF $*.d -MP

${BUILD}/%.o: ./%.c $(modules)
	@echo "CC	"$@
	@$(CC) $(CFLAGS) -o $@ -c $< -MD -MF $*.d -MP