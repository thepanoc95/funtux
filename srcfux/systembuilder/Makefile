CC        ?= gcc
CFLAGS    ?= -O2 -Wall -Wextra -Werror
MOTIFLIBS  = -lXm -lXt -lX11

PREFIX ?= /usr/local
BINDIR  = $(PREFIX)/sbin

all: SystemBuilder

SystemBuilder: SystemBuilder.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(MOTIFLIBS) -o $@

clean:
	rm -f SystemBuilder

install: SystemBuilder
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 SystemBuilder $(DESTDIR)$(BINDIR)/SystemBuilder

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/SystemBuilder

.PHONY: all clean install uninstall
