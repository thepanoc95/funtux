CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11

PREFIX ?= /usr/local
BINDIR  = $(PREFIX)/sbin
BUILDDIR := $(CURDIR)/../../build/bin

FUNROOT_UAPI = ../funroot/uapi

all: $(BUILDDIR) msubsys funroot

msubsys: msubsys.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -o $(BUILDDIR)/$@

funroot: funrootctl.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(FUNROOT_UAPI) $(LDFLAGS) $< $(LDLIBS) -o $(BUILDDIR)/$@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(BUILDDIR)/msubsys $(DESTDIR)$(BINDIR)/msubsys
	install -m 755 $(BUILDDIR)/funroot $(DESTDIR)$(BINDIR)/funroot

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/msubsys $(DESTDIR)$(BINDIR)/funroot

.PHONY: all clean install uninstall
