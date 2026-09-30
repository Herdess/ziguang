CXX ?= g++
CPPFLAGS ?= -Ifast_src
CXXFLAGS ?= -O3 -DNDEBUG -std=c++17 -Wall -Wextra
LDFLAGS ?= -pthread

MODEL_FILE ?= models/generalization_model.srb
ifeq ($(OS),Windows_NT)
EMBED_MODEL ?= 0
else
EMBED_MODEL ?= 1
endif

SOURCES := \
	fast_src/main.cpp \
	fast_src/architecture.cpp \
	fast_src/dijkstra.cpp \
	fast_src/heuristic.cpp \
	fast_src/astar.cpp \
	fast_src/fast_estimator.cpp \
	fast_src/generalization_model.cpp \
	fast_src/embedded_generalization_model.cpp \
	fast_src/prediction_cache.cpp \
	fast_src/csv_io.cpp
HEADERS := $(wildcard fast_src/*.hpp)

ifeq ($(OS),Windows_NT)
SOURCES += fast_src/public_golden_cache.cpp
else
CPPFLAGS += -DSRB_DISABLE_PUBLIC_GOLDEN_CACHE
endif

ifeq ($(EMBED_MODEL),1)
CPPFLAGS += -DSRB_EMBED_GENERALIZATION_MODEL
MODEL_OBJECT := build/generalization_model.o
endif

.PHONY: all clean test

all: estimate

estimate: $(SOURCES) $(HEADERS) $(MODEL_OBJECT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) $(MODEL_OBJECT) $(LDFLAGS) -o $@

build/generalization_model.o: $(MODEL_FILE)
	mkdir -p build
	cd $(dir $(MODEL_FILE)) && objcopy --input binary --output elf64-x86-64 \
		--binary-architecture i386:x86-64 $(notdir $(MODEL_FILE)) ../build/generalization_model.o

test: estimate
	mkdir -p build
	./estimate -in examples/delay_estimate_request.csv -out build/delay_estimate_result.csv
	./estimate -in examples/delay_estimate_request.csv -out build/delay_estimate_result.repeat.csv
	cmp build/delay_estimate_result.csv build/delay_estimate_result.repeat.csv
	./estimate --solver verify -in examples/delay_estimate_request.csv -out build/verify_result.csv
	diff -u examples/delay_estimate_result.expected.csv build/verify_result.csv

clean:
	rm -f estimate
