# Maintainer: FunTux
#
# Mirrors funtux-linux@<pkgver>.bbuild.  Use from a checkout with
# FUNTUX_SRC=/path/to/funtux makepkg -f, or set source=/sha256sums= to a
# release tarball (build() picks it up from $srcdir).
#
# Build a base system with optional libc choice:
#   FUNTUX_LIBC=musl makepkg -f   # use musl (default)
#   FUNTUX_LIBC=minlibc makepkg -f # use minlibc

pkgname=funtux-linux
pkgver=0.1.2
pkgrel=1
pkgdesc="FunTux Linux base system (Rust coreutils, base tools, funtux utilities)"
arch=('aarch64' 'x86_64')
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

# Ensure git submodules are initialized
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

# Determine which libc to use
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

    # Build Rust coreutils (uutils) with the chosen libc
    # NOTE: The `expr` utility requires oniguruma which has linking issues with musl-static.
    # If you need expr, install oniguruma-dev and set OPENSSL_NO_VENDOR=1 or build differently.
    msg "Building coreutils (Rust) with ${libc}"
    if [ "${libc}" = "musl" ]; then
        export RUSTFLAGS="-C link-arg=-s -C link-arg=-Wl,--gc-sections"
        export CC_x86_64_unknown_linux_musl=musl-gcc
        export CC=musl-gcc
        # Build coreutils without expr (oniguruma dependency) by using specific binaries
        # Each utility can be built individually via --bin flag
        cargo build --release --target x86_64-unknown-linux-musl --manifest-path srcfux/coreutils/Cargo.toml \
            --bins --no-default-features \
            --features "feat_os_unix" \
            2>&1 || {
            msg "Coreutils build failed or incomplete. Building individual binaries..."
            # Build essential utilities individually
            for bin in ls cat rm cp mv ln mkdir rmdir pwd echo printf head tail env printenv date dd df du touch chmod install sort uniq split tee tr yes true false seq wc sum; do
                cargo build --release --target x86_64-unknown-linux-musl --manifest-path srcfux/coreutils/Cargo.toml \
                    --bin "${bin}" --no-default-features --features "feat_os_unix" 2>/dev/null || true
            done
        }
    else
        export RUSTFLAGS="-C panic=abort -C link-arg=-Wl,--gc-sections"
        export CC_x86_64_unknown_linux_musl=musl-gcc
        export CC=musl-gcc
        cargo build --release --target x86_64-unknown-linux-musl --manifest-path srcfux/coreutils/Cargo.toml \
            --no-default-features \
            --features "feat_os_unix" \
            2>&1 || {
            msg "Coreutils build failed or incomplete. Building individual binaries..."
            for bin in ls cat rm cp mv ln mkdir rmdir pwd echo printf head tail env printenv date dd df du touch chmod install sort uniq split tee tr yes true false seq wc sum; do
                cargo build --release --target x86_64-unknown-linux-musl --manifest-path srcfux/coreutils/Cargo.toml \
                    --bin "${bin}" --no-default-features --features "feat_os_unix" 2>/dev/null || true
            done
        }
    fi
    
    # Build additional Rust packages
    for pkg in awk bsdutils grep shadow tar util-linux; do
        if [ -f "srcfux/packages/${pkg}/Cargo.toml" ]; then
            msg "Building ${pkg} (Rust)"
            cargo build --release --target x86_64-unknown-linux-musl --manifest-path "srcfux/packages/${pkg}/Cargo.toml" \
                || return 1
        fi
    done
    
    # Build wfetch (uses musl target)
    msg "Building wfetch"
    cargo build --release --target x86_64-unknown-linux-musl --manifest-path srcfux/wfetch/Cargo.toml \
        || return 1
    
    # Build existing C/C++ tools
    msg "Building funtux C/C++ tools"
    make -C srcfux/bin clean >/dev/null 2>&1 || true
    make -C srcfux/bin || return 1
    make -C srcfux/mroot clean >/dev/null 2>&1 || true
    make -C srcfux/mroot || return 1
    make -C srcfux/libfunobject clean >/dev/null 2>&1 || true
    make -C srcfux/libfunobject || return 1
}

package() {
    local src libc
    src="$(funtux_srcdir)"
    libc="$(funtux_libc)"
    cd "${src}" || return 1

    # Create base directory structure
    install -d "${pkgdir}/sbin" \
             "${pkgdir}/bin" \
             "${pkgdir}/usr/bin" \
             "${pkgdir}/usr/lib" \
             "${pkgdir}/usr/include/funobj" \
             "${pkgdir}/etc" \
             "${pkgdir}/usr/src" \
             "${pkgdir}/usr/share/bash-completion/completions"

    # Install funtux C/C++ tools
    install -m 755 build/bin/msubsys "${pkgdir}/sbin/msubsys"
    install -m 755 build/bin/funroot "${pkgdir}/sbin/funroot"
    install -m 755 build/mroot/mroot "${pkgdir}/sbin/mroot"
    install -m 755 build/libfunobject/froot "${pkgdir}/sbin/froot"
    install -m 644 build/libfunobject/libfunobj.a "${pkgdir}/usr/lib/libfunobj.a"
    install -m 755 build/libfunobject/libfunobj.so "${pkgdir}/usr/lib/libfunobj.so"
    install -m 644 srcfux/libfunobject/funobj.h "${pkgdir}/usr/include/funobj/funobj.h"
    install -m 644 srcfux/libfunobject/funroot.h "${pkgdir}/usr/include/funobj/funroot.h"

    # Install Rust coreutils binaries (from musl target)
    local target_dir="x86_64-unknown-linux-musl"
    if [ "${libc}" != "musl" ]; then
        target_dir="x86_64-unknown-linux-gnu"
    fi
    
    install -d "${pkgdir}/usr/bin/coreutils"
    for bin in srcfux/coreutils/target/${target_dir}/release/*; do
        if [ -x "$bin" ] && [ -f "$bin" ]; then
            basename=$(basename "$bin")
            install -m 755 "$bin" "${pkgdir}/usr/bin/$basename"
        fi
    done

    # Install additional Rust utilities (from musl target)
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

    # Install wfetch (from musl target)
    install -m 755 srcfux/wfetch/target/${target_dir}/release/wfetch "${pkgdir}/usr/bin/wfetch"

    # Install configuration files
    install -m 644 filesfux/* "${pkgdir}/etc/" 2>/dev/null || :
    install -m 644 srcfux/etc/* "${pkgdir}/etc/" 2>/dev/null || :

    # Record the libc used
    echo "libc=${libc}" > "${pkgdir}/etc/funtux-libc.conf"

    # Include source for kernel modules
    cp -r srcfux/funroot "${pkgdir}/usr/src/funroot"
    find "${pkgdir}/usr/src/funroot" -type d -exec chmod 755 {} +
    find "${pkgdir}/usr/src/funroot" -type f -exec chmod 644 {} +
}
