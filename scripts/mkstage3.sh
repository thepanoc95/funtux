#!/bin/sh

set -e

STAGE3="$(pwd)/build/stage3"

buildstage3() {
    echo ">>> Installing base system"
    pacstrap -N -U -G -M -U ../build/funtux-linux.pkg.tar.zst $STAGE3
    # cat /etc/hostname > build/stage3/etc/hostname
}

mkdir -p build/stage3

if ! command -v pacstrap >/dev/null 2>&1; then
    echo "Cannot proceed! please install arch-install-scripts!"
    exit 1
else
    buildstage3
    STATUS=$?
    if [ STATUS -eq 0 ]; then
        source rmpacman.sh
    fi
fi