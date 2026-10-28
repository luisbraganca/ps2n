# Builds one IOP module (IOP_BIN) from the .c files and imports.lst of the
# current directory, with ps2sdk's sample makefiles. Called by Makefile.

IOP_OBJS = $(patsubst %.c,%.o,$(wildcard *.c)) imports.o

all: $(IOP_BIN)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal
