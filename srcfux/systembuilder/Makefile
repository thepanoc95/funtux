CC        ?= gcc
CFLAGS    ?= -O2 -Wall -Wextra -Werror
MOTIFLIBS  = -lXm -lXt -lX11

PREFIX ?= /usr/local
BINDIR  = $(PREFIX)/sbin
BUILDDIR := $(CURDIR)/../../build/systembuilder

all: $(BUILDDIR) SystemBuilder

SystemBuilder: SystemBuilder.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(MOTIFLIBS) -o $(BUILDDIR)/$@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(BUILDDIR)/SystemBuilder $(DESTDIR)$(BINDIR)/SystemBuilder

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/SystemBuilder

.PHONY: all clean install uninstall
