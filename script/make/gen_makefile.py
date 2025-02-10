import sys
import os
from pathlib import Path

makefile_string="include $(SCRIPT_MAKE_DIR)/build.mk\n" \
            +   "-include ./Makefile.env\n" \
            +   "modules= $(shell find ./* -maxdepth 0 -type d)\n\n"    \
            +   "all: init $(modules) ${OBJECTS}\n" \
            +   "\t@for mod in $(modules); do $(MAKE) -C $$mod all; done\n\n" \
            +   "-include ${BUILD}/*.d\n" \
            +   "${BUILD}/%.o: ./%.c $(modules)\n" \
            +   "\t@echo \"CC	\"$@\n" \
            +   "\t@$(CC) $(CFLAGS) -o $@ -c $< -MD -MF ${BUILD}/$*.d -MP\n"

def gen_makefile(target_dir):
    target_dir = Path(target_dir)
    for item in target_dir.iterdir():
        if item.is_dir():
            path_string = f"{item}"
            makefile_file_path = os.path.join(path_string,"Makefile")
            makefile_file=open(makefile_file_path,"w")
            makefile_file.write(makefile_string)
            makefile_file.close()
            # Recursively traverse subdirectories
            gen_makefile(item)
        elif item.is_file():
            path_string = f"{item}"
            dir_path = os.path.dirname(path_string)
            file_name = os.path.basename(path_string)
            if file_name.endswith('.S'):
                real_file_name = file_name.split('.')[0]
                append_string = "\n${BUILD}/"+real_file_name+".o: ./"+real_file_name+".S $(modules)\n\t@echo \"CC	${BUILD}/boot.o\"\n\t@$(CC) $(CFLAGS) -o $@ -c $< -MD -MF $*.d -MP"
                makefile_file_path = os.path.join(dir_path,"Makefile")
                makefile_file=open(makefile_file_path,"a")
                makefile_file.write(append_string)
                makefile_file.close()

if __name__ =='__main__':
    print("GEN\tMakefile")
    arch_dir=sys.argv[1]
    gen_makefile(arch_dir)

    kernel_dir=sys.argv[2]
    gen_makefile(kernel_dir)

    modules_dir=sys.argv[3]
    gen_makefile(modules_dir)