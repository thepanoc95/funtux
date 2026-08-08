#!/bin/sh

set -e

if [ $# -lt 1 ]; then
    echo "Usage: $0 <root-directory>"
    echo "Example: $0 /mnt/arch-root"
    exit 1
fi

ROOT="$1"

if [ ! -d "$ROOT" ]; then
    echo "Error: Directory '$ROOT' does not exist"
    exit 1
fi

echo ">>> Removing pacman from $ROOT"
echo "    Continue? [y/N] "

read -r confirm
case "$confirm" in
    [yY]|[yY][eE][sS])
        ;;
    *)
        echo "Aborted."
        exit 0
        ;;
esac

echo "Removing pacman binaries..."
rm -rf "$ROOT/usr/bin/pacman"
rm -rf "$ROOT/usr/bin/pacman-key"
rm -rf "$ROOT/usr/bin/makepkg"
rm -rf "$ROOT/usr/bin/fakeroot"

echo "Removing pacman libraries and data..."
rm -rf "$ROOT/usr/lib/pacman"
rm -rf "$ROOT/usr/share/pacman"
rm -rf "$ROOT/var/lib/pacman"
rm -rf "$ROOT/var/cache/pacman"

echo "Removing pacman configuration..."
rm -f "$ROOT/etc/pacman.conf"
rm -rf "$ROOT/etc/pacman.d"

echo "Removing pacman GPG keys..."
rm -rf "$ROOT/etc/pacman.d/gnupg" 2>/dev/null || true
rm -rf "$ROOT/var/lib/pacman/gnupg" 2>/dev/null || true
rm -rf "$ROOT/root/.gnupg" 2>/dev/null || true

echo "Removing libalpm..."
rm -f "$ROOT/usr/lib/libalpm.so"* 2>/dev/null || true
rm -rf "$ROOT/usr/include/alpm.h" 2>/dev/null || true
rm -rf "$ROOT/usr/include/libalpm" 2>/dev/null || true
rm -rf "$ROOT/usr/share/man/man3/alpm" 2>/dev/null || true

echo "Cleaning up remaining pacman-related files..."
find "$ROOT" -name "*pacman*" -delete 2>/dev/null || true
find "$ROOT" -name "*libalpm*" -delete 2>/dev/null || true

echo ">>> Pacman has been removed from $ROOT"