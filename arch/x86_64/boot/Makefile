include $(SCRIPT_MAKE_DIR)/build.mk
modules= $(shell find ./* -maxdepth 0 -type d)

all: init $(modules) ${OBJECTS}
	@for mod in $(modules); do $(MAKE) -C $$mod all; done
	@-rm -f *.d

-include *.d
${BUILD}/boot.o: ./boot.S $(modules)
	@echo "CC	${BUILD}/boot.o"
	@$(CC) $(CFLAGS) -o $@ -c $<

${BUILD}/%.o: ./%.c $(modules)
	@echo "CC	"$@
	@$(CC) $(CFLAGS) -o $@ -c $< -MD -MF $*.d -MP