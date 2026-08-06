#!/bin/sh

FUNTUX_MAJOR="0"
FUNTUX_MINOR="1"
FUNTUX_MINORMINOR="0"
export FUNTUX_VERSION="$FUNTUX_MAJOR.$FUNTUX_MINOR.$FUNTUX_MINORMINOR"

fun_ver_update() {
    mv funtux-linux@ver.bbuild funtux-linux@$FUNTUX_VERSION.bbuild
    # sed command for updating version in other files.
}

fun_new_ver() {
    read -e "setver? [ex. 1.0.0, 0.15.6, etc] "
}