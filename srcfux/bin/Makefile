CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11

PREFIX ?= /usr/local
BINDIR  = $(PREFIX)/sbin

FUNROOT_UAPI = ../funroot/uapi

all: msubsys funroot

msubsys: msubsys.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -o $@

funroot: funrootctl.c
	$(CC) $(CFLAGS) -I$(FUNROOT_UAPI) $(LDFLAGS) $< $(LDLIBS) -o $@

clean:
	rm -f msubsys funroot

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 msubsys $(DESTDIR)$(BINDIR)/msubsys
	install -m 755 funroot $(DESTDIR)$(BINDIR)/funroot

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/msubsys $(DESTDIR)$(BINDIR)/funroot

.PHONY: all clean install uninstall
