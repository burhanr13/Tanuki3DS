TARGET_EXEC := ctremu

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

LDFLAGS := -L/usr/local/lib -lm -lSDL3 -lfdk-aac

ifeq ($(OS),Windows_NT)
	LTO := -fuse-ld=lld -flto
else
	LTO := -flto
endif

ifeq ($(USER), 1)
	CFLAGS_RELEASE += $(LTO)
	CPPFLAGS += -DNOPORTABLE -DNOCAPSTONE
else
	CFLAGS_RELEASE += -g
	LDFLAGS += -lcapstone
endif

ifeq ($(shell getconf PAGESIZE),4096)
	CPPFLAGS += -DFASTMEM -DJIT_FASTMEM
endif

ifeq ($(shell uname -m),arm64)
	LDFLAGS += -lxbyak_aarch64
endif
ifeq ($(shell uname -m),aarch64)
	LDFLAGS += -lxbyak_aarch64
endif

ifeq ($(OS),Windows_NT)
	LDFLAGS := -static-libgcc -static-libstdc++ -Wl,-Bstatic -lpthread $(LDFLAGS)
	LDFLAGS += -Wl,-Bdynamic -Wl,--stack,8388608
else ifeq ($(shell uname),Darwin)
	CPPFLAGS += -isystem $(shell brew --prefix)/include
	LDFLAGS := -L$(shell brew --prefix)/lib $(LDFLAGS)
endif

BUILD_DIR := build
SRC_DIR := src

SRCS := $(shell find $(SRC_DIR) -name '*.c') 
SRCSCPP := $(shell find $(SRC_DIR) -name '*.cpp')
SRCS := $(SRCS:$(SRC_DIR)/%=%)
SRCSCPP := $(SRCSCPP:$(SRC_DIR)/%=%)

ifeq ($(DEBUG), 1)
	OUT_DIR := $(BUILD_DIR)/debug
	TARGET_EXEC := $(TARGET_EXEC)d
	CFLAGS += $(CFLAGS_DEBUG)
else
	OUT_DIR := $(BUILD_DIR)/release
	CFLAGS += $(CFLAGS_RELEASE)
endif

OBJS := $(SRCS:%.c=$(OUT_DIR)/%.o)  $(SRCSCPP:%.cpp=$(OUT_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

$(OUT_DIR)/$(TARGET_EXEC): $(OBJS)
	@echo linking $@...
	@$(CXX) -o $@ $(CFLAGS) $(CPPFLAGS) $^ $(LDFLAGS)
	@cp $@ $(TARGET_EXEC)
	@echo done

$(OUT_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo $<
	@$(CC) $(CPPFLAGS) $(CSTD) $(CFLAGS) -c $< -o $@

$(OUT_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo $<
	@$(CXX) $(CPPFLAGS) $(CXXSTD) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	@echo clean...
	@rm -rf $(BUILD_DIR) $(TARGET_EXEC) $(TARGET_EXEC)d

-include $(DEPS)
