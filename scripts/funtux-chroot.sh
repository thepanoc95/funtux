#!/bin/bash

if ! command -v chroot 2>&1 /dev/null; then
    echo "Uh...I couldn't find chroot...."
    exit 1
fi

