CXX      = g++
CXXFLAGS = -std=c++17 -O3 -Wall -I./include -I../cmodel/include
AR       = ar

BUILD_DIR = build
OBJ_DIR   = $(BUILD_DIR)/obj

SRCS   = $(wildcard src/*.cpp)
OBJS   = $(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))
TARGET = $(BUILD_DIR)/libmpfr.a

all: $(TARGET)

$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(AR) rcs $@ $^

$(OBJ_DIR)/%.o: src/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
