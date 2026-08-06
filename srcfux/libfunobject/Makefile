CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11
PREFIX ?= /usr/local

FUNROOT_UAPI = ../funroot/uapi

OBJS = funobj.o funroot.o

all: libfunobj.a libfunobj.so froot funobj_test froot_test

funobj.o: funobj.c funobj.h
	$(CC) $(CFLAGS) -fPIC -c funobj.c -o $@

funroot.o: funroot.c funroot.h funobj.h $(FUNROOT_UAPI)/funroot.h
	$(CC) $(CFLAGS) -fPIC -c funroot.c -o $@

libfunobj.a: $(OBJS)
	ar rcs $@ $^

libfunobj.so: $(OBJS)
	$(CC) $(CFLAGS) -shared -o $@ $^

funobj_test: funobj_test.c funobj.o funobj.h
	$(CC) $(CFLAGS) funobj_test.c funobj.o -o $@

froot_test: froot_test.c $(OBJS) funobj.h funroot.h
	$(CC) $(CFLAGS) froot_test.c $(OBJS) -o $@

froot: froot.c $(OBJS) funobj.h funroot.h
	$(CC) $(CFLAGS) froot.c $(OBJS) -o $@

test: funobj_test froot_test
	./funobj_test
	./froot_test

clean:
	rm -f funobj.o funroot.o libfunobj.a libfunobj.so funobj_test froot_test froot

install: libfunobj.a libfunobj.so froot funobj.h funroot.h
	install -d $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include/funobj $(DESTDIR)$(PREFIX)/sbin
	install -m 644 libfunobj.a libfunobj.so $(DESTDIR)$(PREFIX)/lib/
	install -m 644 funobj.h funroot.h $(DESTDIR)$(PREFIX)/include/funobj/
	install -m 755 froot $(DESTDIR)$(PREFIX)/sbin/froot

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/libfunobj.a $(DESTDIR)$(PREFIX)/lib/libfunobj.so
	rm -f $(DESTDIR)$(PREFIX)/include/funobj/funobj.h $(DESTDIR)$(PREFIX)/include/funobj/funroot.h
	rm -f $(DESTDIR)$(PREFIX)/sbin/froot

.PHONY: all test clean install uninstall
