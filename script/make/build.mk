SRC   	:=$(wildcard ./*.c)
OBJECTS	:=$(patsubst %.c,$(BUILD)/%.o,$(notdir $(SRC)))
SRC     +=$(wildcard ./*.s)
OBJECTS	+=$(patsubst %.s,$(BUILD)/%.o,$(notdir $(SRC)))

CUR_DIR	:=$(notdir $(shell pwd))
BUILD	:=$(BUILD)/$(CUR_DIR)

export BUILD

init:
	@$(shell if [ ! -d $(BUILD) ];then mkdir $(BUILD); fi)