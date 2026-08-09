# Copyright 2026 FunTux
# Distributed under the terms of the GNU General Public License v2

EAPI=8

DESCRIPTION="FunTux Linux multiroot tooling (package-manager-less multiroot Linux)"
HOMEPAGE=""
SRC_URI=""
LICENSE="GPL-2"
SLOT="0"
KEYWORDS="~aarch64 ~x86_64"
IUSE=""

DEPEND=""
RDEPEND="${DEPEND}"
BDEPEND="sys-devel/gcc sys-devel/make"

# Building mirrors funtux-linux@<PV>.bbuild.  Point FUNTUX_SRC at a local
# checkout, or set SRC_URI to a release tarball in ${DISTDIR}.

S="${WORKDIR}/funtux-linux-${PV}"

src_unpack() {
	if [ -n "${FUNTUX_SRC}" ]; then
		mkdir -p "${S}"
		cp -a "${FUNTUX_SRC}/." "${S}/" || die "failed to copy FUNTUX_SRC"
		chmod -R u+w "${S}"
	elif [ -f "${DISTDIR}/funtux-linux-${PV}.tar.xz" ]; then
		default
	else
		eerror "FunTux source not found."
		eerror "Export FUNTUX_SRC=/path/to/funtux, or set SRC_URI and place the"
		eerror "release tarball in ${DISTDIR}."
		die "no FunTux source tree"
	fi
}

src_compile() {
	make -C srcfux/bin clean >/dev/null
	make -C srcfux/bin || die "building srcfux/bin failed"
	make -C srcfux/mroot clean >/dev/null
	make -C srcfux/mroot || die "building srcfux/mroot failed"
	make -C srcfux/libfunobject clean >/dev/null
	make -C srcfux/libfunobject || die "building srcfux/libfunobject failed"
}

src_install() {
	exeinto /sbin
	doexe srcfux/bin/msubsys srcfux/bin/funroot
	doexe srcfux/mroot/mroot
	doexe srcfux/libfunobject/froot

	insinto /usr/lib
	doins srcfux/libfunobject/libfunobj.a
	dolib.so srcfux/libfunobject/libfunobj.so

	insinto /usr/include/funobj
	doins srcfux/libfunobject/funobj.h srcfux/libfunobject/funroot.h

	insinto /etc
	doins filesfux/* srcfux/etc/*

	insinto /usr/src/funroot
	doins -r srcfux/funroot/.
}
