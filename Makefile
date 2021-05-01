CXX ?= gcc
CCFLAGS = -W -Wall -std=c++11 `sdl2-config --cflags`
INCLUDES = -I/usr/include/libevdev-1.0

BINARY = gptokeyb
LIBRARIES = -levdev `sdl2-config --libs`
SOURCES = "gptokeyb.cpp"

all:
	$(CXX) $(CCFLAGS) $(INCLUDES) $(SOURCES) -o $(BINARY) $(LIBRARIES)

clean:
	rm -f $(BINARY)
