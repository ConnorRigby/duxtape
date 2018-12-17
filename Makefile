# Look for the EI library and header files
# For crosscompiled builds, ERL_EI_INCLUDE_DIR and ERL_EI_LIBDIR must be
# passed into the Makefile.
ifeq ($(ERL_EI_INCLUDE_DIR),)
$(warning ERL_EI_INCLUDE_DIR not set. Invoke via mix)
else
ERL_CFLAGS ?= -I$(ERL_EI_INCLUDE_DIR)
endif
ifeq ($(ERL_EI_LIBDIR),)
$(warning ERL_EI_LIBDIR not set. Invoke via mix)
else
ERL_LDFLAGS ?= -L$(ERL_EI_LIBDIR)
endif

DUKTAPE_SRCDIR = ./c_src/duktape

.PHONY: all clean
all: priv priv/duxtape_nif.so

priv:
	mkdir -p priv

priv/duxtape_nif.so: $(DUKTAPE_SRCDIR)/duktape.c ./c_src/queue.c ./c_src/duxtape_nif.c
	$(CC) $^ --std=c11 -fPIC -O2 -DDEBUG -g -Wunused $(ERL_CFLAGS) $(ERL_LDFLAGS) -shared -pedantic -o $@

clean:
	$(RM) priv/duxtape_nif.so