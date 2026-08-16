.DELETE_ON_ERROR:

CXX ?= g++
KUROGANE_SYSROOT ?=
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?=

TARGET := build/sdk-service-probe
OBJECT := build/sdk-service-probe.o
SOURCE := src/capability_probe.cpp

.PHONY: all clean check-sysroot explain

all: $(TARGET)

check-sysroot:
	@test -n "$(KUROGANE_SYSROOT)" || { \
		echo "KUROGANE_SYSROOT is required (for example: /opt/kurogane/sysroot)" >&2; \
		exit 2; \
	}
	@test -f "$(KUROGANE_SYSROOT)/usr/include/kurogane/abi.h" || { \
		echo "KuroganeOS SDK headers not found under KUROGANE_SYSROOT=$(KUROGANE_SYSROOT)" >&2; \
		exit 2; \
	}

$(TARGET): $(SOURCE) check-sysroot
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -ffreestanding -fno-exceptions -fno-rtti \
		-fno-stack-protector -fno-pic -fno-pie -mcmodel=large -mno-red-zone \
		-I"$(KUROGANE_SYSROOT)/usr/include" -c $(SOURCE) -o $(OBJECT)
	$(CXX) -nostdlib -static -no-pie -Wl,--fatal-warnings \
		-Wl,--build-id=none -Wl,-z,noexecstack -Wl,-z,separate-code \
		-Wl,--gc-sections -T"$(KUROGANE_SYSROOT)/usr/lib/kurogane-user.ld" \
		-o $(TARGET) "$(KUROGANE_SYSROOT)/usr/lib/crt0.o" $(OBJECT) \
		-L"$(KUROGANE_SYSROOT)/usr/lib" -Wl,--start-group \
		-lui -lc -lkurogane -lgcc -Wl,--end-group

explain:
	@echo "$(TARGET) is a static KuroganeOS ELF64 executable; copy it into /apps"

clean:
	rm -rf -- build
