# Build the original 2-D BELLHOP only.
#
# Normal Windows build (standalone executable):
#   mingw32-make
#
# Optional variants:
#   mingw32-make BUILD=debug
#   mingw32-make LINK=dynamic
#   mingw32-make info
#   mingw32-make runtime-deps

FC = gfortran

# User-facing configuration. Command-line values override these defaults.
BUILD ?= release
LINK  ?= static

VALID_BUILDS = release debug
VALID_LINKS  = static dynamic

ifeq ($(filter $(BUILD),$(VALID_BUILDS)),)
$(error BUILD must be one of: $(VALID_BUILDS))
endif
ifeq ($(filter $(LINK),$(VALID_LINKS)),)
$(error LINK must be one of: $(VALID_LINKS))
endif

BUILD_DIR = build/$(BUILD)-$(LINK)
OBJ_DIR   = $(BUILD_DIR)/obj
MOD_DIR   = $(BUILD_DIR)/mod
BIN_DIR   = bin

# Keep deployment builds CPU-portable and numerically conservative.
# In particular, do not use -march=native or -ffast-math for the baseline.
COMMON_FFLAGS  = -std=gnu -ffree-line-length-none
RELEASE_FFLAGS = -O2
DEBUG_FFLAGS   = -O0 -g -Wall -Wextra -Wno-tabs -fcheck=all -fbacktrace

ifeq ($(BUILD),debug)
FFLAGS    = $(COMMON_FFLAGS) $(DEBUG_FFLAGS)
BUILD_TAG = -debug
else
FFLAGS    = $(COMMON_FFLAGS) $(RELEASE_FFLAGS)
BUILD_TAG =
endif

MODFLAGS = -J$(MOD_DIR) -I$(MOD_DIR)

# Static is the distribution default. It embeds the GNU/MinGW Fortran,
# quadmath, GCC and pthread runtimes. Windows system DLLs remain dynamic.
ifeq ($(LINK),static)
LDFLAGS  = -static
LINK_TAG =
else
LDFLAGS  =
LINK_TAG = -dynamic
endif

TARGET = $(BIN_DIR)/bellhop$(BUILD_TAG)$(LINK_TAG).exe

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
	@echo "mingw32-make [BUILD=release|debug] [LINK=static|dynamic]"
	@echo ""
	@echo "Default: BUILD=release LINK=static -> bin/bellhop.exe"
	@echo "Targets: bellhop, info, runtime-deps, clean"

info:
	@echo "Compiler : $(FC)"
	@echo "Build    : $(BUILD)"
	@echo "Link     : $(LINK)"
	@echo "Flags    : $(FFLAGS)"
	@echo "LinkFlags: $(LDFLAGS)"
	@echo "Target   : $(TARGET)"

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(FC) -o $@ $(OBJECTS) $(LDFLAGS)
	@echo ""
	@echo "BELLHOP built: $@"

# Show the DLL import table. A standalone MinGW build must not list
# libgfortran, libquadmath, libgcc or libwinpthread.
ifeq ($(OS),Windows_NT)
runtime-deps: $(TARGET)
	@objdump -p $(TARGET) | findstr /C:"DLL Name"
else
runtime-deps: $(TARGET)
	@ldd $(TARGET) || true
endif

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

ifeq ($(OS),Windows_NT)
make_dir = if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"
remove_dir = if exist "$(subst /,\,$1)" rmdir /S /Q "$(subst /,\,$1)"
remove_file = if exist "$(subst /,\,$1)" del /Q "$(subst /,\,$1)"
else
make_dir = mkdir -p "$1"
remove_dir = rm -rf "$1"
remove_file = rm -f "$1"
endif

$(OBJ_DIR) $(MOD_DIR) $(BIN_DIR):
	$(call make_dir,$@)

clean:
	$(call remove_dir,build)
	$(call remove_file,$(BIN_DIR)/bellhop.exe)
	$(call remove_file,$(BIN_DIR)/bellhop-debug.exe)
	$(call remove_file,$(BIN_DIR)/bellhop-dynamic.exe)
	$(call remove_file,$(BIN_DIR)/bellhop-debug-dynamic.exe)
