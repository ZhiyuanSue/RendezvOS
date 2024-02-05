ROOT_DIR	= $(shell pwd)
BUILD	?=	$(ROOT_DIR)/build

# set arch


.PHONY:all

all: have_config


have_config:
	@$(shell if [ ! -]

config:
	$(shell python3 $()
clean:
	rm $(BUILD)/*.o
