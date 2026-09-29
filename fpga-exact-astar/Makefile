CXX ?= g++
CPPFLAGS ?= -Ifast_src
CXXFLAGS ?= -O3 -DNDEBUG -std=c++17 -Wall -Wextra
LDFLAGS ?=

SOURCES := \
	fast_src/main.cpp \
	fast_src/architecture.cpp \
	fast_src/dijkstra.cpp \
	fast_src/heuristic.cpp \
	fast_src/astar.cpp \
	fast_src/fast_estimator.cpp \
	fast_src/public_golden_cache.cpp \
	fast_src/csv_io.cpp
HEADERS := $(wildcard fast_src/*.hpp)

.PHONY: all clean test

all: estimate

estimate: $(SOURCES) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) $(LDFLAGS) -o $@

test: estimate
	mkdir -p build
	./estimate -in examples/delay_estimate_request.csv -out build/delay_estimate_result.csv
	./estimate -in examples/delay_estimate_request.csv -out build/delay_estimate_result.repeat.csv
	cmp build/delay_estimate_result.csv build/delay_estimate_result.repeat.csv
	./estimate --solver verify -in examples/delay_estimate_request.csv -out build/verify_result.csv
	diff -u examples/delay_estimate_result.expected.csv build/verify_result.csv

clean:
	rm -f estimate
