//! Recipe generator: record an interactive build session into a recipe.

use crate::config::Config;
use crate::util::{ensure_dir, sh_ok, write_file, R, sq};
use std::io::IsTerminal;
use std::path::{Path, PathBuf};
use std::process::Command;

const KNOWN_PHASES: &[&str] = &[
    "pkg_setup",
    "src_prepare",
    "src_configure",
    "src_compile",
    "src_install",
    "pkg_postinst",
];

pub fn recipe_gen(cfg: &Config, url: &str, version: &str) -> R<()> {
    let name = repo_name(url);
    let base = cfg.tmp_dir.join("recipe").join(format!("{}-{}", name, std::process::id()));
    ensure_dir(&base)?;
    let src = base.join("S");
    let record = base.join("record.log");
    let rcfile = base.join("session.rc");

    println!("cloning {} ...", url);
    sh_ok(&format!("git clone {} {}", sq(url), sq(&src.display().to_string())))?;

    let image = cfg.tmp_dir.join("image").join(&name);
    ensure_dir(&image)?;

    std::fs::write(&rcfile, rc_script(&name))
        .map_err(|e| format!("{}: {}", rcfile.display(), e))?;

    if !std::io::stdin().is_terminal() {
        eprintln!("warning: stdin is not a terminal; the session will not be interactive");
    }
    println!("\n=== FunTux recipe session ===");
    println!("You are now in the cloned source of {}. Build it as you normally would.", name);
    println!("Every command you type is recorded into a recipe.");
    println!("Optional: mark phases with `ftx-phase src_configure` (src_compile / src_install)");
    println!("Type `exit` when you are done.\n");

    let status = Command::new("bash")
        .arg("--rcfile")
        .arg(&rcfile)
        .arg("-i")
        .env("FTX_RECORD", &record)
        .env("FTX_S", &src)
        .env("DESTDIR", &image)
        .env("PREFIX", "/usr")
        .env("MAKEFLAGS", format!("-j{}", cfg.jobs))
        .env("CC", "cc")
        .env("CXX", "c++")
        .current_dir(&src)
        .stdin(std::process::Stdio::inherit())
        .stdout(std::process::Stdio::inherit())
        .stderr(std::process::Stdio::inherit())
        .status()
        .map_err(|e| format!("failed to run interactive session: {}", e))?;
    if !status.success() {
        // exit status reflects the last command, but the recording is valid
        eprintln!(
            "warning: session exited with status {}; using recorded commands",
            status
        );
    }

    let records = parse_record(&record)?;
    if records.is_empty() {
        return Err("no commands were recorded; nothing to generate".to_string());
    }
    let phases = split_phases(records);
    let out = std::env::current_dir().map_err(|e| format!("cwd: {}", e))?;
    let dir = write_recipe(&name, version, url, &phases, &out)?;
    println!("\nrecipe written to {}/", dir.display());
    println!("review it, then move it into {}/<category>/", cfg.repo_dir.display());
    Ok(())
}

/// Derive a directory name from a git URL.
pub fn repo_name(url: &str) -> String {
    let u = url.trim_end_matches('/');
    let base = u.rsplit('/').next().unwrap_or("");
    let base = base.strip_suffix(".git").unwrap_or(base);
    if base.is_empty() {
        "repo".to_string()
    } else {
        base.to_string()
    }
}

// shell rc that records every typed command + ftx-phase markers
fn rc_script(name: &str) -> String {
    let ps1 = format!("PS1='\\u@funtux:{name}:\\w\\$ '");
    let banner = format!("echo \"You are in: $PWD (source of {name})\"");
    let lines = vec![
        "# FunTux recipe session rc",
        "[ -f \"$HOME/.bashrc\" ] && . \"$HOME/.bashrc\"",
        "unset PROMPT_COMMAND",
        "cd \"$FTX_S\"",
        &ps1,
        "ftx-phase() {",
        "    trap - DEBUG",
        "    printf \"###PHASE %s\\n\" \"$1\" >> \"$FTX_RECORD\"",
        "    trap 'printf \"%s\\n\" \"$BASH_COMMAND\" >> \"$FTX_RECORD\"' DEBUG",
        "}",
        "echo ''",
        "echo '=== FunTux recipe session ==='",
        &banner,
        "echo 'Every command you type is recorded. Type exit when done.'",
        "echo 'Optional markers: ftx-phase src_configure / src_compile / src_install'",
        "echo '======================================'",
        // trap last so the banner is not recorded
        "trap 'printf \"%s\\n\" \"$BASH_COMMAND\" >> \"$FTX_RECORD\"' DEBUG",
    ];
    lines.join("\n")
}

/// Parse the recorded log into `(phase_marker, command)` pairs.
fn parse_record(record: &Path) -> R<Vec<(String, String)>> {
    let content = std::fs::read_to_string(record)
        .map_err(|e| format!("{}: {}", record.display(), e))?;
    let mut out = Vec::new();
    let mut marker = String::new();
    for line in content.lines() {
        let t = line.trim();
        if t.is_empty() {
            continue;
        }
        if let Some(m) = t.strip_prefix("###PHASE ") {
            marker = m.trim().to_string();
            continue;
        }
        // skip recorded noise: ftx-phase calls, `exit`, terminal title sets
        if t.starts_with("ftx-phase") || t == "exit" || t.contains("\\033]0;") {
            continue;
        }
        out.push((marker.clone(), t.to_string()));
    }
    Ok(out)
}

// group commands into phases, preferring explicit markers
fn split_phases(records: Vec<(String, String)>) -> Vec<(String, Vec<String>)> {
    let has_markers = records.iter().any(|(m, _)| !m.is_empty());
    if !has_markers {
        return heuristic_split(records);
    }
    let mut phases: Vec<(String, Vec<String>)> = Vec::new();
    for (m, cmd) in records {
        let m = if m.is_empty() || KNOWN_PHASES.contains(&m.as_str()) {
            if m.is_empty() {
                "src_compile".to_string()
            } else {
                m
            }
        } else {
            eprintln!("warning: unknown phase `{}` ignored (command kept)", m);
            "src_compile".to_string()
        };
        match phases.last_mut() {
            Some((cur, cmds)) if *cur == m => cmds.push(cmd),
            _ => phases.push((m, vec![cmd])),
        }
    }
    phases
}

fn heuristic_split(records: Vec<(String, String)>) -> Vec<(String, Vec<String>)> {
    let is_configure = |c: &str| {
        c.contains("./configure") || c.starts_with("cmake") || c.contains("meson setup")
    };
    let is_install = |c: &str| {
        c.contains("make install") || c.contains("ninja install") || c.starts_with("install ")
    };
    let is_prepare = |c: &str| {
        c.starts_with("cd ")
            || c.starts_with("patch")
            || c.starts_with("sed")
            || c.starts_with("autoreconf")
            || c.starts_with("autogen")
            || c.starts_with("./bootstrap")
    };
    let mut prepare = Vec::new();
    let mut configure = Vec::new();
    let mut compile = Vec::new();
    let mut install = Vec::new();
    let mut seen_configure = false;
    let mut seen_install = false;
    for (_, c) in records {
        if seen_install {
            install.push(c);
            continue;
        }
        if is_install(&c) {
            seen_install = true;
            install.push(c);
            continue;
        }
        if !seen_configure {
            if is_configure(&c) {
                seen_configure = true;
                configure.push(c);
                continue;
            }
            if is_prepare(&c) {
                prepare.push(c);
                continue;
            }
            compile.push(c);
            continue;
        }
        compile.push(c);
    }
    let mut out = Vec::new();
    if !prepare.is_empty() {
        out.push(("src_prepare".to_string(), prepare));
    }
    if !configure.is_empty() {
        out.push(("src_configure".to_string(), configure));
    }
    if !compile.is_empty() {
        out.push(("src_compile".to_string(), compile));
    }
    if !install.is_empty() {
        out.push(("src_install".to_string(), install));
    }
    out
}

fn write_recipe(
    name: &str,
    version: &str,
    url: &str,
    phases: &[(String, Vec<String>)],
    out: &Path,
) -> R<PathBuf> {
    let dir = out.join(format!("{}-{}", name, version));
    ensure_dir(&dir)?;
    let meta = format!(
        "DESCRIPTION={} (generated by moka recipe-gen - edit me)\nSRC_URI={}\nLICENSE=UNKNOWN\n",
        name, url
    );
    write_file(&dir.join("meta.ebuild"), &meta)?;
    for (phase, cmds) in phases {
        // recorded commands assume the source root; each phase starts there
        let mut script = String::from("cd \"$S\"\n");
        for c in cmds {
            script.push_str(c);
            script.push('\n');
        }
        write_file(&dir.join(format!("{}.sh", phase)), &script)?;
    }
    Ok(dir)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn repo_name_strips_suffixes() {
        assert_eq!(repo_name("https://github.com/a/b.git"), "b");
        assert_eq!(repo_name("https://github.com/a/b/"), "b");
        assert_eq!(repo_name("git@host:user/proj.git"), "proj");
        assert_eq!(repo_name("https://host/x"), "x");
    }

    #[test]
    fn markers_group_commands() {
        let rec = vec![
            ("".to_string(), "echo hi".to_string()),
            ("src_configure".to_string(), "./configure".to_string()),
            ("src_compile".to_string(), "make".to_string()),
            ("src_compile".to_string(), "make test".to_string()),
            ("src_install".to_string(), "make install DESTDIR=\"$DESTDIR\"".to_string()),
        ];
        let phases = split_phases(rec);
        assert_eq!(phases[0], ("src_compile".to_string(), vec!["echo hi".to_string()]));
        assert_eq!(phases[1], ("src_configure".to_string(), vec!["./configure".to_string()]));
        assert_eq!(
            phases[2],
            (
                "src_compile".to_string(),
                vec!["make".to_string(), "make test".to_string()]
            )
        );
        assert_eq!(
            phases[3],
            (
                "src_install".to_string(),
                vec!["make install DESTDIR=\"$DESTDIR\"".to_string()]
            )
        );
    }

    #[test]
    fn heuristic_splits_typical_build() {
        let rec = vec![
            ("".to_string(), "cd src".to_string()),
            ("".to_string(), "patch -p1 < fix.patch".to_string()),
            ("".to_string(), "./configure --prefix=/usr".to_string()),
            ("".to_string(), "make -j4".to_string()),
            ("".to_string(), "make install DESTDIR=\"$DESTDIR\"".to_string()),
        ];
        let phases = split_phases(rec);
        let names: Vec<_> = phases.iter().map(|(n, _)| n.as_str()).collect();
        assert_eq!(names, vec!["src_prepare", "src_configure", "src_compile", "src_install"]);
        assert_eq!(phases[0].1, vec!["cd src".to_string(), "patch -p1 < fix.patch".to_string()]);
        assert_eq!(phases[3].1.len(), 1);
    }

    #[test]
    fn heuristic_no_configure() {
        let rec = vec![
            ("".to_string(), "make".to_string()),
            ("".to_string(), "make install DESTDIR=\"$DESTDIR\"".to_string()),
        ];
        let phases = split_phases(rec);
        let names: Vec<_> = phases.iter().map(|(n, _)| n.as_str()).collect();
        assert_eq!(names, vec!["src_compile", "src_install"]);
    }
}
