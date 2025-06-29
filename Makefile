TARGET_EXEC := ctremu

CC := clang
CXX := clang++

-include config.mk

BUILD_DIR := build
SRC_DIR := src

CSTD := -std=gnu23
CXXSTD := -std=gnu++23
CFLAGS := -Wall -Wimplicit-fallthrough -Wno-format -Werror
CFLAGS_RELEASE := -O3
CFLAGS_DEBUG := -g -fsanitize=address

CPPFLAGS := -MP -MMD -D_GNU_SOURCE -isystem /usr/local/include -Isrc --embed-dir=sys_files

LIBDIRS := /usr/local/lib

LIBS := -lSDL3
STATIC_LIBS := -lfdk-aac

ifeq ($(shell uname),Darwin)
	CPPFLAGS += -isystem $(shell brew --prefix)/include
	LIBDIRS := $(shell brew --prefix)/lib $(LIBDIRS)
else ifeq ($(OS),Windows_NT)
	LIBDIRS += /mingw64/lib
	# we need all this garbage to static link on windows
	LIBS += -lmsvcrt -limm32 -lole32 -loleaut32 -lsetupapi -lversion -lwinmm -luuid
endif

ifeq ($(USER), 1)
	CFLAGS_RELEASE += -flto
	CPPFLAGS += -DREDIRECTSTDOUT -DNOCAPSTONE -DRAS_NO_CHECKS
	CPPFLAGS += -DEMUVERSION=\"$(shell git describe --tags)\"
else
	CFLAGS_RELEASE += -g
	CPPFLAGS += -DEMUVERSION=\"dev\"
	LIBS += -lcapstone
endif

ifeq ($(GPROF), 1)
	CFLAGS += -g -pg
endif

ifeq ($(shell getconf PAGESIZE),4096)
	CPPFLAGS += -DFASTMEM -DJIT_FASTMEM
endif

LDFLAGS := $(LIBDIRS:%=-L%) $(LIBS)
vpath %.a $(LIBDIRS)
.LIBPATTERNS := lib%.a

ifeq ($(OS),Windows_NT)
	LDFLAGS += -mwindows -static -Wl,--stack,8388608 -fuse-ld=lld
endif

SRCS := $(shell find $(SRC_DIR) -name '*.c' -or -name '*.cpp') 
SRCS := $(SRCS:$(SRC_DIR)/%=%)

BUILD_ROOT := $(BUILD_DIR)
ifeq ($(DEBUG), 1)
	BUILD_DIR := $(BUILD_DIR)/debug
	TARGET_EXEC := $(TARGET_EXEC)d
	CFLAGS += $(CFLAGS_DEBUG)
else
	BUILD_DIR := $(BUILD_DIR)/release
	CFLAGS += $(CFLAGS_RELEASE)
endif

OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

$(BUILD_ROOT)/$(TARGET_EXEC): $(OBJS) $(STATIC_LIBS)
	@echo linking $@...
	@$(CXX) -o $@ $(CFLAGS) $(CPPFLAGS) $^ $(LDFLAGS)
	@echo done

$(BUILD_DIR)/%.c.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo $<
	@$(CC) $(CPPFLAGS) $(CSTD) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.cpp.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo $<
	@$(CXX) $(CPPFLAGS) $(CXXSTD) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	@echo clean...
	@rm -rf $(BUILD_ROOT)

-include $(DEPS)
