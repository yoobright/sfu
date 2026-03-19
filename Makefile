CXX      = g++
CXXFLAGS = -std=c++17 -O3

CUDA_AVAILABLE := $(shell which nvcc > /dev/null 2>&1 && echo 1 || echo 0)
VERILATOR_ROOT := $(shell verilator --getenv VERILATOR_ROOT)

BUILD_DIR = build
TARGET    = $(BUILD_DIR)/sfu_test

VERILATOR_SRCS = $(VERILATOR_ROOT)/include/verilated.cpp $(VERILATOR_ROOT)/include/verilated_threads.cpp

SUBDIRS = util cmodel cpu mpfr chisel

LIBS = util/build/libutil.a     \
	     chisel/build/libchisel.a \
       cmodel/build/libcmodel.a \
       cpu/build/libcpu.a       \
       mpfr/build/libmpfr.a

INCLUDES = -I./util/include   \
           -I./chisel/include \
           -I./cmodel/include \
           -I./cpu/include    \
           -I./mpfr/include   \
           -I$(VERILATOR_ROOT)/include

LDFLAGS = -lmpfr -lgmp

ifeq ($(CUDA_AVAILABLE), 1)
	SUBDIRS  += gpu
	CXXFLAGS += -DUSE_GPU
	LDFLAGS  += -L$(shell dirname $(shell which nvcc))/../lib64 -lcudart
	LIBS     += gpu/build/libgpu.a
	INCLUDES += -I./gpu/include
endif

all: run

run: $(TARGET)
	@LUT_PATH=./lut ./$(TARGET)

util/build/libutil.a:
	$(MAKE) -C util

cmodel/build/libcmodel.a:
	$(MAKE) -C cmodel

cpu/build/libcpu.a:
	$(MAKE) -C cpu

mpfr/build/libmpfr.a:
	$(MAKE) -C mpfr

gpu/build/libgpu.a:
	$(MAKE) -C gpu

chisel/build/libchisel.a:
	$(MAKE) -C chisel

$(TARGET): main.cpp $(LIBS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $< $(VERILATOR_SRCS) $(LIBS) $(LDFLAGS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
	for dir in $(SUBDIRS); do $(MAKE) -C $$dir clean; done

.PHONY: all clean $(SUBDIRS)
