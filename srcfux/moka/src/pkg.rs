//! Package model + installed-database (vdb) read/write.

use crate::util::{read_lines, write_file, R};
use std::fmt;
use std::path::Path;
use std::str::FromStr;

/// Atom: category/name-version (e.g. `sys-devel/gcc-12.2.0`).
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct Atom {
    pub category: String,
    pub name: String,
    pub version: String,
}

impl Atom {
    pub fn new(category: &str, name: &str, version: &str) -> Self {
        Self {
            category: category.to_string(),
            name: name.to_string(),
            version: version.to_string(),
        }
    }

    pub fn to_string(&self) -> String {
        format!("{}/{}-{}", self.category, self.name, self.version)
    }

    /// Match a raw atom string (`category/name[-version]` or `name[-version]`).
    pub fn matches(&self, raw: &str) -> bool {
        if let Some((cat, rest)) = raw.split_once('/') {
            if self.category != cat {
                return false;
            }
            self.matches_rest(rest)
        } else {
            self.matches_rest(raw)
        }
    }

    fn matches_rest(&self, rest: &str) -> bool {
        if let Some((n, v)) = rest.rsplit_once('-') {
            if n != self.name {
                return false;
            }
            return v.is_empty() || v == self.version;
        }
        self.name == rest || self.version == rest
    }
}

impl fmt::Display for Atom {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.to_string())
    }
}

impl FromStr for Atom {
    type Err = String;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let (category, rest) = s
            .split_once('/')
            .ok_or_else(|| format!("invalid atom `{}`: missing '/'", s))?;
        let (name, version) = rest
            .rsplit_once('-')
            .ok_or_else(|| format!("invalid atom `{}`: missing version", s))?;
        Ok(Atom::new(category, name, version))
    }
}

/// A recipe on disk (ebuild-like): metadata + phases.
#[derive(Clone, Debug)]
pub struct Package {
    pub atom: Atom,
    /// Build-phase shell fragments, keyed by phase name.
    pub phases: std::collections::HashMap<String, String>,
    /// Key=value metadata (DEPEND, SRC_URI, etc.).
    pub ebuild: std::collections::HashMap<String, String>,
    /// User-visible description.
    pub description: String,
}

impl Package {
    /// Parse `DEPEND`/`RDEPEND`/`BDEPEND` into plain atom strings.
    ///
    /// Only plain atoms are supported (`category/name[-version]` or
    /// `name[-version]`). Operators, use-flag groups, and slots are rejected
    /// loudly rather than silently ignored.
    pub fn deps(&self) -> R<Vec<String>> {
        let mut out: Vec<String> = Vec::new();
        for key in ["DEPEND", "RDEPEND", "BDEPEND"] {
            let Some(v) = self.ebuild.get(key) else { continue };
            for tok in v.split_whitespace() {
                let tok = tok.trim();
                if tok.is_empty() || tok.starts_with('#') {
                    continue;
                }
                if tok.starts_with('!')
                    || tok.starts_with('>')
                    || tok.starts_with('<')
                    || tok.starts_with('=')
                    || tok.starts_with('~')
                    || tok.contains('(')
                    || tok.contains(')')
                    || tok.contains('|')
                    || tok.contains(':')
                {
                    return Err(format!(
                        "unsupported DEPEND syntax `{}` in {} (plain atoms only: no operators, use-flags, or slots yet)",
                        tok, self.atom
                    ));
                }
                if self.atom.matches(tok) {
                    continue; // self-dependency is a no-op
                }
                if !out.iter().any(|o| o == tok) {
                    out.push(tok.to_string());
                }
            }
        }
        Ok(out)
    }

    pub fn load_from_dir(dir: &Path) -> R<Package> {
        let base = dir
            .file_name()
            .map(|s| s.to_string_lossy().into_owned())
            .ok_or_else(|| format!("bad dir {}", dir.display()))?;
        // base is `category/name-version` or `category/name`; find ebuild file.
        let atom = parse_atom_from_dir(dir, &base)?;
        let meta = read_lines(&dir.join("meta.ebuild"));
        let mut ebuild = std::collections::HashMap::new();
        let mut phases = std::collections::HashMap::new();
        let description;
        for line in meta {
            if line.contains('=') && !line.starts_with('#') {
                if let Some((k, v)) = line.split_once('=') {
                    ebuild.insert(k.trim().to_string(), v.trim().to_string());
                }
            }
            // inline phase block marker: `src_compile() { ... }`
        }
        // Load phase scripts.
        for (phase, fname) in [
            ("pkg_setup", "pkg_setup.sh"),
            ("src_prepare", "src_prepare.sh"),
            ("src_configure", "src_configure.sh"),
            ("src_compile", "src_compile.sh"),
            ("src_install", "src_install.sh"),
            ("pkg_postinst", "pkg_postinst.sh"),
        ] {
            let p = dir.join(fname);
            if let Ok(s) = std::fs::read_to_string(&p) {
                phases.insert(phase.to_string(), s);
            }
        }
        description = ebuild
            .get("DESCRIPTION")
            .cloned()
            .unwrap_or_default();
        Ok(Package {
            atom,
            phases,
            ebuild,
            description,
        })
    }
}

fn parse_atom_from_dir(dir: &Path, base: &str) -> R<Atom> {
    // Expected layout: repo/<category>/<name-version>/
    let cat = dir
        .parent()
        .and_then(|p| p.file_name())
        .map(|s| s.to_string_lossy().into_owned())
        .ok_or_else(|| format!("cannot derive category for {}", dir.display()))?;
    let (name, version) = base
        .rsplit_once('-')
        .ok_or_else(|| format!("cannot derive version for {}", dir.display()))?;
    Ok(Atom::new(&cat, name, version))
}

/// Installed package metadata (vdb).
#[derive(Clone, Debug)]
pub struct InstalledPkg {
    pub atom: Atom,
    /// Contents manifest: one relpath per line.
    pub files: Vec<String>,
    pub build_log: String,
}

/// List installed packages (vdb dirs).
pub fn installed_packages(vdb: &Path) -> Vec<InstalledPkg> {
    let mut out = Vec::new();
    for cat in crate::util::list_dir(vdb).unwrap_or_default() {
        if !cat.is_dir() {
            continue;
        }
        let cname = crate::util::basename(&cat);
        for pkg in crate::util::list_dir(&cat).unwrap_or_default() {
            if !pkg.is_dir() {
                continue;
            }
            let base = crate::util::basename(&pkg);
            if let Some((name, version)) = base.rsplit_once('-') {
                let atom = Atom::new(&cname, name, version);
                let files = read_lines(&pkg.join("contents"));
                let build_log = read_lines(&pkg.join("build.log")).join("\n");
                out.push(InstalledPkg {
                    atom,
                    files,
                    build_log,
                });
            }
        }
    }
    out
}

/// Write the vdb entry for a freshly installed package.
pub fn record_install(vdb: &Path, pkg: &InstalledPkg) -> R<()> {
    let dir = vdb
        .join(&pkg.atom.category)
        .join(format!("{}-{}", pkg.atom.name, pkg.atom.version));
    write_file(&dir.join("contents"), &pkg.files.join("\n"))?;
    write_file(&dir.join("build.log"), &pkg.build_log)?;
    write_file(&dir.join("info"), &pkg.atom.to_string())?;
    Ok(())
}
