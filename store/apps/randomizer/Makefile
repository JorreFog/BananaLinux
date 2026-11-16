# Randomizer: the static aarch64 binary and a host build for --smoke renders.
# No external libraries: Pixelify Sans is baked into randomizer-font.h and stb_image is
# compiled into the binary.
CXX ?= aarch64-linux-gnu-g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wno-unused-function
LDFLAGS ?= -static -s

all: banana-randomizer

banana-randomizer: banana-randomizer.cpp randomizer-font.h randomizer-logo.h stb_image.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ banana-randomizer.cpp

# A build for the machine you are on, for --smoke renders (it draws, but has no DRM/evdev).
host: banana-randomizer.cpp randomizer-font.h randomizer-logo.h stb_image.h
	$(CXX) -O2 -std=c++17 -Wall -Wno-unused-function -o randomizer-host banana-randomizer.cpp

# With zig: zig c++ -target aarch64-linux-musl -std=c++17 -O2 -static -s -o banana-randomizer banana-randomizer.cpp
# or for the host: zig c++ -std=c++17 -O2 -o randomizer-host banana-randomizer.cpp

clean:
	rm -f banana-randomizer randomizer-host

.PHONY: all host clean
