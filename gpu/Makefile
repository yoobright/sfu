NVCC      = nvcc
NVCCFLAGS = -O3 -std=c++17 --use_fast_math -I./include -I../cmodel/include
AR        = ar

BUILD_DIR = build
OBJ_DIR   = $(BUILD_DIR)/obj

SRCS   = $(wildcard src/*.cu)
OBJS   = $(patsubst src/%.cu,$(OBJ_DIR)/%.o,$(SRCS))
TARGET = $(BUILD_DIR)/libgpu.a

all: $(TARGET)

$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(AR) rcs $@ $^

$(OBJ_DIR)/%.o: src/%.cu | $(OBJ_DIR)
	$(NVCC) $(NVCCFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
