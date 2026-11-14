# Activity: the aarch64 binary and the host preview. No external libraries.
CXX ?= aarch64-linux-gnu-g++
CXXFLAGS ?= -O2 -std=c++17
LDFLAGS ?= -static -s

all: banana-activity

banana-activity: banana-activity.cpp activity-font.h activity-logo.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ banana-activity.cpp

# A build for the machine you are on, for --smoke renders (it draws, but has no DRM/evdev).
host: banana-activity.cpp activity-font.h activity-logo.h
	g++ -O2 -std=c++17 -o activity-host banana-activity.cpp

clean:
	rm -f banana-activity activity-host

.PHONY: all host clean
