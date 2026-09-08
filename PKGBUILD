# FUNTUX_LIBC=musl makepkg -f   # use musl (recommended)
# FUNTUX_LIBC=minlibc makepkg -f # use minlibc

pkgname=funtux-linux
pkgver=0.1.0
pkgrel=1
pkgdesc="FunTux Linux base system"
arch=(any)
url=""
license=('GPL-2.0-only')
backup=(
    'etc/fstab'
    'etc/gettytab'
    'etc/group'
    'etc/hostname'
    'etc/hosts'
    'etc/issue'
    'etc/ld.so.conf'
    'etc/locale.sh'
    'etc/motd'
    'etc/os-release'
    'etc/passwd'
    'etc/profile'
    'etc/shells'
    'etc/sysctl'
    'etc/sysusers'
    'etc/tmpfiles'
)
install=funtux-linux.install
# Choose libc: FUNTUX_LIBC=musl (default) or FUNTUX_LIBC=minlibc
depends=()
makedepends=('gcc' 'make' 'git' 'rust' 'cargo' 'pkgconf' 'zlib')
source=()
sha256sums=()

_git_submodule_update() {
    if [ -d "${1}/.git" ] && [ -f "${1}/.gitmodules" ]; then
        msg "Initializing git submodules in ${1}"
        (
            cd "${1}" || return 1
            git submodule update --init --recursive || return 1
        ) || return 1
    fi
}

funtux_srcdir() {
    if [ -n "${FUNTUX_SRC}" ]; then
        printf '%s\n' "${FUNTUX_SRC}"
    else
        printf '%s\n' "${startdir}"
    fi
}

funtux_libc() {
    echo "${FUNTUX_LIBC:-musl}"
}

build() {
    local src libc
    src="$(funtux_srcdir)"
    libc="$(funtux_libc)"
    
    if [ ! -d "${src}" ]; then
        error "FunTux source not found at ${src}"
        error "set FUNTUX_SRC to a local checkout or add a source tarball"
        return 1
    fi
    cd "${src}" || return 1

    # Initialize git submodules
    _git_submodule_update "${src}" || return 1

    # Save makepkg's CFLAGS/CXXFLAGS and unset them for Rust builds.
    # The makepkg CFLAGS include -flto=auto which breaks onig_sys/
    # oniguruma compilation under the cc crate for musl targets (LTO
    # objects from system gcc are incompatible with the rustc
    # self-contained linker). The cc crate resolves its own flags for
    # the target triple via CC_x86_64_unknown_linux_musl.
    _pkg_CFLAGS="${CFLAGS}"
    _pkg_CXXFLAGS="${CXXFLAGS}"
    unset CFLAGS CXXFLAGS

    msg "Building coreutils"
    if [ "${libc}" = "musl" ]; then
        export RUSTFLAGS="-C link-arg=-s -C link-arg=-Wl,--gc-sections"
        export CC_x86_64_unknown_linux_musl=musl-gcc
        export CC=musl-gcc
        cargo build --release --target x86_64-unknown-linux-musl --manifest-path srcfux/coreutils/Cargo.toml \
            --no-default-features \
            --features "ls,cat,rm,cp,mv,ln,mkdir,rmdir,pwd,echo,printf,head,tail,env,printenv,date,dd,df,du,touch,chmod,install,sort,uniq,split,tee,tr,yes,true,false,seq,wc,sum,whoami,nproc" \
            || return 1
    else
        export RUSTFLAGS="-C panic=abort -C link-arg=-Wl,--gc-sections"
        export CC_x86_64_unknown_linux_musl=musl-gcc
        export CC=musl-gcc
        cargo build --release --target x86_64-unknown-linux-musl --manifest-path srcfux/coreutils/Cargo.toml \
            --no-default-features \
            --features "ls,cat,rm,cp,mv,ln,mkdir,rmdir,pwd,echo,printf,head,tail,env,printenv,date,dd,df,du,touch,chmod,install,sort,uniq,split,tee,tr,yes,true,false,seq,wc,sum,whoami,nproc" \
            || return 1
    fi

    for pkg in awk bsdutils grep shadow tar util-linux; do
        if [ -f "srcfux/packages/${pkg}/Cargo.toml" ]; then
            msg "Building ${pkg} (Rust)"
            if [ "${pkg}" = "util-linux" ]; then
                cargo build --release --target x86_64-unknown-linux-musl --manifest-path "srcfux/packages/${pkg}/Cargo.toml" \
                    --no-default-features \
                    --features "blockdev,cal,chcpu,ctrlaltdel,dmesg,fsfreeze,hexdump,last,lscpu,lsmem,mcookie,mesg,mountpoint,nologin,renice,rev,setpgid,setsid,uuidgen" \
                    || return 1
            else
                cargo build --release --target x86_64-unknown-linux-musl --manifest-path "srcfux/packages/${pkg}/Cargo.toml" \
                    || return 1
            fi
        fi
    done
    
    msg "Building wfetch"
    cargo build --release --target x86_64-unknown-linux-musl --manifest-path srcfux/wfetch/Cargo.toml \
        || return 1

    msg "Building funtux-utils"
    export CFLAGS="${_pkg_CFLAGS}" CXXFLAGS="${_pkg_CXXFLAGS}"
    make -C srcfux/bin clean >/dev/null 2>&1 || true
    make -C srcfux/bin || return 1
    make -C srcfux/mroot clean >/dev/null 2>&1 || true
    make -C srcfux/mroot || return 1
    make -C srcfux/libfunobject clean >/dev/null 2>&1 || true
    make -C srcfux/libfunobject || return 1

    msg "Building musl"
    ( cd srcfux/lib/musl && CC=gcc ./configure --prefix=/usr --syslibdir=/lib && make -j$(nproc) ) || return 1

    msg "Building dash"
    ( cd srcfux/packages/dash && CC=musl-gcc ./configure --prefix=/usr && make -j$(nproc) ) || return 1
}

package() {
    local src libc
    src="$(funtux_srcdir)"
    libc="$(funtux_libc)"
    cd "${src}" || return 1

    install -d "${pkgdir}/sbin" \
             "${pkgdir}/bin" \
             "${pkgdir}/usr/bin" \
             "${pkgdir}/usr/lib" \
             "${pkgdir}/usr/include/funobj" \
             "${pkgdir}/etc" \
             "${pkgdir}/usr/src" \
             "${pkgdir}/usr/share/bash-completion/completions"

    install -m 755 build/bin/msubsys "${pkgdir}/sbin/msubsys"
    install -m 755 build/bin/funroot "${pkgdir}/sbin/funroot"
    install -m 755 build/mroot/mroot "${pkgdir}/sbin/mroot"
    install -m 755 build/libfunobject/froot "${pkgdir}/sbin/froot"
    install -m 644 build/libfunobject/libfunobj.a "${pkgdir}/usr/lib/libfunobj.a"
    install -m 755 build/libfunobject/libfunobj.so "${pkgdir}/usr/lib/libfunobj.so"
    install -m 644 srcfux/libfunobject/funobj.h "${pkgdir}/usr/include/funobj/funobj.h"
    install -m 644 srcfux/libfunobject/funroot.h "${pkgdir}/usr/include/funobj/funroot.h"

    local target_dir="x86_64-unknown-linux-musl"
    if [ "${libc}" != "musl" ]; then
        target_dir="x86_64-unknown-linux-gnu"
    fi
    
    install -d "${pkgdir}/usr/bin"
    install -m 755 "srcfux/coreutils/target/${target_dir}/release/coreutils" "${pkgdir}/usr/bin/coreutils"
    local coreutils_cmds="ls cat rm cp mv ln mkdir rmdir pwd echo printf head tail env printenv date dd df du touch chmod install sort uniq split tee tr yes true false seq wc sum whoami nproc"
    for cmd in ${coreutils_cmds}; do
        ln -sf coreutils "${pkgdir}/usr/bin/${cmd}"
    done

    for pkg in awk bsdutils grep shadow tar util-linux; do
        pkgdir_src="${src}/srcfux/packages/${pkg}"
        if [ -f "${pkgdir_src}/Cargo.toml" ] && [ -d "${pkgdir_src}/target/${target_dir}/release" ]; then
            for bin in "${pkgdir_src}/target/${target_dir}/release/"*; do
                if [ -x "$bin" ] && [ -f "$bin" ]; then
                    basename_bin=$(basename "$bin")
                    install -m 755 "$bin" "${pkgdir}/usr/bin/$basename_bin"
                fi
            done
        fi
    done

    install -m 755 srcfux/wfetch/target/${target_dir}/release/wfetch "${pkgdir}/usr/bin/wfetch"

    msg "Installing musl"
    make -C srcfux/lib/musl install DESTDIR="${pkgdir}"

    install -d "${pkgdir}/bin" "${pkgdir}/usr/bin"
    install -m 755 srcfux/packages/dash/src/dash "${pkgdir}/bin/dash"
    ln -sf dash "${pkgdir}/bin/bash"
    ln -sf dash "${pkgdir}/bin/sh"
    ln -sf ../bin/dash "${pkgdir}/usr/bin/bash"
    ln -sf ../bin/dash "${pkgdir}/usr/bin/sh"

    install -m 644 filesfux/* "${pkgdir}/etc/" 2>/dev/null || :
    install -m 644 srcfux/etc/* "${pkgdir}/etc/" 2>/dev/null || :

    echo "libc=${libc}" > "${pkgdir}/etc/funtux-libc.conf"

    cp -r srcfux/funroot "${pkgdir}/usr/src/funroot"
    find "${pkgdir}/usr/src/funroot" -type d -exec chmod 755 {} +
    find "${pkgdir}/usr/src/funroot" -type f -exec chmod 644 {} +
}
