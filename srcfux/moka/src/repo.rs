//! Repository handling: sync the recipe tree from git.

use crate::config::Config;
use crate::pkg::Package;
use crate::util::{list_dir, rm_rf, sh_run, R, sq};

/// Check that the recipe tree is present; if not, clone the repo.
pub fn ensure_repo(cfg: &Config) -> R<()> {
    if cfg.repo_dir.join(".git").is_dir() {
        return Ok(());
    }
    // Currently the repo is expected to be prepared by the system admin;
    // provide a helpful message instead of guessing a URL.
    Err(format!(
        "no recipe tree at {} (run `moka repo-setup <git-url>` first)",
        cfg.repo_dir.display()
    ))
}

/// Clone/fetch a git recipe tree.
pub fn repo_setup(cfg: &Config, url: &str) -> R<()> {
    if cfg.repo_dir.join(".git").is_dir() {
        return Err(format!("repo already present at {}", cfg.repo_dir.display()));
    }
    // Clone into a temp sibling, then move into place atomically.
    let staging = cfg.repo_src.join("repo-clone");
    rm_rf(&staging)?;
    crate::util::ensure_dir(&cfg.repo_src)?;
    let code = sh_run(&format!(
        "git clone {} {}",
        sq(url),
        sq(&staging.display().to_string())
    ))?;
    if code != 0 {
        return Err(format!("git clone failed (exit {})", code));
    }
    if cfg.repo_dir.exists() {
        rm_rf(&cfg.repo_dir)?;
    }
    std::fs::rename(&staging, &cfg.repo_dir)
        .map_err(|e| format!("failed to move repo: {}", e))?;
    Ok(())
}

/// List every package available in the tree.
pub fn list_packages(cfg: &Config) -> Vec<Package> {
    let mut out = Vec::new();
    for cat in list_dir(&cfg.repo_dir).unwrap_or_default() {
        if !cat.is_dir() || cat.join(".git").is_dir() {
            continue;
        }
        for entry in list_dir(&cat).unwrap_or_default() {
            if !entry.is_dir() || !entry.join("meta.ebuild").is_file() {
                continue;
            }
            if let Ok(p) = Package::load_from_dir(&entry) {
                out.push(p);
            }
        }
    }
    out
}

/// Find a single package by atom-ish query.
pub fn find_package(cfg: &Config, query: &str) -> Result<Package, String> {
    let pkgs = list_packages(cfg);
    let mut matches: Vec<Package> = pkgs
        .into_iter()
        .filter(|p| p.atom.matches(query))
        .collect();
    if matches.is_empty() {
        return Err(format!("no package matches `{}`", query));
    }
    if matches.len() > 1 {
        let names: Vec<String> = matches.iter().map(|m| m.atom.to_string()).collect();
        return Err(format!("ambiguous match for `{}`: {}", query, names.join(", ")));
    }
    Ok(matches.remove(0))
}

/// Search the repo by name/description keyword.
pub fn search_packages(cfg: &Config, keyword: &str) -> Vec<Package> {
    list_packages(cfg)
        .into_iter()
        .filter(|p| {
            p.atom.name.contains(keyword)
                || p.description.to_lowercase().contains(&keyword.to_lowercase())
        })
        .collect()
}
