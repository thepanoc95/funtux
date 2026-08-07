CC     ?= gcc
CFLAGS ?= -O2 -Wall -Werror -std=c11
PREFIX ?= /usr/local

FUNROOT_UAPI = ../funroot/uapi
BUILDDIR     := $(CURDIR)/../../build/libfunobject

OBJS = $(BUILDDIR)/funobj.o $(BUILDDIR)/funroot.o

all: $(BUILDDIR) libfunobj.a libfunobj.so froot funobj_test froot_test

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/funobj.o: funobj.c funobj.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -fPIC -c funobj.c -o $@

$(BUILDDIR)/funroot.o: funroot.c funroot.h funobj.h $(FUNROOT_UAPI)/funroot.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -fPIC -c funroot.c -o $@

libfunobj.a: $(OBJS)
	ar rcs $(BUILDDIR)/$@ $^

libfunobj.so: $(OBJS)
	$(CC) $(CFLAGS) -shared -o $(BUILDDIR)/$@ $^

funobj_test: funobj_test.c $(BUILDDIR)/funobj.o funobj.h
	$(CC) $(CFLAGS) funobj_test.c $(BUILDDIR)/funobj.o -o $(BUILDDIR)/$@

froot_test: froot_test.c $(OBJS) funobj.h funroot.h
	$(CC) $(CFLAGS) froot_test.c $(OBJS) -o $(BUILDDIR)/$@

froot: froot.c $(OBJS) funobj.h funroot.h
	$(CC) $(CFLAGS) froot.c $(OBJS) -o $(BUILDDIR)/$@

test: funobj_test froot_test
	$(BUILDDIR)/funobj_test
	$(BUILDDIR)/froot_test

clean:
	rm -rf $(BUILDDIR)

install: libfunobj.a libfunobj.so froot funobj.h funroot.h
	install -d $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include/funobj $(DESTDIR)$(PREFIX)/sbin
	install -m 644 $(BUILDDIR)/libfunobj.a $(BUILDDIR)/libfunobj.so $(DESTDIR)$(PREFIX)/lib/
	install -m 644 funobj.h funroot.h $(DESTDIR)$(PREFIX)/include/funobj/
	install -m 755 $(BUILDDIR)/froot $(DESTDIR)$(PREFIX)/sbin/froot

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/libfunobj.a $(DESTDIR)$(PREFIX)/lib/libfunobj.so
	rm -f $(DESTDIR)$(PREFIX)/include/funobj/funobj.h $(DESTDIR)$(PREFIX)/include/funobj/funroot.h
	rm -f $(DESTDIR)$(PREFIX)/sbin/froot

.PHONY: all test clean install uninstall
