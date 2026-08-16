# KuroganeOS kernel build
#
# Windows uses the repository-local cross-toolchain and PowerShell frontend.
# macOS uses the Homebrew x86_64-elf cross-toolchain.
# Native x86-64 Linux may use the host GNU freestanding toolchain; callers can
# set TARGET_PREFIX=x86_64-elf- when a dedicated cross-toolchain is installed.

CONFIG    ?= debug
HOST_OS   := $(shell uname -s 2>/dev/null || echo Windows)
HOST_ARCH := $(shell uname -m 2>/dev/null || echo unknown)

ifeq ($(HOST_OS),Darwin)
TARGET_PREFIX ?= x86_64-elf-
EXEEXT        ?=
POWERSHELL    ?= powershell.exe
else ifeq ($(HOST_OS),Linux)
TARGET_PREFIX ?=
EXEEXT        ?=
POWERSHELL    ?= pwsh
else
POWERSHELL    ?= powershell.exe
TOOLCHAIN_DIR ?= tools/compiler/x86_64-elf/bin
TARGET_PREFIX ?= $(TOOLCHAIN_DIR)/x86_64-elf-
EXEEXT        ?= .exe
endif

CC      := $(TARGET_PREFIX)gcc$(EXEEXT)
CXX     := $(TARGET_PREFIX)g++$(EXEEXT)
LD      := $(TARGET_PREFIX)ld$(EXEEXT)
READELF := $(TARGET_PREFIX)readelf$(EXEEXT)

BUILD_DIR := build
OBJ_DIR   := $(BUILD_DIR)/obj
KERNEL    := $(BUILD_DIR)/kernel.elf
MAP       := $(BUILD_DIR)/kernel.map

rwildcard = $(foreach item,$(wildcard $1*),$(call rwildcard,$(item)/,$2)) $(wildcard $1$2)
CPP_SOURCES := $(sort $(call rwildcard,kernel/,*.cpp))
CPP_OBJECTS := $(patsubst kernel/%.cpp,$(OBJ_DIR)/%.o,$(CPP_SOURCES))

ENTRY_SOURCE := kernel/arch/x86_64/entry.asm
ENTRY_OBJECT := $(OBJ_DIR)/arch/x86_64/entry.o
ASM_SOURCES  := $(sort $(call rwildcard,kernel/,*.asm))
ASM_OBJECTS  := $(patsubst kernel/%.asm,$(OBJ_DIR)/%.o,$(ASM_SOURCES))
OBJECTS      := $(ENTRY_OBJECT) $(filter-out $(ENTRY_OBJECT),$(ASM_OBJECTS)) $(CPP_OBJECTS)
DEPS         := $(OBJECTS:.o=.d)

CPPFLAGS := -Ikernel -Ikernel/include -Ikernel/memory -Ikernel/fs -Isdk/include
WARNFLAGS := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wundef \
	-Werror=return-type
ifeq ($(CONFIG),release)
OPTFLAGS := -O2 -g1 -DNDEBUG -DKUROGANE_DEBUG=0 -DKUROGANE_TEST=0
else ifeq ($(CONFIG),test)
OPTFLAGS := -O1 -g3 -DKUROGANE_DEBUG=1 -DKUROGANE_TEST=1
else
OPTFLAGS := -O0 -g3 -DKUROGANE_DEBUG=1 -DKUROGANE_TEST=0
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

ifneq ($(filter $(HOST_OS),Darwin Linux),)
define ensure-dir
	@mkdir -p "$(1)"
endef
else
define ensure-dir
	@$(POWERSHELL) -NoProfile -Command "[System.IO.Directory]::CreateDirectory('$(1)') | Out-Null"
endef
endif

.DEFAULT_GOAL := all
.PHONY: all kernel stage macos linux powershell rebuild clean verify print-sources

ifeq ($(HOST_OS),Darwin)
all: macos
macos:
	./scripts/build-macos.sh --configuration $(CONFIG)
stage: kernel
	./scripts/build-macos.sh --configuration $(CONFIG) --stage-only
rebuild:
	./scripts/build-macos.sh --configuration $(CONFIG) --rebuild
clean:
	./scripts/build-macos.sh --clean
else ifeq ($(HOST_OS),Linux)
all: linux
linux:
	bash ./scripts/build-linux.sh --configuration $(CONFIG)
stage: kernel
	bash ./scripts/build-linux.sh --configuration $(CONFIG) --stage-only
rebuild:
	bash ./scripts/build-linux.sh --configuration $(CONFIG) --rebuild
clean:
	bash ./scripts/build-linux.sh --clean
else
all: stage
stage: $(KERNEL)
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration $(CONFIG) -StageOnly
powershell:
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration $(CONFIG)
rebuild:
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration $(CONFIG) -Rebuild
clean:
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Clean
endif

kernel: $(KERNEL)

$(KERNEL): $(OBJECTS) linker.ld
	$(call ensure-dir,$(abspath $(BUILD_DIR)))
	$(LD) $(KERNEL_LDFLAGS) -Map=$(MAP) -o $@ $(OBJECTS)

$(OBJ_DIR)/%.o: kernel/%.cpp
	$(call ensure-dir,$(abspath $(dir $@)))
	$(CXX) $(CPPFLAGS) $(KERNEL_CXXFLAGS) \
		-MMD -MP -MF $(@:.o=.d) -MT $@ -frandom-seed=$< \
		-c $< -o $@

$(OBJ_DIR)/%.o: kernel/%.asm
	$(call ensure-dir,$(abspath $(dir $@)))
	$(CC) $(CPPFLAGS) $(KERNEL_ASFLAGS) \
		-MMD -MP -MF $(@:.o=.d) -MT $@ \
		-c -x assembler-with-cpp $< -o $@

verify: $(KERNEL)
	$(READELF) -hW $(KERNEL)
	$(READELF) -lW $(KERNEL)

print-sources:
	@$(foreach source,$(CPP_SOURCES),echo $(source);)

-include $(DEPS)
