# KuroganeOS kernel build
#
# The repository-local Windows cross-toolchain is used by default.  The
# PowerShell frontend (`make powershell`) is the canonical Windows build, while
# the rules below keep a regular GNU Make dependency graph available.

POWERSHELL    ?= powershell.exe
CONFIG        ?= debug
TOOLCHAIN_DIR ?= tools/compiler/x86_64-elf/bin
TARGET_PREFIX ?= $(TOOLCHAIN_DIR)/x86_64-elf-
EXEEXT         ?= .exe

CC      := $(TARGET_PREFIX)gcc$(EXEEXT)
CXX     := $(TARGET_PREFIX)g++$(EXEEXT)
LD      := $(TARGET_PREFIX)ld$(EXEEXT)
READELF := $(TARGET_PREFIX)readelf$(EXEEXT)

BUILD_DIR := build
OBJ_DIR   := $(BUILD_DIR)/obj
KERNEL    := $(BUILD_DIR)/kernel.elf
MAP       := $(BUILD_DIR)/kernel.map

# Recursively discover every C++ translation unit below kernel/.
rwildcard = $(foreach item,$(wildcard $1*),$(call rwildcard,$(item)/,$2)) $(wildcard $1$2)
CPP_SOURCES := $(sort $(call rwildcard,kernel/,*.cpp))
CPP_OBJECTS := $(patsubst kernel/%.cpp,$(OBJ_DIR)/%.o,$(CPP_SOURCES))

ENTRY_SOURCE := kernel/arch/x86_64/entry.asm
ENTRY_OBJECT := $(OBJ_DIR)/arch/x86_64/entry.o
ASM_SOURCES  := $(sort $(call rwildcard,kernel/,*.asm))
ASM_OBJECTS  := $(patsubst kernel/%.asm,$(OBJ_DIR)/%.o,$(ASM_SOURCES))
OBJECTS      := $(ENTRY_OBJECT) $(filter-out $(ENTRY_OBJECT),$(ASM_OBJECTS)) $(CPP_OBJECTS)
DEPS        := $(OBJECTS:.o=.d)

CPPFLAGS := -Ikernel -Ikernel/include -Ikernel/memory -Ikernel/fs -Isdk/include
WARNFLAGS := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wundef \
	-Werror=return-type
ifeq ($(CONFIG),release)
OPTFLAGS := -O2 -g1 -DNDEBUG -DKUROGANE_DEBUG=0
else
OPTFLAGS := -O0 -g3 -DKUROGANE_DEBUG=1
endif
FREESTANDING_FLAGS := \
	-ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics \
	-fno-use-cxa-atexit -fno-stack-protector -fPIE -fno-plt \
	-fno-omit-frame-pointer \
	-fno-unwind-tables -fno-asynchronous-unwind-tables \
	-Wa,--noexecstack -m64 -mno-red-zone -mno-mmx -mno-sse -msoft-float
KERNEL_CXXFLAGS := -std=c++17 $(OPTFLAGS) $(WARNFLAGS) $(FREESTANDING_FLAGS) -fvisibility=hidden
KERNEL_ASFLAGS  := -ffreestanding -fno-stack-protector -fPIE -fno-plt \
	-Wa,--noexecstack -m64 -mno-red-zone -mno-mmx -mno-sse -msoft-float
KERNEL_LDFLAGS  := --fatal-warnings --build-id=none -pie --no-dynamic-linker \
	-z noexecstack -z text -z max-page-size=0x1000 -T linker.ld

.DEFAULT_GOAL := all

.PHONY: all stage powershell rebuild clean verify print-sources

all: stage

$(KERNEL): $(OBJECTS) linker.ld
	@$(POWERSHELL) -NoProfile -Command "[System.IO.Directory]::CreateDirectory('$(abspath $(BUILD_DIR))') | Out-Null"
	$(LD) $(KERNEL_LDFLAGS) -Map=$(MAP) -o $@ $(OBJECTS)

$(OBJ_DIR)/%.o: kernel/%.cpp
	@$(POWERSHELL) -NoProfile -Command "[System.IO.Directory]::CreateDirectory('$(abspath $(dir $@))') | Out-Null"
	$(CXX) $(CPPFLAGS) $(KERNEL_CXXFLAGS) \
		-MMD -MP -MF $(@:.o=.d) -MT $@ -frandom-seed=$< \
		-c $< -o $@

$(OBJ_DIR)/%.o: kernel/%.asm
	@$(POWERSHELL) -NoProfile -Command "[System.IO.Directory]::CreateDirectory('$(abspath $(dir $@))') | Out-Null"
	$(CC) $(CPPFLAGS) $(KERNEL_ASFLAGS) \
		-MMD -MP -MF $(@:.o=.d) -MT $@ \
		-c -x assembler-with-cpp $< -o $@

# Build the standalone EFI application and copy it with the freshly linked
# kernel into the image staging tree.
stage: $(KERNEL)
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration $(CONFIG) -StageOnly

# Canonical Windows entry points.
powershell:
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration $(CONFIG)

rebuild:
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration $(CONFIG) -Rebuild

clean:
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Clean

verify: $(KERNEL)
	$(READELF) -hW $(KERNEL)
	$(READELF) -lW $(KERNEL)

print-sources:
	@$(foreach source,$(CPP_SOURCES),echo $(source);)

-include $(DEPS)
