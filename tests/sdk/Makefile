# Rebuilds the test programs in bin/ with the ps2dev toolchain and ps2sdk.
# The tests use the binaries checked in under bin/, so this is only needed
# after changing a source file:
#
#   export PS2DEV=/path/to/ps2dev PS2SDK=$PS2DEV/ps2sdk
#   export PATH=$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin:$PATH
#   make -C tests/sdk

EE_PROGRAMS = iop_rpc iop_sd pad padx vu cdvd mc
IOP_MODULES = rpctest sdtest

# Sources, extra objects and flags per EE program (default sources: the .c
# files in the directory of that name).
SRC_pad = pad/main.c
LIBS_pad = -lpad
SRC_padx = pad/main.c
CFLAGS_padx = -DXPAD
LIBS_padx = -lpadx
OBJS_vu = obj/vu0.o obj/vu1.o
LIBS_vu = -ldma
LIBS_cdvd = -lcdvd
LIBS_mc = -lmc

EE_CC = mips64r5900el-ps2-elf-gcc
EE_STRIP = mips64r5900el-ps2-elf-strip
EE_DVP = dvp-as
EE_CFLAGS = -D_EE -G0 -O2 -Wall -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include
EE_LDFLAGS = -T$(PS2SDK)/ee/startup/linkfile -L$(PS2SDK)/ee/lib -Wl,-zmax-page-size=128

all: $(EE_PROGRAMS:%=bin/%.elf) $(IOP_MODULES:%=bin/%.irx)

# EE programs, linked with ps2sdk. Debug information is stripped; symbols
# stay, the recompiler uses them.
define ee_program
bin/$(1).elf: $$(or $$(SRC_$(1)),$$(wildcard $(1)/*.c)) $$(OBJS_$(1))
	@mkdir -p bin obj
	$$(EE_CC) $$(EE_CFLAGS) $$(CFLAGS_$(1)) -o obj/$(1).elf $$^ $$(EE_LDFLAGS) $$(LIBS_$(1))
	$$(EE_STRIP) --strip-debug -o $$@ obj/$(1).elf
endef
$(foreach p,$(EE_PROGRAMS),$(eval $(call ee_program,$(p))))

# VU micro programs.
obj/%.o: vu/%.vsm
	@mkdir -p obj
	$(EE_DVP) $< -o $@

# IOP modules: built with ps2sdk's own makefiles.
bin/%.irx: %/*.c %/imports.lst
	@mkdir -p bin
	$(MAKE) -C $* -f $(CURDIR)/irx.mk IOP_BIN=$*.irx
	cp $*/$*.irx $@

clean:
	rm -rf obj $(IOP_MODULES:%=%/obj) $(IOP_MODULES:%=%/*.irx) $(IOP_MODULES:%=%/*.elf)

.PHONY: all clean
