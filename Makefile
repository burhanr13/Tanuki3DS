TARGET_EXEC := ctremu

CC := gcc
CXX := g++

CSTD := -std=gnu17
CXXSTD := -std=gnu++20
CFLAGS := -Wall -Wimplicit-fallthrough -Wno-format -Wno-unused-variable -Wno-unused-result -Werror
CFLAGS_RELEASE := -O3 -flto -DNOINFO
CFLAGS_DEBUG := -g -fsanitize=address -DNOINFO

CPPFLAGS := -MP -MMD -D_GNU_SOURCE

LDFLAGS := -lm -lSDL2 -lreadline -lcapstone

ifeq ($(shell uname),Darwin)
	CFLAGS += -arch x86_64
	CPPFLAGS += -I/opt/homebrew/include
	LDFLAGS := -L/usr/local/lib -L/opt/homebrew/lib $(LDFLAGS)
	LDFLAGS += -framework OpenGL -lGLEW
else
	LDFLAGS += -lGL -lGLEW
endif

BUILD_DIR := build
SRC_DIR := src

DEBUG_DIR := $(BUILD_DIR)/debug
RELEASE_DIR := $(BUILD_DIR)/release

SRCS := $(shell find $(SRC_DIR) -name '*.c') 
SRCSCPP := $(shell find $(SRC_DIR) -name '*.cpp')
SRCS := $(SRCS:$(SRC_DIR)/%=%)
SRCSCPP := $(SRCSCPP:$(SRC_DIR)/%=%)

OBJS_DEBUG := $(SRCS:%.c=$(DEBUG_DIR)/%.o) $(SRCSCPP:%.cpp=$(DEBUG_DIR)/%.o)
DEPS_DEBUG := $(OBJS_DEBUG:.o=.d)

OBJS_RELEASE := $(SRCS:%.c=$(RELEASE_DIR)/%.o)  $(SRCSCPP:%.cpp=$(RELEASE_DIR)/%.o)
DEPS_RELEASE := $(OBJS_RELEASE:.o=.d)

.PHONY: release, debug, clean

release: CFLAGS += $(CFLAGS_RELEASE)
release: $(RELEASE_DIR)/$(TARGET_EXEC)

debug: CFLAGS += $(CFLAGS_DEBUG)
debug: $(DEBUG_DIR)/$(TARGET_EXEC)

$(RELEASE_DIR)/$(TARGET_EXEC): $(OBJS_RELEASE)
	$(CXX) -o $@ $(CFLAGS) $(CPPFLAGS) $^ $(LDFLAGS)
	cp $@ $(TARGET_EXEC)

$(RELEASE_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CSTD) $(CFLAGS) -c $< -o $@

$(RELEASE_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXSTD) $(CFLAGS) -c $< -o $@

$(DEBUG_DIR)/$(TARGET_EXEC): $(OBJS_DEBUG)
	$(CXX) -o $@ $(CFLAGS) $(CPPFLAGS) $^ $(LDFLAGS)
	cp $@ $(TARGET_EXEC)d

$(DEBUG_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CSTD) $(CFLAGS) -c $< -o $@

$(DEBUG_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXSTD) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET_EXEC) $(TARGET_EXEC)d

-include $(DEPS_DEBUG)
-include $(DEPS_RELEASE)
