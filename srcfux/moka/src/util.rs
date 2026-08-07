use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

pub type R<T> = Result<T, String>;

pub fn sq(s: &str) -> String {
    format!("'{}'", s.replace('\'', "'\\''"))
}

pub fn sh_run(cmd: &str) -> R<i32> {
    Command::new("/bin/sh")
        .arg("-c")
        .arg(cmd)
        .status()
        .map(|s| s.code().unwrap_or(-1))
        .map_err(|e| format!("failed to run `{}`: {}", cmd, e))
}

pub fn sh_ok(cmd: &str) -> R<()> {
    let code = sh_run(cmd)?;
    if code == 0 {
        Ok(())
    } else {
        Err(format!("command failed (exit {}): {}", code, cmd))
    }
}

pub fn sh_out(cmd: &str) -> R<String> {
    let out = Command::new("/bin/sh")
        .arg("-c")
        .arg(cmd)
        .output()
        .map_err(|e| format!("failed to run `{}`: {}", cmd, e))?;
    if !out.status.success() {
        return Err(format!(
            "command failed (exit {:?}): {}",
            out.status.code(),
            cmd
        ));
    }
    String::from_utf8(out.stdout)
        .map(|s| s.trim_end().to_string())
        .map_err(|e| e.to_string())
}

pub fn read_file(p: &Path) -> R<String> {
    std::fs::read_to_string(p).map_err(|e| format!("{}: {}", p.display(), e))
}

pub fn write_file(p: &Path, s: &str) -> R<()> {
    ensure_parent(p)?;
    std::fs::write(p, s).map_err(|e| format!("{}: {}", p.display(), e))
}

pub fn read_lines(p: &Path) -> Vec<String> {
    read_file(p)
        .map(|s| {
            s.lines()
                .map(|l| l.trim().to_string())
                .filter(|l| !l.is_empty())
                .collect()
        })
        .unwrap_or_default()
}

pub fn ensure_dir(p: &Path) -> R<()> {
    std::fs::create_dir_all(p).map_err(|e| format!("{}: {}", p.display(), e))
}

pub fn ensure_parent(p: &Path) -> R<()> {
    if let Some(d) = p.parent() {
        if !d.as_os_str().is_empty() {
            ensure_dir(d)?;
        }
    }
    Ok(())
}

pub fn rm_rf(p: &Path) -> R<()> {
    sh_ok(&format!("rm -rf {}", sq(&p.display().to_string())))
}

pub fn list_dir(p: &Path) -> R<Vec<PathBuf>> {
    let mut v = Vec::new();
    if p.is_dir() {
        for e in std::fs::read_dir(p).map_err(|e| format!("{}: {}", p.display(), e))? {
            v.push(e.map_err(|e| e.to_string())?.path());
        }
    }
    Ok(v)
}

pub fn basename(p: &Path) -> String {
    p.file_name()
        .map(|s| s.to_string_lossy().into_owned())
        .unwrap_or_default()
}

// run a phase script: `cd work && ENV=... sh`, script piped to stdin
pub fn run_phase(
    name: &str,
    script: &str,
    env: &[(String, String)],
    work: &Path,
) -> R<()> {
    if script.trim().is_empty() {
        return Ok(());
    }
    use std::io::Write;
    let envs = env
        .iter()
        .map(|(k, v)| format!("{}={}", k, sq(v)))
        .collect::<Vec<_>>()
        .join(" ");
    let cmd = format!("cd {} && {} sh", sq(&work.display().to_string()), envs);
    let mut child = Command::new("/bin/sh")
        .arg("-c")
        .arg(&cmd)
        .stdin(Stdio::piped())
        .spawn()
        .map_err(|e| format!("failed to spawn phase: {}", e))?;
    if let Some(mut stdin) = child.stdin.take() {
        stdin
            .write_all(script.as_bytes())
            .map_err(|e| format!("failed to write phase script: {}", e))?;
    }
    let status = child
        .wait()
        .map_err(|e| format!("failed to wait for phase: {}", e))?;
    let code = status.code().unwrap_or(-1);
    if code != 0 {
        return Err(format!("phase {} failed (exit {})", name, code));
    }
    Ok(())
}
