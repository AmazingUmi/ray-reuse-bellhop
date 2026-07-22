# Build the original 2-D BELLHOP on this Apple Silicon Mac.
#
# Normal build:
#   make
#
# Useful targets:
#   make info
#   make runtime-deps
#   make clean

FC = /opt/homebrew/bin/gfortran

BUILD_DIR = build
OBJ_DIR   = $(BUILD_DIR)/obj
MOD_DIR   = $(BUILD_DIR)/mod
BIN_DIR   = bin

# -O2 is the numerical-performance baseline; -g keeps useful symbols for LLDB.
# Avoid -mcpu=native and -ffast-math so results and binaries stay conservative.
FFLAGS  = -O2 -g -std=gnu -ffree-line-length-none
MODFLAGS = -J$(MOD_DIR) -I$(MOD_DIR)

# Homebrew gfortran links its runtime dylibs dynamically on macOS.
LDFLAGS =

TARGET = $(BIN_DIR)/bellhop

MISC_OBJECTS = \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_MathConstants.o \
	$(OBJ_DIR)/misc_monotonicMod.o \
	$(OBJ_DIR)/misc_SortMod.o \
	$(OBJ_DIR)/misc_subtabulate.o \
	$(OBJ_DIR)/misc_SourceReceiverPositions.o \
	$(OBJ_DIR)/misc_pchipMod.o \
	$(OBJ_DIR)/misc_AttenMod.o \
	$(OBJ_DIR)/misc_PolyMod.o \
	$(OBJ_DIR)/misc_RefCoef.o \
	$(OBJ_DIR)/misc_beampattern.o \
	$(OBJ_DIR)/misc_RWSHDFile.o \
	$(OBJ_DIR)/misc_splinec.o

BELLHOP_OBJECTS = \
	$(OBJ_DIR)/bellhop_bellhopMod.o \
	$(OBJ_DIR)/bellhop_angleMod.o \
	$(OBJ_DIR)/bellhop_ArrMod.o \
	$(OBJ_DIR)/bellhop_bdryMod.o \
	$(OBJ_DIR)/bellhop_sspMod.o \
	$(OBJ_DIR)/bellhop_ReflectMod.o \
	$(OBJ_DIR)/bellhop_WriteRay.o \
	$(OBJ_DIR)/bellhop_influence.o \
	$(OBJ_DIR)/bellhop_Step.o \
	$(OBJ_DIR)/bellhop_ReadEnvironmentBell.o \
	$(OBJ_DIR)/bellhop_bellhop.o

OBJECTS = $(MISC_OBJECTS) $(BELLHOP_OBJECTS)

.PHONY: all bellhop clean help info runtime-deps

all: bellhop

bellhop: $(TARGET)

help:
	@echo "$(MAKE) [bellhop|info|runtime-deps|clean]"
	@echo ""
	@echo "Default target: $(TARGET)"

info:
	@echo "Compiler : $(FC)"
	@echo "Flags    : $(FFLAGS)"
	@echo "LinkFlags: $(LDFLAGS)"
	@echo "Target   : $(TARGET)"

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(FC) -o $@ $(OBJECTS) $(LDFLAGS)
	@echo ""
	@echo "BELLHOP built: $@"

# Show linked macOS system and Homebrew runtime libraries.
runtime-deps: $(TARGET)
	@otool -L $(TARGET)

# Compile recipes are shared; explicit dependency declarations below preserve
# the Fortran module order and make parallel builds safe.
$(OBJ_DIR)/misc_%.o: misc/%.f90 Makefile | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/bellhop_%.o: Bellhop/%.f90 Makefile | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

# Common-module dependencies.
$(OBJ_DIR)/misc_SourceReceiverPositions.o: \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_monotonicMod.o \
	$(OBJ_DIR)/misc_SortMod.o \
	$(OBJ_DIR)/misc_subtabulate.o

$(OBJ_DIR)/misc_AttenMod.o: \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_MathConstants.o

$(OBJ_DIR)/misc_RefCoef.o: \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_MathConstants.o \
	$(OBJ_DIR)/misc_PolyMod.o

$(OBJ_DIR)/misc_beampattern.o: \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_monotonicMod.o

$(OBJ_DIR)/misc_RWSHDFile.o: \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_SourceReceiverPositions.o

# BELLHOP module dependencies. misc/sspMod.f90 is deliberately not compiled;
# the 2-D model uses Bellhop/sspMod.f90.
$(OBJ_DIR)/bellhop_bellhopMod.o: $(OBJ_DIR)/misc_MathConstants.o

$(OBJ_DIR)/bellhop_angleMod.o: \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_MathConstants.o \
	$(OBJ_DIR)/misc_SortMod.o \
	$(OBJ_DIR)/misc_subtabulate.o \
	$(OBJ_DIR)/misc_SourceReceiverPositions.o

$(OBJ_DIR)/bellhop_ArrMod.o: \
	$(OBJ_DIR)/misc_MathConstants.o \
	$(OBJ_DIR)/bellhop_bellhopMod.o

$(OBJ_DIR)/bellhop_bdryMod.o: \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_monotonicMod.o

$(OBJ_DIR)/bellhop_sspMod.o: \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_pchipMod.o \
	$(OBJ_DIR)/misc_AttenMod.o \
	$(OBJ_DIR)/misc_splinec.o

$(OBJ_DIR)/bellhop_ReflectMod.o: \
	$(OBJ_DIR)/misc_AttenMod.o \
	$(OBJ_DIR)/misc_RefCoef.o \
	$(OBJ_DIR)/bellhop_bellhopMod.o \
	$(OBJ_DIR)/bellhop_sspMod.o

$(OBJ_DIR)/bellhop_WriteRay.o: \
	$(OBJ_DIR)/bellhop_bellhopMod.o \
	$(OBJ_DIR)/bellhop_sspMod.o

$(OBJ_DIR)/bellhop_influence.o: \
	$(OBJ_DIR)/misc_SourceReceiverPositions.o \
	$(OBJ_DIR)/bellhop_ArrMod.o \
	$(OBJ_DIR)/bellhop_bellhopMod.o \
	$(OBJ_DIR)/bellhop_sspMod.o \
	$(OBJ_DIR)/bellhop_WriteRay.o

$(OBJ_DIR)/bellhop_Step.o: \
	$(OBJ_DIR)/bellhop_bdryMod.o \
	$(OBJ_DIR)/bellhop_bellhopMod.o \
	$(OBJ_DIR)/bellhop_sspMod.o

$(OBJ_DIR)/bellhop_ReadEnvironmentBell.o: \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_MathConstants.o \
	$(OBJ_DIR)/misc_AttenMod.o \
	$(OBJ_DIR)/misc_SourceReceiverPositions.o \
	$(OBJ_DIR)/misc_RWSHDFile.o \
	$(OBJ_DIR)/bellhop_angleMod.o \
	$(OBJ_DIR)/bellhop_bdryMod.o \
	$(OBJ_DIR)/bellhop_bellhopMod.o \
	$(OBJ_DIR)/bellhop_sspMod.o

$(OBJ_DIR)/bellhop_bellhop.o: \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_beampattern.o \
	$(OBJ_DIR)/misc_RefCoef.o \
	$(OBJ_DIR)/misc_SourceReceiverPositions.o \
	$(OBJ_DIR)/bellhop_angleMod.o \
	$(OBJ_DIR)/bellhop_ArrMod.o \
	$(OBJ_DIR)/bellhop_bdryMod.o \
	$(OBJ_DIR)/bellhop_bellhopMod.o \
	$(OBJ_DIR)/bellhop_influence.o \
	$(OBJ_DIR)/bellhop_ReadEnvironmentBell.o \
	$(OBJ_DIR)/bellhop_ReflectMod.o \
	$(OBJ_DIR)/bellhop_sspMod.o \
	$(OBJ_DIR)/bellhop_Step.o \
	$(OBJ_DIR)/bellhop_WriteRay.o

make_dir = mkdir -p "$1"
remove_dir = rm -rf "$1"
remove_file = rm -f "$1"

$(OBJ_DIR) $(MOD_DIR) $(BIN_DIR):
	$(call make_dir,$@)

clean:
	$(call remove_dir,build)
	$(call remove_file,$(BIN_DIR)/bellhop)
