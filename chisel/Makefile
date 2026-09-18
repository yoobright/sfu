.DEFAULT_GOAL := all
CXX        = g++
AR         = ar
VERILATOR  = verilator
MILL       = ./mill

ROOT_DIR   = $(abspath .)
BUILD_DIR  = $(ROOT_DIR)/build
OBJ_DIR    = $(BUILD_DIR)/obj_dir
TARGET     = $(BUILD_DIR)/libchisel.a

VERILOG    = $(ROOT_DIR)/generated/SFU.sv
SIM_SRC    = $(ROOT_DIR)/src/chisel.cpp

VERILATOR_ROOT = $(shell verilator --getenv VERILATOR_ROOT)

CXXFLAGS = -std=c++17 -O3
INCLUDES = -I$(ROOT_DIR)/include -I$(OBJ_DIR) -I$(VERILATOR_ROOT)/include -I$(ROOT_DIR)/../cmodel/include

VERILATOR_FLAGS = --cc --top-module SFU -Mdir $(OBJ_DIR)

.PHONY: all verilog verilate clean force-verilog

# Tracked generated RTL may be older than the source after a checkout.
# Always elaborate before simulation; never silently run stale EXP logic.
force-verilog:

all: $(TARGET)

verilog: $(VERILOG)

$(VERILOG): force-verilog scala/SFU.scala $(wildcard ../lut/*-coeffs.txt)
	@LUT_PATH=../lut $(MILL) --no-server SFU.run

verilate: $(OBJ_DIR)/VSFU__ALL.o

$(OBJ_DIR)/VSFU__ALL.o: $(VERILOG) | $(OBJ_DIR)
	$(VERILATOR) $(VERILATOR_FLAGS) $(VERILOG) -CFLAGS "$(INCLUDES)"
	$(MAKE) -C $(OBJ_DIR) -f VSFU.mk

$(OBJ_DIR)/chisel.o: $(SIM_SRC) | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET): $(OBJ_DIR)/VSFU__ALL.o $(OBJ_DIR)/chisel.o
	$(AR) rcs $@ $^

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OBJ_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
