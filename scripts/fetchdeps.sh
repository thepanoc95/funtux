#!/bin/sh

if ! command -v wget 2>&1 /dev/null; then
	echo "I can't find wget."
	exit 1
else
	wget https://dl.suckless.org/tools/9base-6.tar.gz
fi
