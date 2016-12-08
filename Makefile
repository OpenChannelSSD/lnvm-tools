CFLAGS := -std=gnu99 -O2 -g -Wall
LDFLAGS := -llightnvm -fopenmp
EXEC = lnvm-tool
INSTALL ?= install
DESTDIR =
PREFIX ?= /usr/local
SBINDIR = $(PREFIX)/sbin

default: $(EXEC)

lnvm-tool: lnvm.c $(LIGHTNVM_HEADER)
	$(CC) $(CFLAGS) lnvm.c $(LDFLAGS) -o $(EXEC)

all: lnvm-tool

clean:
	rm -f $(EXEC) *.o *~ a.out

clobber: clean

install-bin: default
	$(INSTALL) -d $(DESTDIR)$(SBINDIR)
	$(INSTALL) -m 755 lnvm-tool $(DESTDIR)$(SBINDIR)

install: install-bin

.PHONY: default all clean clobber install
