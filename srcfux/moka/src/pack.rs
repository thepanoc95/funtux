//! Pack a built (staged) package into a pacman-compatible binary archive
//! (`.pkg.tar.zst`) so moka builds can be distributed and installed with
//! pacman on machines that do not want to compile from source.

use crate::config::Config;
use crate::pkg::Package;
use crate::util::{ensure_dir, rm_rf, sh_ok, sh_out, sq, write_file, R};
use std::path::{Path, PathBuf};

/// Produce `<name>-<ver>-1-<arch>.pkg.tar.zst` in `outdir` from a staged
/// image (the build's `$DESTDIR`). Returns the path to the archive.
pub fn pack(cfg: &Config, pkg: &Package, image: &Path, outdir: &Path) -> R<PathBuf> {
    ensure_dir(outdir)?;
    let arch = std::env::consts::ARCH;
    let fname = format!("{}-{}-1-{}.pkg.tar.zst", pkg.atom.name, pkg.atom.version, arch);
    let out = outdir.join(&fname);
    let tmp = cfg.tmp_dir.join("pack").join(&fname);
    rm_rf(&tmp)?;
    ensure_dir(&tmp)?;

    // Payload: hardlink the staged image into the pack dir (no data copy).
    sh_ok(&format!(
        "cp -al {}/. {}/",
        sq(&image.display().to_string()),
        sq(&tmp.display().to_string())
    ))?;

    write_pkginfo(pkg, &tmp, arch)?;
    gen_mtree(&tmp)?;

    sh_ok(&format!(
        "set -o pipefail; find {t} -mindepth 1 -printf '%P\\0' | LC_ALL=C sort -z | bsdtar -C {t} --no-fflags -cnf - --null --files-from - | zstd -q -f -19 -T0 -o {o}",
        t = sq(&tmp.display().to_string()),
        o = sq(&out.display().to_string()),
    ))?;
    rm_rf(&tmp)?;
    Ok(out)
}

/// Write a pacman `.PKGINFO` describing the package.
fn write_pkginfo(pkg: &Package, dir: &Path, arch: &str) -> R<()> {
    let name = &pkg.atom.name;
    let ver = &pkg.atom.version;
    let license = pkg
        .ebuild
        .get("LICENSE")
        .cloned()
        .unwrap_or_else(|| "UNKNOWN".to_string());
    let builddate = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    let size = installed_size(dir);
    let mut info = format!(
        "pkgname = {name}\n\
         pkgbase = {name}\n\
         pkgver = {ver}-1\n\
         pkgdesc = {desc}\n\
         url =\n\
         builddate = {builddate}\n\
         packager = FunTux\n\
         size = {size}\n\
         arch = {arch}\n\
         license = {license}\n",
        desc = pkg.description,
    );
    for dep in pkg.deps()? {
        info.push_str(&format!("depend = {}\n", dep_name(&dep)));
    }
    write_file(&dir.join(".PKGINFO"), &info)
}

/// Reduce a dep atom string to a bare pacman package name.
fn dep_name(dep: &str) -> String {
    let name = dep.rsplit('/').next().unwrap_or(dep);
    name.rsplit_once('-')
        .map(|(n, _)| n.to_string())
        .unwrap_or_else(|| name.to_string())
}

/// Generate a `.MTREE` manifest over the pack dir, exactly like makepkg does.
fn gen_mtree(dir: &Path) -> R<()> {
    let list = sh_out(&format!(
        "find {d} -mindepth 1 -printf '%P\\n' | LC_ALL=C sort",
        d = sq(&dir.display().to_string())
    ))?;
    let mut targets: Vec<String> = Vec::new();
    for line in list.lines() {
        if !line.is_empty() {
            targets.push(sq(line));
        }
    }
    let opts = "'!all,use-set,type,uid,gid,mode,time,size,md5,sha256,sha512'";
    sh_ok(&format!(
        "bsdtar -C {d} -cf {o} --format=mtree --options={opts} {t}",
        d = sq(&dir.display().to_string()),
        o = sq(&dir.join(".MTREE").display().to_string()),
        t = targets.join(" "),
    ))
}

/// Sum the size of regular files under `dir` (installed size).
fn installed_size(dir: &Path) -> u64 {
    let mut total = 0u64;
    if let Ok(entries) = std::fs::read_dir(dir) {
        for e in entries.flatten() {
            let p = e.path();
            if let Ok(meta) = std::fs::symlink_metadata(&p) {
                if meta.is_dir() {
                    total += installed_size(&p);
                } else if !meta.file_type().is_symlink() {
                    total += meta.len();
                }
            }
        }
    }
    total
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn dep_name_strips_category_and_version() {
        assert_eq!(dep_name("sys-devel/gcc-12.2.0"), "gcc");
        assert_eq!(dep_name("gcc"), "gcc");
        assert_eq!(dep_name("app-libs/zlib"), "zlib");
    }
}
