CXX ?= g++
CXXFLAGS ?= -O3 -DNDEBUG -std=c++17 -Wall -Wextra -Wpedantic
LDFLAGS ?=

BUILD := build
TARGET := $(BUILD)/srb_periodic_solver
SOURCES := src/solver_main.cpp src/grid_model.cpp src/architecture.cpp
HEADERS := $(wildcard src/*.hpp)

.PHONY: all clean

all: $(TARGET)

$(BUILD):
	mkdir -p $@

$(TARGET): $(SOURCES) $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -DGRID_EXPECTED_SOURCES=208 $(SOURCES) $(LDFLAGS) -o $@

clean:
	rm -f $(TARGET)
