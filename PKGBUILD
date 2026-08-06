# Maintainer: FunTux
#
# Automates the FunTux Linux build for pacman (Arch Linux).  Mirrors
# funtux-linux@<pkgver>.bbuild: builds the multiroot tooling and stages it
# plus the base FunTux configuration into an Arch package.
#
# Usage from a checkout of the repo:
#   FUNTUX_SRC=/path/to/funtux makepkg -f
#
# For a published release, instead set source= and sha256sums= to a tarball
# named funtux-linux-<pkgver>.tar.* (extraction places it in $srcdir and
# build() picks it up the same way the bbuild does).

pkgname=funtux-linux
pkgver=0.1.0
pkgrel=1
pkgdesc="FunTux Linux multiroot tooling (package-manager-less multiroot Linux)"
arch=('aarch64' 'x86_64')
url=""
license=('GPL-2.0-only')
depends=()
makedepends=('gcc' 'g++' 'make')
source=()
sha256sums=()

funtux_srcdir() {
    if [ -n "${FUNTUX_SRC}" ]; then
        printf '%s\n' "${FUNTUX_SRC}"
    else
        printf '%s\n' "${srcdir}/funtux-linux-${pkgver}"
    fi
}

build() {
    local src
    src="$(funtux_srcdir)"
    if [ ! -d "${src}" ]; then
        error "FunTux source not found at ${src}"
        error "set FUNTUX_SRC to a local checkout or add a source tarball"
        return 1
    fi
    cd "${src}" || return 1

    make -C srcfux/bin clean >/dev/null
    make -C srcfux/bin || return 1
    make -C srcfux/mroot clean >/dev/null
    make -C srcfux/mroot || return 1
    make -C srcfux/libfunobject clean >/dev/null
    make -C srcfux/libfunobject || return 1
}

package() {
    local src
    src="$(funtux_srcdir)"
    cd "${src}" || return 1

    install -d "${pkgdir}/sbin" "${pkgdir}/usr/lib" "${pkgdir}/usr/include/funobj" "${pkgdir}/etc" "${pkgdir}/usr/src"

    install -m 755 srcfux/bin/msubsys "${pkgdir}/sbin/msubsys"
    install -m 755 srcfux/bin/funroot "${pkgdir}/sbin/funroot"
    install -m 755 srcfux/mroot/mroot "${pkgdir}/sbin/mroot"
    install -m 755 srcfux/libfunobject/froot "${pkgdir}/sbin/froot"
    install -m 644 srcfux/libfunobject/libfunobj.a "${pkgdir}/usr/lib/libfunobj.a"
    install -m 755 srcfux/libfunobject/libfunobj.so "${pkgdir}/usr/lib/libfunobj.so"
    install -m 644 srcfux/libfunobject/funobj.h "${pkgdir}/usr/include/funobj/funobj.h"
    install -m 644 srcfux/libfunobject/funroot.h "${pkgdir}/usr/include/funobj/funroot.h"

    install -m 644 filesfux/* "${pkgdir}/etc/" 2>/dev/null || :
    install -m 644 srcfux/etc/* "${pkgdir}/etc/" 2>/dev/null || :

    cp -r srcfux/funroot "${pkgdir}/usr/src/funroot"
    find "${pkgdir}/usr/src/funroot" -type d -exec chmod 755 {} +
    find "${pkgdir}/usr/src/funroot" -type f -exec chmod 644 {} +
}
