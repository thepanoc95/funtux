set shell := ["bash", "-cu"]

DIRS := 'srcfux/bin srcfux/mroot srcfux/libfunobject'

build:
    @for d in {{DIRS}}; do make -C "$d"; done
    @makepkg -f

clean:
    @for d in {{DIRS}}; do make -C "$d" clean; done
    @rm -rf pkg src build
    @rm *.zst

test:
    @make -C srcfux/libfunobject test

install DESTDIR="" PREFIX="/usr/local":
    @for d in {{DIRS}}; do \
        make -C "$d" DESTDIR="{{DESTDIR}}" PREFIX="{{PREFIX}}" install; \
    done

uninstall DESTDIR="" PREFIX="/usr/local":
    @for d in {{DIRS}}; do \
        make -C "$d" DESTDIR="{{DESTDIR}}" PREFIX="{{PREFIX}}" uninstall; \
    done

mod KVER="":
    make -C srcfux/funroot {{KVER}}

mod-install KVER="":
    make -C srcfux/funroot {{KVER}} install

mod-clean KVER="":
    make -C srcfux/funroot {{KVER}} clean

version:
    @grep -h '^FUNTUX_\(MAJOR\|MINOR\|MINORMINOR\)' setver.sh \
        | cut -d'"' -f2 | paste -sd '.' -

bump VER:
    @./setver.sh {{VER}}

pkg-arch:
    @FUNTUX_SRC="{{justfile_directory()}}" makepkg -f

pkg-gentoo:
    @FUNTUX_SRC="{{justfile_directory()}}" emerge funtux

# Build the Bedrock bbuild stratum
pkg-bedrock:
    @FUNTUX_SRC="{{justfile_directory()}}" bpt build funtux-linux@*.bbuild

# Fetch build dependencies (delegates to fetchdeps.sh)
deps:
    @./scripts/fetchdeps.sh

# Show all available recipes
default:
    @just --list
