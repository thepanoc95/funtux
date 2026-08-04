CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11

PREFIX ?= /usr/local
BINDIR  = $(PREFIX)/sbin

all: msubsys

msubsys: msubsys.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -o $@

clean:
	rm -f msubsys

install: msubsys
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 msubsys $(DESTDIR)$(BINDIR)/msubsys

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/msubsys

.PHONY: all clean install uninstall
