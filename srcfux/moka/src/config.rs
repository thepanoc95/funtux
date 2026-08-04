//! Global configuration: filesystem layout, parallelism, feature defaults.

use crate::util::{read_lines, R};
use std::path::PathBuf;

#[derive(Clone, Debug)]
pub struct Config {
    /// Root of the FunTux system. Everything (repo, cache, vdb) hangs off this.
    pub root: PathBuf,
    /// Where portage-style ebuilds / recipes live (git tree).
    pub repo_dir: PathBuf,
    /// Where git repo is cloned/synced.
    pub repo_src: PathBuf,
    /// Build cache (sources, distfiles).
    pub cache_dir: PathBuf,
    /// Binary/vdb metadata for installed packages.
    pub vdb_dir: PathBuf,
    /// Temp dir for builds.
    pub tmp_dir: PathBuf,
    /// Number of parallel compile jobs.
    pub jobs: usize,
    /// Chroot root used during install (may equal root).
    pub chroot: PathBuf,
}

impl Default for Config {
    fn default() -> Self {
        Self::new()
    }
}

impl Config {
    pub fn new() -> Self {
        if let Ok(root) = std::env::var("FUNTUX_ROOT") {
            if !root.is_empty() {
                return Self::from_root(std::path::Path::new(&root));
            }
        }
        Self::from_root(std::path::Path::new("/"))
    }

    /// Config rooted at `root` (used for sandboxes/tests).
    pub fn from_root(root: &std::path::Path) -> Self {
        let repo_dir = root.join("var/lib/funtux/repo");
        Self {
            root: root.to_path_buf(),
            repo_dir: repo_dir.clone(),
            repo_src: root.join("var/lib/funtux/repo-src"),
            cache_dir: root.join("var/cache/funtux"),
            vdb_dir: root.join("var/db/funtux"),
            tmp_dir: root.join("var/tmp/funtux"),
            jobs: std::thread::available_parallelism()
                .map(|n| n.get())
                .unwrap_or(4),
            chroot: root.to_path_buf(),
        }
    }

    pub fn ensure_dirs(&self) -> R<()> {
        for d in [
            &self.repo_dir,
            &self.repo_src,
            &self.cache_dir,
            &self.vdb_dir,
            &self.tmp_dir,
        ] {
            crate::util::ensure_dir(d)?;
        }
        Ok(())
    }
}

/// Read `MAKE_OPTS`-style knobs from a config file if present.
pub fn load_make_conf(cfg: &Config) -> Vec<String> {
    let p = cfg.root.join("etc/funtux/make.conf");
    read_lines(&p)
        .into_iter()
        .filter(|l| l.starts_with("JOBS="))
        .flat_map(|l| {
            l.trim_start_matches("JOBS=")
                .trim()
                .parse::<usize>()
                .ok()
        })
        .map(|n| format!("-j{}", n))
        .collect()
}
