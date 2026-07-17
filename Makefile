#-------------------------------------------------------------------------------
# Minimal wut RPL (exports OverlayProbe). Output: overlay.rpl
#
# Export table: source/exports.s is generated from exports.def with rplexportgen.
# Regenerate after editing exports.def:
#     make exports
# Debug build (breadcrumbs enabled, -O0):
#     make debug                  # -> tphd_tools_debug.rpl
#-------------------------------------------------------------------------------
.SUFFIXES:
#-------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)

include $(DEVKITPRO)/wut/share/wut_rules

#-------------------------------------------------------------------------------
# Project layout:
#   src/            main.cpp + build glue (gx2_backend shim, crt0, rpl malloc, exports)
#   src/utils/      renderer / input / menu modules
#   src/debug/      debug tools (Link Position, ...)
#   include/{utils,debug}/  matching headers
# Dear ImGui core lives one level up (shared with the standalone app):
# external/imgui. The GX2 renderer backend is compiled via src/gx2_backend.cpp
# (a unity shim) so we leave out the swkbd-heavy imgui_impl_wiiu.cpp;
# backends/wiiu is on the include path only.
DEBUG		?=	0
EXPERIMENTAL	?=	0
VERSION		?=
VERSION		:=	$(strip $(VERSION))
ifeq ($(VERSION),)
VERSION		:=	$(shell powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(TOPDIR)/scripts/source_crc32.ps1" -ProjectRoot "$(TOPDIR)")
endif
ifeq ($(strip $(VERSION)),)
$(error "Unable to calculate the automatic source CRC32 version")
endif
ROOT_TARGET	:=	tphd_tools
ifeq ($(EXPERIMENTAL),1)
BASE_TARGET	:=	$(ROOT_TARGET)_experimental
BUILD_SUFFIX	:=	_experimental
EXPERIMENTAL_DEFINES := -DTPHD_TOOLS_EXPERIMENTAL=1
else
BASE_TARGET	:=	$(ROOT_TARGET)
BUILD_SUFFIX	:=
EXPERIMENTAL_DEFINES :=
endif
ifeq ($(DEBUG),1)
TARGET		:=	$(BASE_TARGET)_debug
BUILD		:=	build$(BUILD_SUFFIX)_debug
OPTFLAGS	:=	-O0
DEBUG_DEFINES	:=	-DTPHD_TOOLS_DEBUG=1
else
TARGET		:=	$(BASE_TARGET)
BUILD		:=	build$(BUILD_SUFFIX)
OPTFLAGS	:=	-O2
DEBUG_DEFINES	:=
endif
VERSION_DEFINE	:=	'-DTPHD_TOOLS_VERSION_BASE="$(VERSION)"'
SOURCES		:=	src src/cemu src/utils src/debug src/tools src/cheats external/imgui external/cjson
DATA		:=	data
INCLUDES	:=	include include/utils include/debug include/tools include/cheats external/imgui external/imgui/backends/wiiu external/cjson

#-------------------------------------------------------------------------------
# options for code generation
#-------------------------------------------------------------------------------
CFLAGS	:=	-g -Wall $(OPTFLAGS) -ffunction-sections \
			$(MACHDEP)

CFLAGS	+=	$(INCLUDE) -D__WIIU__ -D__WUT__ $(DEBUG_DEFINES) \
			$(EXPERIMENTAL_DEFINES) $(VERSION_DEFINE)

CXXFLAGS	:= $(CFLAGS) -fno-exceptions -fno-rtti

ASFLAGS	:=	-g $(ARCH)
# RPLSPECS makes this an RPL (relocatable, with export/import tables) rather
# than an RPX (main executable).
LDFLAGS	=	-g $(ARCH) $(RPLSPECS) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lwut

#-------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(WUT_ROOT)

#-------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#-------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

# Dear ImGui demo is not used by this project (ShowDemoWindow is never called).
CPPFILES	:=	$(filter-out imgui_demo.cpp,$(CPPFILES))
# Experimental tools are absent from standard binaries, not merely hidden in UI.
ifneq ($(EXPERIMENTAL),1)
CPPFILES	:=	$(filter-out auto_splitter.cpp boss_practice.cpp,$(CPPFILES))
endif
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#-------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif
#-------------------------------------------------------------------------------

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean all debug exports

#-------------------------------------------------------------------------------
all: $(BUILD)

debug:
	@$(MAKE) --no-print-directory DEBUG=1 all

# (Re)generate the export table assembly from exports.def.
exports: src/cemu/exports.s
src/cemu/exports.s: exports.def
	rplexportgen $< $@

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#-------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr build build_debug build_experimental build_experimental_debug \
		$(ROOT_TARGET).rpl $(ROOT_TARGET).elf \
		$(ROOT_TARGET)_debug.rpl $(ROOT_TARGET)_debug.elf \
		$(ROOT_TARGET)_experimental.rpl $(ROOT_TARGET)_experimental.elf \
		$(ROOT_TARGET)_experimental_debug.rpl $(ROOT_TARGET)_experimental_debug.elf

#-------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#-------------------------------------------------------------------------------
all	:	$(OUTPUT).rpl

$(OUTPUT).rpl	:	$(OUTPUT).elf
$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN)

%.bin.o	%_bin.h :	%.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#-------------------------------------------------------------------------------
endif
#-------------------------------------------------------------------------------
