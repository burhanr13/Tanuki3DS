TARGET_EXEC := ctremu

BUILD_DIR := build
SRC_DIR := src

ifeq ($(OS),Windows_NT)
	CC := clang
	CXX := clang++
else ifeq ($(shell uname),Darwin)
	CC := $(shell brew --prefix)/opt/llvm/bin/clang
	CXX := $(shell brew --prefix)/opt/llvm/bin/clang++
else ifeq ($(shell uname),Linux)
	CC := clang-19
	CXX := clang++-19
else
	CC := clang
	CXX := clang++
endif

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
	LIBDIRS += /mingw32/lib /mingw64/lib
	LIBS += -lntdll -lkernel32 -lmsvcrt -ladvapi32 -lbcrypt -lrpcrt4 -lgdi32 -lucrtbase -luser32 -limm32 -lole32 -loleaut32 -lsetupapi -lshell32 -lversion -lwinmm -lcfgmgr32 -lcryptbase -lbcryptprimitives -luuid
endif

ifeq ($(USER), 1)
	CFLAGS_RELEASE += -flto
	CPPFLAGS += -DNOPORTABLE -DNOCAPSTONE
else
	CFLAGS_RELEASE += -g
	LIBS += -lcapstone
endif

ifeq ($(shell getconf PAGESIZE),4096)
	CPPFLAGS += -DFASTMEM -DJIT_FASTMEM
endif

ifeq ($(shell uname -m),arm64)
	STATIC_LIBS += -lxbyak_aarch64
endif
ifeq ($(shell uname -m),aarch64)
	STATIC_LIBS += -lxbyak_aarch64
endif

LDFLAGS := $(LIBDIRS:%=-L%) $(LIBS)
vpath %.a $(LIBDIRS)
.LIBPATTERNS := lib%.a

ifeq ($(OS),Windows_NT)
	LDFLAGS += -static -Wl,--stack,8388608 -fuse-ld=lld
endif

SRCS := $(shell find $(SRC_DIR) -name '*.c') 
SRCSCPP := $(shell find $(SRC_DIR) -name '*.cpp')
SRCS := $(SRCS:$(SRC_DIR)/%=%)
SRCSCPP := $(SRCSCPP:$(SRC_DIR)/%=%)

ifeq ($(DEBUG), 1)
	BUILD_DIR := $(BUILD_DIR)/debug
	TARGET_EXEC := $(TARGET_EXEC)d
	CFLAGS += $(CFLAGS_DEBUG)
else
	BUILD_DIR := $(BUILD_DIR)/release
	CFLAGS += $(CFLAGS_RELEASE)
endif

OBJS := $(SRCS:%.c=$(BUILD_DIR)/%.o)  $(SRCSCPP:%.cpp=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS) $(STATIC_LIBS)
	@echo linking $@...
	@$(CXX) -o $@ $(CFLAGS) $(CPPFLAGS) $^ $(LDFLAGS)
	@cp $@ $(TARGET_EXEC)
	@echo done

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo $<
	@$(CC) $(CPPFLAGS) $(CSTD) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo $<
	@$(CXX) $(CPPFLAGS) $(CXXSTD) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	@echo clean...
	@rm -rf $(BUILD_DIR) $(TARGET_EXEC) $(TARGET_EXEC)d

-include $(DEPS)
