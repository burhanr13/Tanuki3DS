TARGET_EXEC := ctremu

CC := clang
CXX := clang++

CSTD := -std=gnu23
CXXSTD := -std=gnu++23
CFLAGS := -Wall -Wimplicit-fallthrough -Wno-format -Wno-unused-variable -Wno-unused-result -Werror
CFLAGS_RELEASE := -O3 -DJIT_FASTMEM
CFLAGS_DEBUG := -g -fsanitize=address

CPPFLAGS := -MP -MMD -D_GNU_SOURCE

LDFLAGS := -lm -lSDL2

ifeq ($(USER), 1)
	CFLAGS_RELEASE += -flto
	CPPFLAGS += -DUSE_TFD -DNOPORTABLE
else
	LDFLAGS += -lcapstone
endif

ifeq ($(shell uname),Darwin)
	CC := /usr/local/opt/llvm/bin/clang
	CXX := /usr/local/opt/llvm/bin/clang++
	CFLAGS += -target x86_64-apple-darwin
	CPPFLAGS += -I/opt/homebrew/include
	LDFLAGS := -L/usr/local/lib -L/opt/homebrew/lib $(LDFLAGS)
	LDFLAGS += -framework OpenGL -lGLEW
else
	LDFLAGS += -lGL -lGLEW
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
