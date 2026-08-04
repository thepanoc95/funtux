//! Dependency resolution: walk `DEPEND`/`RDEPEND`/`BDEPEND` across the recipe
//! tree and produce a build/install order (dependencies first).
//!
//! Already-installed packages (per the vdb) are pruned. Cycles are reported
//! with the offending chain rather than recursing forever.

use crate::config::Config;
use crate::pkg::{InstalledPkg, Package};
use crate::repo;
use crate::util::R;
use std::collections::HashMap;

/// Resolve `roots` plus all transitive dependencies into install order.
/// Returns an empty vec when everything is already installed.
pub fn resolve(cfg: &Config, roots: &[String]) -> R<Vec<Package>> {
    let installed = crate::pkg::installed_packages(&cfg.vdb_dir);
    let mut order: Vec<Package> = Vec::new();
    let mut colors: HashMap<String, u8> = HashMap::new();
    let mut stack: Vec<String> = Vec::new();
    for root in roots {
        walk(cfg, root, &installed, &mut order, &mut colors, &mut stack)?;
    }
    Ok(order)
}

/// DFS walk. `colors`: 0 = unvisited, 1 = on the current stack, 2 = done.
fn walk(
    cfg: &Config,
    atom: &str,
    installed: &[InstalledPkg],
    order: &mut Vec<Package>,
    colors: &mut HashMap<String, u8>,
    stack: &mut Vec<String>,
) -> R<()> {
    let pkg = repo::find_package(cfg, atom)?;
    let key = pkg.atom.to_string();
    match colors.get(&key) {
        Some(&1) => {
            let start = stack.iter().position(|s| *s == key).unwrap_or(0);
            let mut cycle = stack[start..].to_vec();
            cycle.push(key);
            return Err(format!("dependency cycle: {}", cycle.join(" -> ")));
        }
        Some(&2) => return Ok(()),
        _ => {}
    }
    if installed.iter().any(|ip| ip.atom.matches(&key)) {
        colors.insert(key, 2);
        return Ok(());
    }
    colors.insert(key.clone(), 1);
    stack.push(key.clone());
    for dep in pkg.deps()? {
        walk(cfg, &dep, installed, order, colors, stack)?;
    }
    stack.pop();
    colors.insert(key, 2);
    order.push(pkg);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::Config;

    /// Build a recipe tree from `cat/name-version -> deps` spec.
    /// `spec` maps `cat/name-version` to a list of dependency atom strings.
    fn make_repo(spec: &[(&str, &[&str])]) -> Config {
        let tmp = std::env::temp_dir().join(format!(
            "moka-resolve-{}-{}",
            std::process::id(),
            spec.len()
        ));
        let _ = std::fs::remove_dir_all(&tmp);
        let cfg = Config::from_root(&tmp);
        for (atom, deps) in spec {
            let (cat, rest) = atom.rsplit_once('/').unwrap();
            let dir = cfg.repo_dir.join(cat).join(rest);
            std::fs::create_dir_all(&dir).unwrap();
            let dep_line = if deps.is_empty() {
                String::new()
            } else {
                format!("DEPEND={}\n", deps.join(" "))
            };
            std::fs::write(
                dir.join("meta.ebuild"),
                format!("DESCRIPTION=test {}\n{}", atom, dep_line),
            )
            .unwrap();
            std::fs::write(
                dir.join("src_compile.sh"),
                "echo '#!/bin/sh\ntrue' > build-me\n",
            )
            .unwrap();
        }
        cfg
    }

    fn names(pkgs: &[Package]) -> Vec<String> {
        pkgs.iter().map(|p| p.atom.to_string()).collect()
    }

    #[test]
    fn resolves_transitive_order() {
        let cfg = make_repo(&[
            ("sys-devel/gcc-12.2.0", &[]),
            ("app-misc/hello-1.0", &["sys-devel/gcc-12.2.0", "app-libs/zlib-1.3"]),
            ("app-libs/zlib-1.3", &[]),
        ]);
        let plan = resolve(&cfg, &["app-misc/hello-1.0".to_string()]).unwrap();
        let got = names(&plan);
        // deps first, target last; exact tie order is unspecified
        assert_eq!(got.last().unwrap(), "app-misc/hello-1.0");
        assert!(got.iter().position(|n| n == "sys-devel/gcc-12.2.0").unwrap()
            < got.iter().position(|n| n == "app-misc/hello-1.0").unwrap());
        assert!(got.iter().position(|n| n == "app-libs/zlib-1.3").unwrap()
            < got.iter().position(|n| n == "app-misc/hello-1.0").unwrap());
    }

    #[test]
    fn detects_cycles() {
        let cfg = make_repo(&[
            ("a/a-1", &["b/b-1"]),
            ("b/b-1", &["c/c-1"]),
            ("c/c-1", &["a/a-1"]),
        ]);
        let err = resolve(&cfg, &["a/a-1".to_string()]).unwrap_err();
        assert!(err.contains("cycle"), "err = {}", err);
    }

    #[test]
    fn prunes_installed_packages() {
        let cfg = make_repo(&[
            ("sys-devel/gcc-12.2.0", &[]),
            ("app-misc/hello-1.0", &["sys-devel/gcc-12.2.0"]),
        ]);
        // mark gcc as installed
        let vdb = cfg.vdb_dir.join("sys-devel/gcc-12.2.0");
        std::fs::create_dir_all(&vdb).unwrap();
        std::fs::write(vdb.join("info"), "sys-devel/gcc-12.2.0").unwrap();
        std::fs::write(vdb.join("contents"), "").unwrap();
        std::fs::write(vdb.join("build.log"), "").unwrap();

        let plan = resolve(&cfg, &["app-misc/hello-1.0".to_string()]).unwrap();
        let got = names(&plan);
        assert_eq!(got, vec!["app-misc/hello-1.0"]);
    }

    #[test]
    fn bare_name_dep_matches_any_installed_version() {
        let cfg = make_repo(&[
            ("sys-devel/gcc-12.2.0", &[]),
            ("app-misc/hello-1.0", &["gcc"]),
        ]);
        let plan = resolve(&cfg, &["app-misc/hello-1.0".to_string()]).unwrap();
        // gcc resolved via bare name, then installed pruning would see it;
        // with nothing installed the full chain comes back
        let got = names(&plan);
        assert!(got.iter().any(|n| n == "sys-devel/gcc-12.2.0"));
        assert_eq!(got.last().unwrap(), "app-misc/hello-1.0");
    }

    #[test]
    fn rejects_operators_and_use_flags() {
        let cfg = make_repo(&[("app-misc/foo-1", &[">=app-libs/bar-1"])]);
        let err = resolve(&cfg, &["app-misc/foo-1".to_string()]).unwrap_err();
        assert!(err.contains("unsupported DEPEND"), "err = {}", err);

        let cfg = make_repo(&[("app-misc/baz-1", &["|| ( app-libs/a app-libs/b )"])]);
        let err = resolve(&cfg, &["app-misc/baz-1".to_string()]).unwrap_err();
        assert!(err.contains("unsupported DEPEND"), "err = {}", err);
    }

    #[test]
    fn ignores_self_dependency() {
        let cfg = make_repo(&[("app-misc/self-1", &["app-misc/self-1"])]);
        let plan = resolve(&cfg, &["app-misc/self-1".to_string()]).unwrap();
        assert_eq!(names(&plan), vec!["app-misc/self-1"]);
    }

    #[test]
    fn multi_root_resolution() {
        let cfg = make_repo(&[
            ("sys-devel/gcc-12.2.0", &[]),
            ("app-libs/zlib-1.3", &[]),
            ("a/a-1", &["sys-devel/gcc-12.2.0"]),
            ("b/b-1", &["app-libs/zlib-1.3"]),
        ]);
        let plan = resolve(&cfg, &["a/a-1".to_string(), "b/b-1".to_string()]).unwrap();
        let got = names(&plan);
        assert_eq!(got.len(), 4);
        assert!(got.iter().position(|n| n == "a/a-1").unwrap()
            > got.iter().position(|n| n == "sys-devel/gcc-12.2.0").unwrap());
        assert!(got.iter().position(|n| n == "b/b-1").unwrap()
            > got.iter().position(|n| n == "app-libs/zlib-1.3").unwrap());
    }
}
