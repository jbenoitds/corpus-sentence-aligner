# Plain Makefile for systems without CMake (GNU make + g++ or clang++).
#   make            build bin/csa-align
#   make test       build and run the unit tests
#   make clean

CXX      ?= g++
CXXFLAGS ?= -O2
override CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS  ?=

LIB_SRC  := src/utf8.cpp src/zip.cpp src/document.cpp src/segmenter.cpp \
            src/gale_church.cpp src/aligner.cpp src/export.cpp src/evaluate.cpp
TEST_SRC := $(wildcard tests/test_*.cpp)

LIB_OBJ  := $(LIB_SRC:src/%.cpp=build/obj/%.o)
TEST_OBJ := $(TEST_SRC:tests/%.cpp=build/obj/tests/%.o)

.PHONY: all test clean

all: bin/csa-align

bin/csa-align: build/obj/main.o $(LIB_OBJ)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

bin/csa_tests: $(TEST_OBJ) $(LIB_OBJ)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

build/obj/%.o: src/%.cpp $(wildcard include/csa/*.hpp)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/obj/tests/%.o: tests/%.cpp $(wildcard include/csa/*.hpp) tests/minitest.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -DCSA_TEST_DATA='"$(CURDIR)/tests/data"' -c -o $@ $<

test: bin/csa_tests bin/csa-align
	./bin/csa_tests
	./bin/csa-align --quiet --date 20220101T000000Z tests/data/sample_en.txt tests/data/sample_fr.txt \
	    --gold tests/data/sample_gold.tsv > /dev/null

clean:
	rm -rf build bin
