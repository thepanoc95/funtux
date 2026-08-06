CXX ?= g++
CC  ?= gcc

CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2
CFLAGS   ?= -O2 -Wall -Wextra -Werror
MOTIFLIBS = -lXm -lXt -lX11
LDLIBS    =

FUNROOT_UAPI = ../funroot/uapi

PREFIX   ?= /usr/local
BINDIR    = $(PREFIX)/sbin

all: mroot # mroot-gui

mroot: main.cc
	$(CXX) $(CXXFLAGS) -I$(FUNROOT_UAPI) $(LDFLAGS) $< $(LDLIBS) -o $@

# mroot-gui: mroot-gui.c
#	$(CC) $(CFLAGS) $(LDFLAGS) $< $(MOTIFLIBS) -o $@

clean:
	rm -f mroot mroot-gui

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 mroot $(DESTDIR)$(BINDIR)/mroot
#	install -m 755 mroot-gui $(DESTDIR)$(BINDIR)/mroot-gui

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/mroot # $(DESTDIR)$(BINDIR)/mroot-gui

.PHONY: all clean install uninstall
