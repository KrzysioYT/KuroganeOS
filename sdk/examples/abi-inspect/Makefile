KUROGANE_SYSROOT ?= ../../sysroot
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic

.PHONY: all clean

all: build/abi-inspect.o

build/abi-inspect.o: main.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -ffreestanding \
		-I$(KUROGANE_SYSROOT)/usr/include -c $< -o $@

clean:
	rm -rf build
