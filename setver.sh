#!/bin/sh

FUNTUX_MAJOR="0"
FUNTUX_MINOR="1"
FUNTUX_MINORMINOR="2"
export FUNTUX_VERSION="$FUNTUX_MAJOR.$FUNTUX_MINOR.$FUNTUX_MINORMINOR"

BBUILD="funtux-linux@$FUNTUX_VERSION.bbuild"

if ! command -v sed 2>&1 /dev/null; then
    echo "Huh, looks like I couldn't find sed on your system, I can't continue this operation without it."
    exit 1
fi

fun_ver_update() {
    old_ver="$FUNTUX_VERSION"
    new_ver="$1"

    if [ "$new_ver" = "$old_ver" ]; then
        echo "already at $old_ver"
        return 0
    fi

    old_file="funtux-linux@$old_ver.bbuild"
    new_file="funtux-linux@$new_ver.bbuild"
    old_ebuild="funtux-$old_ver.ebuild"
    new_ebuild="funtux-$new_ver.ebuild"

    if [ ! -f "$old_file" ]; then
        echo "error: $old_file not found" >&2
        return 1
    fi

    # Update this script's own version defaults (MAJOR.MINOR.MINORMINOR).
    new_major="${new_ver%%.*}"
    new_minor="${new_ver#*.}"; new_minor="${new_minor%.*}"
    new_minorminor="${new_ver##*.}"
    sed -i "s/^FUNTUX_MAJOR=\".*\"/FUNTUX_MAJOR=\"${new_major}\"/" "$0"
    sed -i "s/^FUNTUX_MINOR=\".*\"/FUNTUX_MINOR=\"${new_minor}\"/" "$0"
    sed -i "s/^FUNTUX_MINORMINOR=\".*\"/FUNTUX_MINORMINOR=\"${new_minorminor}\"/" "$0"

    # Rename the bbuild and bump the version inside it.
    mv "$old_file" "$new_file"
    sed -i "s/^pkgver=\"$old_ver\"/pkgver=\"$new_ver\"/" "$new_file"

    # Rename the gentoo ebuild (its version comes from the filename).
    if [ -f "$old_ebuild" ]; then
        mv "$old_ebuild" "$new_ebuild"
    fi

    # Bump the arch PKGBUILD version.
    if [ -f PKGBUILD ]; then
        sed -i "s/^pkgver=$old_ver$/pkgver=$new_ver/" PKGBUILD
    fi

    if [ -f version.h ]; then
        sed -i "s/^#define version \"$old_ver\"/#define version \"$new_ver\"/" version.h
    fi

    echo "bumped $old_ver -> $new_ver ($new_file, $new_ebuild, PKGBUILD)"
}

fun_new_ver() {
    read -p "setver? [ex. 1.0.0, 0.15.6, etc] " new_ver
    fun_ver_update "$new_ver"
}

case "$1" in
"") fun_new_ver ;;
*) fun_ver_update "$1" ;;
esac
