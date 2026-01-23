CXX      = g++
CXXFLAGS = -std=c++17 -O3 -march=native -fopenmp -Wall -Wextra
LDFLAGS  = -lmpfr -lgmp -fopenmp

INCLUDES = -I./include \
           -I../cmodel/include \
           -I../mpfr/include \
           -I../util/include

BUILD_DIR = build
OBJ_DIR   = $(BUILD_DIR)/obj
CACHE_DIR = cache

# Library dependencies
LIBS = ../cmodel/build/libcmodel.a \
       ../mpfr/build/libmpfr.a \
       ../util/build/libutil.a

# Source files
SRCS = $(wildcard src/*.cpp)
OBJS = $(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(filter-out src/main.cpp,$(SRCS)))
MAIN_OBJ = $(OBJ_DIR)/main.o

TARGET = $(BUILD_DIR)/optimizer

all: $(TARGET)

$(TARGET): $(OBJS) $(MAIN_OBJ) $(LIBS) | $(BUILD_DIR) $(CACHE_DIR)
	$(CXX) $(CXXFLAGS) $(OBJS) $(MAIN_OBJ) $(LIBS) $(LDFLAGS) -o $@

$(OBJ_DIR)/%.o: src/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(CACHE_DIR):
	mkdir -p $(CACHE_DIR)

# Ensure dependencies are built
../cmodel/build/libcmodel.a:
	$(MAKE) -C ../cmodel

../mpfr/build/libmpfr.a:
	$(MAKE) -C ../mpfr

../util/build/libutil.a:
	$(MAKE) -C ../util

clean:
	rm -rf $(BUILD_DIR) $(CACHE_DIR)

.PHONY: all clean
