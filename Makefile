CXX      = g++
CXXFLAGS = -std=c++17 -O3

CUDA_AVAILABLE := $(shell which nvcc > /dev/null 2>&1 && echo 1 || echo 0)
VERILATOR_ROOT := $(shell command -v verilator >/dev/null 2>&1 && verilator --getenv VERILATOR_ROOT)

BUILD_DIR = build
TARGET    = $(BUILD_DIR)/sfu_test
SIGMOID_TEST = $(BUILD_DIR)/sigmoid_test
SIGMOID_ACCURACY = $(BUILD_DIR)/sigmoid_accuracy
EXP_TEST = $(BUILD_DIR)/exp_test
EXP_ACCURACY = $(BUILD_DIR)/exp_accuracy
TANH_TEST = $(BUILD_DIR)/tanh_test
TANH_ACCURACY = $(BUILD_DIR)/tanh_accuracy

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

test-cmodel: $(SIGMOID_TEST) $(EXP_TEST) $(TANH_TEST) $(BUILD_DIR)/exp_centered_test
	@LUT_PATH=./lut ./$(SIGMOID_TEST)
	@LUT_PATH=./lut ./$(EXP_TEST)
	@LUT_PATH=./lut ./$(TANH_TEST)
	@LUT_PATH=./lut ./$(BUILD_DIR)/exp_centered_test

accuracy-sigmoid: $(SIGMOID_ACCURACY)
	@LUT_PATH=./lut ./$(SIGMOID_ACCURACY)

accuracy-exp: $(EXP_ACCURACY)
	@LUT_PATH=./lut ./$(EXP_ACCURACY)

accuracy-tanh: $(TANH_ACCURACY)
	@LUT_PATH=./lut ./$(TANH_ACCURACY)

generate-tanh-lut:
	python3 tools/gen_tanh_lut.py

run: $(TARGET)
	@LUT_PATH=./lut ./$(TARGET)

util/build/libutil.a:
	$(MAKE) -C util

cmodel/build/libcmodel.a: $(wildcard cmodel/src/*.cpp cmodel/include/*.h) cmodel/Makefile
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

$(SIGMOID_TEST): tests/sigmoid_test.cpp cmodel/build/libcmodel.a | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I./cmodel/include -o $@ $< cmodel/build/libcmodel.a

$(SIGMOID_ACCURACY): tests/sigmoid_accuracy.cpp cmodel/build/libcmodel.a | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I./cmodel/include -o $@ $< cmodel/build/libcmodel.a

$(EXP_TEST): tests/exp_test.cpp cmodel/build/libcmodel.a | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I./cmodel/include -o $@ $< cmodel/build/libcmodel.a

$(EXP_ACCURACY): tests/exp_accuracy.cpp cmodel/build/libcmodel.a | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I./cmodel/include -o $@ $< cmodel/build/libcmodel.a

$(TANH_TEST): tests/tanh_test.cpp cmodel/build/libcmodel.a | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I./cmodel/include -o $@ $< cmodel/build/libcmodel.a

$(TANH_ACCURACY): tests/tanh_accuracy.cpp cmodel/build/libcmodel.a | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I./cmodel/include -o $@ $< cmodel/build/libcmodel.a

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
	for dir in $(SUBDIRS); do $(MAKE) -C $$dir clean; done

.PHONY: all clean test-cmodel accuracy-sigmoid accuracy-exp accuracy-tanh generate-tanh-lut $(SUBDIRS)

# Bit-for-bit EXP comparison against freshly elaborated RTL.
test-exp-rtl: cmodel/build/libcmodel.a | $(BUILD_DIR)
	$(MAKE) -C chisel
	$(CXX) $(CXXFLAGS) -DTEST_EXP_RTL $(INCLUDES) -o $(BUILD_DIR)/exp_rtl_test tests/exp_test.cpp $(VERILATOR_SRCS) chisel/build/libchisel.a cmodel/build/libcmodel.a
	LUT_PATH=./lut ./$(BUILD_DIR)/exp_rtl_test
	$(CXX) $(CXXFLAGS) -DTEST_EXP_RTL $(INCLUDES) -o $(BUILD_DIR)/exp_centered_rtl_test tests/exp_centered_test.cpp $(VERILATOR_SRCS) chisel/build/libchisel.a cmodel/build/libcmodel.a
	LUT_PATH=./lut ./$(BUILD_DIR)/exp_centered_rtl_test

.PHONY: test-exp-rtl

$(BUILD_DIR)/exp_centered_test: tests/exp_centered_test.cpp cmodel/build/libcmodel.a | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I./cmodel/include -o $@ $< cmodel/build/libcmodel.a

generate-exp2-lut:
	python3 tools/gen_exp2_centered_lut.py
	cp lut/exp2-coeffs.txt lut/exp2-coeffs.txt.backup
	cp lut/exp2-coeffs.txt optimizer/lut-optimized/exp2-coeffs.txt
	cp lut/exp2-coeffs.txt optimizer/lut-optimized/exp2-coeffs.txt.backup

check-exp2-lut:
	python3 tools/gen_exp2_centered_lut.py --check
	cmp lut/exp2-coeffs.txt lut/exp2-coeffs.txt.backup
	cmp lut/exp2-coeffs.txt optimizer/lut-optimized/exp2-coeffs.txt
	cmp lut/exp2-coeffs.txt optimizer/lut-optimized/exp2-coeffs.txt.backup

accuracy-exp2: $(BUILD_DIR)/exp2_accuracy
	LUT_PATH=./lut ./$(BUILD_DIR)/exp2_accuracy

$(BUILD_DIR)/exp2_accuracy: tests/exp2_accuracy.cpp cmodel/build/libcmodel.a | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I./cmodel/include -o $@ $< cmodel/build/libcmodel.a

.PHONY: generate-exp2-lut check-exp2-lut accuracy-exp2
