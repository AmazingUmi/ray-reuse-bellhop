# Minimal, reproducible build for the original 2-D BELLHOP executable.
#
# Windows (MinGW):
#   mingw32-make
#
# Linux/macOS with GNU Make and gfortran:
#   make
#
# Override the compiler or flags when needed, for example:
#   mingw32-make FC=C:/path/to/gfortran.exe
#   make FFLAGS="-O0 -g -fcheck=all -fbacktrace"

FC = gfortran

BUILD_DIR = build
OBJ_DIR   = $(BUILD_DIR)/obj
MOD_DIR   = $(BUILD_DIR)/mod
BIN_DIR   = bin
TARGET    = $(BIN_DIR)/bellhop.exe

# -ffree-line-length-none is needed by several long source lines.
# Module files are kept out of the source tree with -J.
FFLAGS = -O2 -std=gnu -ffree-line-length-none -Wall -Wextra -Wno-tabs
MODFLAGS = -J$(MOD_DIR) -I$(MOD_DIR)
# Link the GNU/MinGW runtime libraries statically so bellhop.exe can run on a
# Windows machine without copying libgfortran, libgcc or libquadmath DLLs.
# Windows system DLLs (for example KERNEL32.dll) remain dynamic by design.
LDFLAGS = -static

ifeq ($(OS),Windows_NT)
make_dir = if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"
remove_dir = if exist "$(subst /,\,$1)" rmdir /S /Q "$(subst /,\,$1)"
remove_file = if exist "$(subst /,\,$1)" del /Q "$(subst /,\,$1)"
else
make_dir = mkdir -p "$1"
remove_dir = rm -rf "$1"
remove_file = rm -f "$1"
endif

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
	$(OBJ_DIR)/bellhop_main.o

OBJECTS = $(MISC_OBJECTS) $(BELLHOP_OBJECTS)

.PHONY: all bellhop clean info

all: bellhop

bellhop: $(TARGET)

info:
	@echo Compiler: $(FC)
	@echo Flags:    $(FFLAGS)
	@echo Target:   $(TARGET)

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(FC) $(LDFLAGS) -o $@ $(OBJECTS)
	@echo ""
	@echo "BELLHOP built: $@"

# -----------------------------------------------------------------------------
# Common modules.  Only the misc sources actually required by 2-D BELLHOP are
# compiled.  In particular, misc/sspMod.f90 is intentionally excluded because
# BELLHOP has its own module with the same name in Bellhop/sspMod.f90.

$(OBJ_DIR)/misc_FatalError.o: misc/FatalError.f90 | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/misc_MathConstants.o: misc/MathConstants.f90 | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/misc_monotonicMod.o: misc/monotonicMod.f90 | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/misc_SortMod.o: misc/SortMod.f90 | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/misc_subtabulate.o: misc/subtabulate.f90 | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/misc_SourceReceiverPositions.o: misc/SourceReceiverPositions.f90 \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_monotonicMod.o \
	$(OBJ_DIR)/misc_SortMod.o \
	$(OBJ_DIR)/misc_subtabulate.o | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/misc_pchipMod.o: misc/pchipMod.f90 | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/misc_AttenMod.o: misc/AttenMod.f90 \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_MathConstants.o | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/misc_PolyMod.o: misc/PolyMod.f90 | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/misc_RefCoef.o: misc/RefCoef.f90 \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_MathConstants.o \
	$(OBJ_DIR)/misc_PolyMod.o | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/misc_beampattern.o: misc/beampattern.f90 \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_monotonicMod.o | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/misc_RWSHDFile.o: misc/RWSHDFile.f90 \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_SourceReceiverPositions.o | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/misc_splinec.o: misc/splinec.f90 | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

# -----------------------------------------------------------------------------
# 2-D BELLHOP modules, in explicit Fortran module dependency order.

$(OBJ_DIR)/bellhop_bellhopMod.o: Bellhop/bellhopMod.f90 \
	$(OBJ_DIR)/misc_MathConstants.o | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/bellhop_angleMod.o: Bellhop/angleMod.f90 \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_MathConstants.o \
	$(OBJ_DIR)/misc_SortMod.o \
	$(OBJ_DIR)/misc_subtabulate.o \
	$(OBJ_DIR)/misc_SourceReceiverPositions.o | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/bellhop_ArrMod.o: Bellhop/ArrMod.f90 \
	$(OBJ_DIR)/misc_MathConstants.o \
	$(OBJ_DIR)/bellhop_bellhopMod.o | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/bellhop_bdryMod.o: Bellhop/bdryMod.f90 \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_monotonicMod.o | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/bellhop_sspMod.o: Bellhop/sspMod.f90 \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_pchipMod.o \
	$(OBJ_DIR)/misc_AttenMod.o \
	$(OBJ_DIR)/misc_splinec.o | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/bellhop_ReflectMod.o: Bellhop/ReflectMod.f90 \
	$(OBJ_DIR)/misc_AttenMod.o \
	$(OBJ_DIR)/misc_RefCoef.o \
	$(OBJ_DIR)/bellhop_bellhopMod.o \
	$(OBJ_DIR)/bellhop_sspMod.o | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/bellhop_WriteRay.o: Bellhop/WriteRay.f90 \
	$(OBJ_DIR)/bellhop_bellhopMod.o \
	$(OBJ_DIR)/bellhop_sspMod.o | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/bellhop_influence.o: Bellhop/influence.f90 \
	$(OBJ_DIR)/misc_SourceReceiverPositions.o \
	$(OBJ_DIR)/bellhop_ArrMod.o \
	$(OBJ_DIR)/bellhop_bellhopMod.o \
	$(OBJ_DIR)/bellhop_sspMod.o \
	$(OBJ_DIR)/bellhop_WriteRay.o | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/bellhop_Step.o: Bellhop/Step.f90 \
	$(OBJ_DIR)/bellhop_bdryMod.o \
	$(OBJ_DIR)/bellhop_bellhopMod.o \
	$(OBJ_DIR)/bellhop_sspMod.o | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/bellhop_ReadEnvironmentBell.o: Bellhop/ReadEnvironmentBell.f90 \
	$(OBJ_DIR)/misc_FatalError.o \
	$(OBJ_DIR)/misc_MathConstants.o \
	$(OBJ_DIR)/misc_AttenMod.o \
	$(OBJ_DIR)/misc_SourceReceiverPositions.o \
	$(OBJ_DIR)/misc_RWSHDFile.o \
	$(OBJ_DIR)/bellhop_angleMod.o \
	$(OBJ_DIR)/bellhop_bdryMod.o \
	$(OBJ_DIR)/bellhop_bellhopMod.o \
	$(OBJ_DIR)/bellhop_sspMod.o | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

$(OBJ_DIR)/bellhop_main.o: Bellhop/bellhop.f90 \
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
	$(OBJ_DIR)/bellhop_WriteRay.o | $(OBJ_DIR) $(MOD_DIR)
	$(FC) $(FFLAGS) $(MODFLAGS) -c $< -o $@

# Directory creation works with cmd.exe on Windows and with a POSIX shell.
$(OBJ_DIR) $(MOD_DIR) $(BIN_DIR):
	$(call make_dir,$@)

clean:
	$(call remove_dir,$(BUILD_DIR))
	$(call remove_file,$(TARGET))
