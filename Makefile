# Host and OS detection
ifeq ($(OS),Windows_NT)
WINDOWS := 1
else
WINDOWS := 0
endif

# Toolchains
# 64-bit (x86_64)
CC       ?= x86_64-w64-mingw32-gcc
CXX      ?= x86_64-w64-mingw32-g++
# 32-bit (i686)
PJ64_CC  ?= i686-w64-mingw32-gcc
PJ64_CXX ?= i686-w64-mingw32-g++

native = $(subst /,\,$1)
ifeq ($(WINDOWS),1)
MKDIR = if not exist $(call native,$1) mkdir $(call native,$1)
RMDIR = if exist $(call native,$1) rmdir /s /q $(call native,$1)
else
MKDIR = mkdir -p $1
RMDIR = rm -rf $1
endif

# Upstream parallel-rdp-standalone configuration
PARALLEL_RDP_IMPLEMENTATION := src/parallel-rdp-standalone
platform := win
include $(PARALLEL_RDP_IMPLEMENTATION)/config.mk

# Add WSI to parallel-rdp sources
PARALLEL_RDP_SOURCES_CXX += $(PARALLEL_RDP_IMPLEMENTATION)/vulkan/wsi.cpp

# Plugin sources
PORT_SOURCES_CXX := \
	src/config/config.cpp \
	src/vulkan/vulkan_wsi_win32.cpp \
	src/vulkan/vulkan_renderer.cpp \
	src/plugin/pj64_gfx.cpp \
	src/util/rdp_log.cpp

ALL_SOURCES_CXX := $(PARALLEL_RDP_SOURCES_CXX) $(PORT_SOURCES_CXX)
ALL_SOURCES_C   := $(PARALLEL_RDP_SOURCES_C)

INCLUDES := \
	-Isrc \
	-Isrc/config \
	-Isrc/plugin \
	-Isrc/vulkan \
	-Isrc/util \
	$(PARALLEL_RDP_INCLUDE_DIRS)

PARALLEL_RDP_LOG ?= 0

DEFINES := \
	-DVK_USE_PLATFORM_WIN32_KHR \
	-DGRANITE_VULKAN_MT \
	-DNOMINMAX

ifeq ($(PARALLEL_RDP_LOG),1)
DEFINES += -DPARALLEL_RDP_LOG=1
endif

COMMON_FLAGS := -O3 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-macro-redefined -MMD -MP $(DEFINES) $(INCLUDES)

CXXFLAGS := -std=c++17 $(COMMON_FLAGS)
CFLAGS   := -std=c11 $(COMMON_FLAGS)

LDFLAGS := -shared -static -static-libgcc -static-libstdc++ -luser32 -lgdi32 -lwinmm

BUILD_DIR   := build
BUILD32_DIR := build32

# Object lists
OBJS64_CXX := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(ALL_SOURCES_CXX))
OBJS64_C   := $(patsubst %.c,$(BUILD_DIR)/%.o,$(ALL_SOURCES_C))
OBJS64     := $(OBJS64_CXX) $(OBJS64_C)

OBJS32_CXX := $(patsubst %.cpp,$(BUILD32_DIR)/%.o,$(ALL_SOURCES_CXX))
OBJS32_C   := $(patsubst %.c,$(BUILD32_DIR)/%.o,$(ALL_SOURCES_C))
OBJS32     := $(OBJS32_CXX) $(OBJS32_C)

DLL64 := $(BUILD_DIR)/parallel-rdp-pj64.dll
DLL32 := $(BUILD32_DIR)/parallel-rdp-pj64.dll

.PHONY: all pj64 pj64-64 clean

all: pj64 pj64-64

pj64: $(DLL32)

pj64-64: $(DLL64)

# 64-bit build rules
$(BUILD_DIR)/%.o: %.cpp
	@$(call MKDIR,$(dir $@))
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	@$(call MKDIR,$(dir $@))
	$(CC) $(CFLAGS) -c $< -o $@

$(DLL64): $(OBJS64)
	@$(call MKDIR,$(dir $@))
	$(CXX) -o $@ $^ $(LDFLAGS)

# 32-bit build rules
$(BUILD32_DIR)/%.o: %.cpp
	@$(call MKDIR,$(dir $@))
	$(PJ64_CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD32_DIR)/%.o: %.c
	@$(call MKDIR,$(dir $@))
	$(PJ64_CC) $(CFLAGS) -c $< -o $@

$(DLL32): $(OBJS32)
	@$(call MKDIR,$(dir $@))
	$(PJ64_CXX) -o $@ $^ $(LDFLAGS)

clean:
	@$(call RMDIR,$(BUILD_DIR))
	@$(call RMDIR,$(BUILD32_DIR))

# Include auto-generated header dependencies
DEPS := $(wildcard $(BUILD_DIR)/*/*/*/*.d $(BUILD_DIR)/*/*/*.d $(BUILD_DIR)/*/*.d $(BUILD32_DIR)/*/*/*/*.d $(BUILD32_DIR)/*/*/*.d $(BUILD32_DIR)/*/*.d)
-include $(DEPS)
