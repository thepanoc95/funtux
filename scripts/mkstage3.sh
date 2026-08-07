#!/bin/sh

set -e

STAGE3="$(pwd)/build/stage3"

buildstage3() {
    echo ">>> Installing base system"
    pacstrap -N -U -G -M base $STAGE3
    cat /etc/hostname > build/stage3/etc/hostname
    cp -r $(pwd)/filefux/* $STAGE3/etc/
}

mkdir -p build/stage3

if ! command -v pacstrap >/dev/null 2>&1; then
    echo "Cannot proceed! please install arch-install-scripts!"
    exit 1
else
    buildstage3
fi