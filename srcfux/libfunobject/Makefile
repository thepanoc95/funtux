CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Werror -std=c11
PREFIX  ?= /usr/local

all: libfunobj.so funobj_test

funobj.o: funobj.c funobj.h
	$(CC) $(CFLAGS) -fPIC -c funobj.c -o $@

libfunobj.so: funobj.o
	ar rcs $@ $<

funobj_test: funobj_test.c funobj.o funobj.h
	$(CC) $(CFLAGS) funobj_test.c funobj.o -o $@

test: funobj_test
	./funobj_test

clean:
	rm -f funobj.o libfunobj.a funobj_test

install: libfunobj.so funobj.h
	install -d $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include/funobj
	install -m 644 libfunobj.a $(DESTDIR)$(PREFIX)/lib/
	install -m 644 funobj.h $(DESTDIR)$(PREFIX)/include/funobj/

.PHONY: all test clean install
