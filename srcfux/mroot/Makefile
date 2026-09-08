CXX ?= g++
CC  ?= gcc

CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2
CFLAGS   ?= -O2 -Wall -Wextra -Werror
MOTIFLIBS = -lXm -lXt -lX11
LDLIBS    =

FUNROOT_UAPI = ../funroot/uapi

PREFIX   ?= /usr/local
BINDIR    = $(PREFIX)/sbin
BUILDDIR := $(CURDIR)/../../build/mroot

SRCS = main.cc utils.cc media.cc kernel.cc roots.cc
OBJS = $(patsubst %.cc,$(BUILDDIR)/%.o,$(SRCS))

all: $(BUILDDIR) mroot

mroot: $(OBJS) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -I$(FUNROOT_UAPI) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $(BUILDDIR)/$@

$(BUILDDIR)/%.o: %.cc | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -I$(FUNROOT_UAPI) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(BUILDDIR)/mroot $(DESTDIR)$(BINDIR)/mroot
#	install -m 755 mroot-gui $(DESTDIR)$(BINDIR)/mroot-gui

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/mroot # $(DESTDIR)$(BINDIR)/mroot-gui

.PHONY: all clean install uninstall
