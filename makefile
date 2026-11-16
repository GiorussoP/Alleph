CXX = g++
CXXFLAGS = -std=c++20 -O2 -Wall -Wextra
LIBS = $(shell pkg-config --libs sdl3 sdl3-image) -lGL -ldl

TARGET = sdf_physics
SOURCE_DIR = src
INCLUDE_DIR = include

CPP_SOURCES = $(shell find $(SOURCE_DIR) -name '*.cpp')
INCLUDES = -I. -I$(INCLUDE_DIR)

all: $(TARGET)

$(TARGET): $(CPP_SOURCES)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $(CPP_SOURCES) $(LIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
