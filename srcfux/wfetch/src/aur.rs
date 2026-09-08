use std::collections::HashMap;
use std::fs;
use std::path::Path;
use std::process::{Command, ExitCode};

use crate::{Config, PackageMetadata, ExitCode_};
use crate::{extract_pkg_name, print_warning, get_user_confirmation, get_architecture, get_libc};

pub fn add_package_aur(config: &Config, url: &str, yes: bool) -> ExitCode {
    let (pkg_name, clone_url) = if url.contains("://") || url.contains("git@") {
        let name = extract_pkg_name(url);
        (name, url.to_string())
    } else {
        (url.to_string(), format!("https://aur.archlinux.org/{}.git", url))
    };

    if !yes {
        print_warning(&clone_url);
        match get_user_confirmation() {
            Some(true) => {},
            _ => {
                println!("Aborted.");
                return ExitCode_::GeneralError.exit_code();
            }
        }
    }

    let src_dir = config.src_dir.join(&pkg_name);
    let _ = fs::remove_dir_all(&src_dir);
    if let Err(e) = fs::create_dir_all(&src_dir) {
        eprintln!("error: failed to create source dir: {}", e);
        return ExitCode_::GeneralError.exit_code();
    }

    let result = Command::new("git")
        .arg("clone")
        .arg(&clone_url)
        .arg(&src_dir)
        .status();

    match result {
        Ok(status) if status.success() => {},
        _ => {
            eprintln!("error: failed to clone AUR repo: {}", clone_url);
            return ExitCode_::DownloadFailure.exit_code();
        }
    }

    let makepkg = Command::new("makepkg")
        .arg("--version")
        .output();
    if makepkg.map_or(false, |o| o.status.success()) {
        let build_result = Command::new("makepkg")
            .arg("-si")
            .current_dir(&src_dir)
            .env("SRC_DIR", &src_dir)
            .env("BUILD_DIR", &config.build_dir.join(&pkg_name))
            .env("PREFIX", &config.prefix)
            .env("WFETCH_ROOT", &config.cache_dir)
            .status();

        match build_result {
            Ok(status) if status.success() => {
                let pkg_db_dir = config.db_dir.join(&pkg_name);
                let _ = fs::create_dir_all(&pkg_db_dir);

                let metadata = PackageMetadata {
                    name: pkg_name.clone(),
                    version: get_pkgver_from_pkgbuild(&src_dir).unwrap_or_else(|| "unknown".to_string()),
                    source: clone_url,
                    architecture: get_architecture(),
                    libc: get_libc(),
                    depends: Vec::new(),
                    sha256sums: HashMap::new(),
                };

                if let Err(e) = metadata.save_to_dir(&pkg_db_dir) {
                    eprintln!("warning: failed to save metadata: {}", e);
                }

                println!("Successfully installed AUR package: {}", pkg_name);
                ExitCode_::Success.exit_code()
            }
            _ => {
                eprintln!("error: makepkg build failed for {}", pkg_name);
                ExitCode_::BuildFailure.exit_code()
            }
        }
    } else {
        eprintln!("error: makepkg is not installed. Please install the 'base-devel' group.");
        ExitCode_::BuildFailure.exit_code()
    }
}

fn get_pkgver_from_pkgbuild(dir: &Path) -> Option<String> {
    let pkgbuild = dir.join("PKGBUILD");
    let content = fs::read_to_string(&pkgbuild).ok()?;

    for line in content.lines() {
        let line = line.trim();
        if line.starts_with("pkgver=") {
            let value = line.split_once('=')
                .map(|(_, v)| v.trim().trim_matches('"').to_string())
                .unwrap_or_default();
            return Some(value);
        }
    }

    None
}
