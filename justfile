set shell := ["bash", "-cu"]

# --- sources ---------------------------------------------------------

DIRS := 'srcfux/bin srcfux/mroot srcfux/libfunobject'

# Build all the FunTux userspace tooling
build:
    @for d in {{DIRS}}; do make -C "$d"; done

# Remove build artifacts from every source tree
clean:
    @for d in {{DIRS}}; do make -C "$d" clean; done

# Run the libfunobject unit tests
test:
    @make -C srcfux/libfunobject test

# Install into DESTDIR (default /usr/local)
install DESTDIR="" PREFIX="/usr/local":
    @for d in {{DIRS}}; do \
        make -C "$d" DESTDIR="{{DESTDIR}}" PREFIX="{{PREFIX}}" install; \
    done

# Remove the installed files
uninstall DESTDIR="" PREFIX="/usr/local":
    @for d in {{DIRS}}; do \
        make -C "$d" DESTDIR="{{DESTDIR}}" PREFIX="{{PREFIX}}" uninstall; \
    done

# --- kernel module ---------------------------------------------------

# Build the funroot kernel module (needs kernel headers)
mod KVER="":
    make -C srcfux/funroot {{KVER}}

# Install the funroot kernel module into the running kernel
mod-install KVER="":
    make -C srcfux/funroot {{KVER}} install

# Remove the funroot kernel module build artifacts
mod-clean KVER="":
    make -C srcfux/funroot {{KVER}} clean

# --- versioning ------------------------------------------------------

# Show the current FunTux version
version:
    @grep -h '^FUNTUX_\(MAJOR\|MINOR\|MINORMINOR\)' setver.sh \
        | cut -d'"' -f2 | paste -sd '.' -

# Bump the version everywhere: just bump 0.2.0
bump VER:
    @./setver.sh {{VER}}

# --- packaging -------------------------------------------------------

# Build the Arch PKGBUILD
pkg-arch:
    @FUNTUX_SRC="{{justfile_directory()}}" makepkg -f

# Build the Gentoo ebuild (export FUNTUX_SRC or run from a checkout)
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
