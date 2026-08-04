//! Build orchestration: chroot-aware compile + install, file manifest tracking.

use crate::config::Config;
use crate::pkg::Package;
use crate::util::{ensure_dir, list_dir, rm_rf, run_phase, R};
use std::path::{Path, PathBuf};

/// Root that builds happen in. Currently chroot == root; kept as a seam so a
/// true chroot can be dropped in later.
pub struct BuildCtx<'a> {
    pub cfg: &'a Config,
    pub pkg: &'a Package,
    pub work: PathBuf,
    pub image: PathBuf, // staging dir for DESTDIR
    pub log: String,
}

impl<'a> BuildCtx<'a> {
    pub fn new(cfg: &'a Config, pkg: &'a Package) -> R<Self> {
        let work = cfg
            .tmp_dir
            .join("work")
            .join(format!("{}-{}", pkg.atom.category, pkg.atom.name));
        let image = cfg.tmp_dir.join("image").join(pkg.atom.to_string());
        rm_rf(&work)?;
        rm_rf(&image)?;
        ensure_dir(&work)?;
        ensure_dir(&image)?;
        Ok(Self {
            cfg,
            pkg,
            work,
            image,
            log: String::new(),
        })
    }

    pub fn base_env(&self) -> Vec<(String, String)> {
        let jobs = format!("-j{}", self.cfg.jobs);
        let mut make_opts = vec![jobs.clone()];
        make_opts.extend(crate::config::load_make_conf(self.cfg));
        vec![
            ("MAKE_OPTS".into(), make_opts.join(" ")),
            ("DESTDIR".into(), self.image.display().to_string()),
            ("PREFIX".into(), "/usr".into()),
            ("CC".into(), "cc".into()),
            ("CXX".into(), "c++".into()),
            ("CFLAGS".into(), "-O2 -pipe".into()),
            ("LDFLAGS".into(), String::new()),
            ("FUNTUX_CHROOT".into(), self.cfg.chroot.display().to_string()),
            ("S".into(), self.work.join("S").display().to_string()),
            ("WORKDIR".into(), self.work.display().to_string()),
        ]
    }

    /// Run a phase in the workdir with the base env set.
    fn phase(&mut self, name: &str) -> R<()> {
        let script = self.pkg.phases.get(name).cloned().unwrap_or_default();
        if script.trim().is_empty() {
            return Ok(());
        }
        let env = self.base_env();
        self.log
            .push_str(&format!("### phase {}\n{}\n", name, script));
        run_phase(name, &script, &env, &self.work)
    }

    /// Perform a full build: fetch source -> prepare -> configure -> compile -> install.
    pub fn build(&mut self) -> R<()> {
        self.fetch_source()?;
        self.phase("src_prepare")?;
        self.phase("src_configure")?;
        self.phase("src_compile")?;
        self.phase("src_install")?;
        Ok(())
    }

    /// Populate $S. If the recipe declares a git `SRC_URI`, clone it there;
    /// otherwise $S is left empty for `src_prepare` to fill.
    fn fetch_source(&self) -> R<()> {
        let sdir = self.work.join("S");
        match self.pkg.ebuild.get("SRC_URI") {
            None => ensure_dir(&sdir),
            Some(uri) => {
                println!("fetching source: {}", uri);
                rm_rf(&sdir)?;
                ensure_dir(&sdir)?;
                let code = crate::util::sh_run(&format!(
                    "git clone {} {}",
                    crate::util::sq(uri),
                    crate::util::sq(&sdir.display().to_string())
                ))?;
                if code != 0 {
                    return Err(format!("failed to clone SRC_URI `{}`", uri));
                }
                Ok(())
            }
        }
    }

    /// Copy staged files into the live root, returning installed relpaths.
    pub fn install_into_root(&self) -> R<Vec<String>> {
        let mut installed = Vec::new();
        copy_tree(&self.image, &self.cfg.root, &mut installed)?;
        Ok(installed)
    }
}

fn copy_tree(src: &Path, dst_root: &Path, installed: &mut Vec<String>) -> R<()> {
    copy_tree_rec(src, src, dst_root, installed)
}

fn copy_tree_rec(dir: &Path, src_root: &Path, dst_root: &Path, installed: &mut Vec<String>) -> R<()> {
    for e in list_dir(dir)? {
        let rel = e
            .strip_prefix(src_root)
            .map(|p| p.to_string_lossy().into_owned())
            .map_err(|err| err.to_string())?;
        let target = dst_root.join(&rel);
        let meta = std::fs::symlink_metadata(&e).map_err(|err| err.to_string())?;
        if meta.file_type().is_symlink() {
            let link = std::fs::read_link(&e).map_err(|err| err.to_string())?;
            if let Some(p) = target.parent() {
                ensure_dir(p)?;
            }
            let _ = std::fs::remove_file(&target);
            std::os::unix::fs::symlink(&link, &target)
                .map_err(|err| format!("symlink {}: {}", target.display(), err))?;
            installed.push(rel);
        } else if meta.is_dir() {
            ensure_dir(&target)?;
            copy_tree_rec(&e, src_root, dst_root, installed)?;
        } else {
            if let Some(p) = target.parent() {
                ensure_dir(p)?;
            }
            std::fs::copy(&e, &target)
                .map_err(|err| format!("copy {}: {}", target.display(), err))?;
            installed.push(rel);
        }
    }
    Ok(())
}
