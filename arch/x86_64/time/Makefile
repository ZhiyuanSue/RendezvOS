include $(SCRIPT_MAKE_DIR)/build.mk
modules= $(shell find ./* -maxdepth 0 -type d)

all: init $(modules) ${OBJECTS}
	@for mod in $(modules); do $(MAKE) -C $$mod all; done
	@-rm -f *.d

${BUILD}/time_asm.o: ./time_asm.S $(modules)
	@echo "CC	${BUILD}/time_asm.o"
	@$(CC) $(CFLAGS) -o $@ -c $<

-include *.d
${BUILD}/%.o: ./%.c $(modules)
	@echo "CC	"$@
	@$(CC) $(CFLAGS) -o $@ -c $< -MD -MF $*.d -MP