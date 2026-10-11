# arcade-shelf
#   make                 macOS: build/libarcadeshelf.a and build/viewer
#   make vita            PS Vita (VitaSDK): build-vita/libarcadeshelf.a
#                        (VITAGL_DIR=... to use the application's vitaGL headers)
#   make ADB=1 ...       also the artwork download from Arcade Database
#                        (libcurl; see src/adb_scraper.h before shipping it)
#   make vita IMGUI=system
#                        use VitaSDK's Dear ImGui 1.61 (libimgui), for an
#                        application that links it already
# The library builds its own Dear ImGui 1.61 (deps/imgui): an application
# that uses it must not link another ImGui, unless IMGUI=system.

ADB ?= 0
IMGUI ?= bundled
SOURCES_CXX := src/shelf.cpp src/menu.cpp src/gl.cpp src/cabinet3d.cpp src/disc3d.cpp
ifeq ($(IMGUI),bundled)
SOURCES_CXX += deps/imgui/imgui.cpp deps/imgui/imgui_draw.cpp
IMGUI_FLAGS := -Ideps/imgui
else ifneq ($(IMGUI),system)
$(error IMGUI must be bundled or system)
endif
SOURCES_C :=
DEFINES :=
ifeq ($(ADB),1)
SOURCES_CXX += src/adb_scraper.cpp
SOURCES_C += deps/rjson/rjson.c deps/rjson/rstrtod.c
DEFINES += -DARCADE_SHELF_ADB -Ideps/rjson/include
endif

ifeq ($(MAKECMDGOALS),vita)
PREFIX := arm-vita-eabi-
CC := $(PREFIX)gcc
CXX := $(PREFIX)g++
AR := $(PREFIX)ar
BUILD := build-vita$(if $(filter 1,$(ADB)),-adb,)$(if $(filter system,$(IMGUI)),-sysimgui,)
ARCH_FLAGS := -mcpu=cortex-a9 -mtune=cortex-a9 -mfpu=neon -marm -fsingle-precision-constant
# VITAGL_DIR: the application's vitaGL checkout, if not VitaSDK's.
PLATFORM_FLAGS := $(if $(VITAGL_DIR),-I$(VITAGL_DIR)/source,) -I$(VITASDK)/arm-vita-eabi/include
else
CC := xcrun clang
CXX := xcrun clang++
AR := ar
BUILD := build$(if $(filter 1,$(ADB)),-adb,)
ARCH_FLAGS := -arch arm64
SDL3_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL3_LIBS := $(shell pkg-config --libs sdl3)
PLATFORM_FLAGS := $(shell pkg-config --cflags libpng) $(SDL3_CFLAGS)
endif

CFLAGS := $(ARCH_FLAGS) -O2 -g -Wall -Iinclude -Isrc $(IMGUI_FLAGS) -I$(BUILD) $(PLATFORM_FLAGS) $(DEFINES)
CXXFLAGS := $(CFLAGS) -std=gnu++17
LIB := $(BUILD)/libarcadeshelf.a
OBJECTS := $(addprefix $(BUILD)/,$(SOURCES_CXX:.cpp=.o) $(SOURCES_C:.c=.o))
FONT := $(BUILD)/roboto_medium.inc

.PHONY: all vita clean
all: $(LIB) $(BUILD)/viewer
vita: $(LIB)

$(LIB): $(OBJECTS)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $^

$(BUILD)/viewer: examples/viewer/viewer.cpp $(LIB)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIB) $(SDL3_LIBS) $(shell pkg-config --libs libpng) \
		$(if $(filter 1,$(ADB)),-lcurl,) -lz -framework OpenGL

# Roboto Medium (Apache License 2.0) from deps/imgui/misc/fonts, as a C array.
$(FONT): deps/imgui/misc/fonts/Roboto-Medium.ttf
	@mkdir -p $(BUILD)
	cd deps/imgui/misc/fonts && xxd -i Roboto-Medium.ttf > $(CURDIR)/$@

$(BUILD)/src/shelf.o: $(FONT)

# Dear ImGui 1.61 predates clang's -Wnontrivial-memcall.
$(BUILD)/deps/imgui/%.o: CXXFLAGS += $(if $(filter vita,$(MAKECMDGOALS)),,-Wno-nontrivial-memcall)

$(BUILD)/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=gnu99 -MMD -MP -c $< -o $@

clean:
	rm -rf build build-adb build-vita*

-include $(OBJECTS:.o=.d)
