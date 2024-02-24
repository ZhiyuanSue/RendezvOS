include ${ROOT_DIR}/Makefile.env
modules= $(shell find ./* -maxdepth 0 -type d)
all: $(modules) ${OBJECTS}
	@for mod in $(modules); do $(MAKE) -C $$mod; done

${BUILD}/%.o: ./%.c $(modules)
	@echo "CC	"$@
	@$(CC) $(CFLAGS) -o $@ -c $<