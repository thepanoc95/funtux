use std::env;
use std::process::{Command, ExitCode, Stdio};

use clap::{Parser, Subcommand};

#[derive(Parser)]
#[command(name = "makepkg", version, about = "A makepkg-like CLI build tool for FunTux packages")]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    Build {
        #[arg(short, long, help = "Run makepkg build in the given directory")]
        dir: String,

        #[arg(short, long, help = "Clean before building")]
        clean: bool,

        #[arg(long, help = "Install dependencies with pacman first")]
        install_deps: bool,

        #[arg(short, long, help = "Skip checksum verification")]
        skip_checksums: bool,

        #[arg(short, long, help = "Skip dependency checking")]
        skip_dependencies: bool,
    },

    Install {
        #[arg(short, long, help = "Install after building")]
        syncdeps: bool,

        #[arg(short, long, help = "Remove dependencies after install")]
        remove_deps: bool,

        #[arg(long, help = "Force install even if already installed")]
        force: bool,

        #[arg(help = "Path to package directory or .tar.gz package file")]
        target: String,
    },

    Query {
        #[arg(short, long, help = "Query by name")]
        name: Option<String>,
    },

    Clean {
        #[arg(short, long, help = "Remove all but downloaded sources")]
        keep: bool,

        #[arg(help = "Package directory to clean")]
        dir: String,
    },
}

fn main() -> ExitCode {
    let cli = Cli::parse();

    match cli.command {
        Commands::Build {
            dir,
            clean,
            install_deps,
            skip_checksums,
            skip_dependencies,
        } => cmd_build(&dir, clean, install_deps, skip_checksums, skip_dependencies),

        Commands::Install {
            syncdeps,
            remove_deps,
            force,
            target,
        } => cmd_install(syncdeps, remove_deps, force, &target),

        Commands::Query { name } => cmd_query(name.as_deref()),

        Commands::Clean { keep, dir } => cmd_clean(keep, &dir),
    }
}

fn cmd_build(
    dir: &str,
    clean: bool,
    install_deps: bool,
    skip_checksums: bool,
    skip_dependencies: bool,
) -> ExitCode {
    let mut args = vec!["-s".to_string()];

    if clean {
        args.push("-c".to_string());
    }

    if install_deps {
        args.push("--noconfirm".to_string());
    }

    if skip_checksums {
        args.push("--skipchecksums".to_string());
    }

    if skip_dependencies {
        args.push("--skipdeps".to_string());
    }

    eprintln!("makepkg: invoking makepkg in {}", dir);

    let status = match Command::new("makepkg")
        .args(&args)
        .current_dir(dir)
        .status()
    {
        Ok(s) => s,
        Err(e) => {
            eprintln!("error: failed to execute makepkg: {}", e);
            return ExitCode::from(1);
        }
    };

    if status.success() {
        eprintln!("makepkg: build completed successfully");
        ExitCode::SUCCESS
    } else {
        eprintln!("makepkg: build failed");
        ExitCode::from(1)
    }
}

fn cmd_install(
    syncdeps: bool,
    remove_deps: bool,
    force: bool,
    target: &str,
) -> ExitCode {
    let path = std::path::Path::new(target);
    let is_pkg_file = path.extension().map_or(false, |ext| ext == "pkg.tar" || ext == "tar");
    let is_dir = path.is_dir();

    if !is_pkg_file && !is_dir {
        eprintln!("error: target '{}' is not a directory or package file", target);
        return ExitCode::from(1);
    }

    let mut args = vec!["-U".to_string()];

    if syncdeps {
        args.push("-s".to_string());
    }

    if remove_deps {
        args.push("-r".to_string());
    }

    if force {
        args.push("--force".to_string());
    }

    args.push(target.to_string());

    eprintln!("makepkg: running makepkg -U {}", target);

    let status = match Command::new("makepkg")
        .args(&args)
        .stdin(Stdio::null())
        .status()
    {
        Ok(s) => s,
        Err(e) => {
            eprintln!("error: failed to execute makepkg: {}", e);
            return ExitCode::from(1);
        }
    };

    if status.success() {
        eprintln!("makepkg: install completed successfully");
        ExitCode::SUCCESS
    } else {
        eprintln!("makepkg: install failed");
        ExitCode::from(1)
    }
}

fn cmd_query(name: Option<&str>) -> ExitCode {
    let db = "/var/lib/pacman";
    let local_dir = format!("{}/local", db);

    let entries = match std::fs::read_dir(&local_dir) {
        Ok(e) => e,
        Err(e) => {
            eprintln!("error: cannot read pacman database at {}: {}", local_dir, e);
            return ExitCode::from(1);
        }
    };

    println!("{:<40} {}", "NAME", "VERSION");
    println!("{}", "-".repeat(60));

    let mut found = 0;
    for entry in entries.flatten() {
        let pkg_name = entry.file_name().to_string_lossy().to_string();

        if let Some(n) = name {
            if !pkg_name.starts_with(n) {
                continue;
            }
        }

        let desc_path = entry.path().join("desc");
        let desc = match std::fs::read_to_string(&desc_path) {
            Ok(d) => d,
            Err(_) => continue,
        };

        let mut version = String::new();
        for line in desc.lines() {
            if line == "%VERSION%" {
                continue;
            }
            if !line.starts_with('%') && !line.is_empty() {
                version = line.to_string();
                break;
            }
        }

        println!("{:<40} {}", pkg_name, version);
        found += 1;
    }

    if found == 0 {
        eprintln!("makepkg: no matching packages found");
    }

    ExitCode::SUCCESS
}

fn cmd_clean(keep: bool, dir: &str) -> ExitCode {
    let src_dir = format!("{}/src", dir);
    let pkg_dir = format!("{}/pkg", dir);

    if keep {
        if std::path::Path::new(&src_dir).exists() {
            if let Err(e) = std::fs::remove_dir_all(&src_dir) {
                eprintln!("warning: failed to remove {}: {}", src_dir, e);
            } else {
                eprintln!("makepkg: removed {}", src_dir);
            }
        }
        if std::path::Path::new(&pkg_dir).exists() {
            if let Err(e) = std::fs::remove_dir_all(&pkg_dir) {
                eprintln!("warning: failed to remove {}: {}", pkg_dir, e);
            } else {
                eprintln!("makepkg: removed {}", pkg_dir);
            }
        }
    } else {
        eprintln!("makepkg: cleaning all build artifacts in {}", dir);

        let status = match Command::new("makepkg")
            .arg("-C")
            .current_dir(dir)
            .status()
        {
            Ok(s) => s,
            Err(e) => {
                eprintln!("error: failed to execute makepkg -C: {}", e);
                return ExitCode::from(1);
            }
        };

        if status.success() {
            eprintln!("makepkg: clean completed");
            return ExitCode::SUCCESS;
        } else {
            eprintln!("makepkg: clean failed");
            return ExitCode::from(1);
        }
    }

    ExitCode::SUCCESS
}
