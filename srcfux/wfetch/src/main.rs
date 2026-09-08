use std::collections::HashMap;
use std::fs;
use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::process::{Command, ExitCode};

use clap::{Parser, Subcommand};
use reqwest::blocking::Client;
use serde::Deserialize;

mod pkgs;
mod aur;


#[derive(Parser)]
#[clap(name = "wfetch", version, about = "Pacman-compatible package manager for FunTux!")]
struct Cli {
    #[clap(short, long, help = "Run non-interactively, assume yes to prompts")]
    yes: bool,

    #[clap(short, long, help = "Path to a TOML config file to use instead of the default")]
    config: Option<PathBuf>,

    #[clap(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    Add {
        url: String,

        #[clap(long, help = "Install from the AUR (implies makepkg-style build)")]
        aur: bool,
    },

    Remove {
        package: String,
    },

    List,

    Info {
        package: String,
    },

    Update {
        package: Option<String>,
    },

    Clean,

    Search {
        term: String,
    },
}

#[derive(Deserialize, Default)]
pub(crate) struct Config {
    cache_dir: PathBuf,
    src_dir: PathBuf,
    pkgs_dir: PathBuf,
    db_dir: PathBuf,
    build_dir: PathBuf,
    tmp_dir: PathBuf,
    prefix: String,
}

#[derive(Deserialize)]
struct ConfigToml {
    cache: Option<PathBuf>,
    src: Option<PathBuf>,
    pkgs: Option<PathBuf>,
    db: Option<PathBuf>,
    build: Option<PathBuf>,
    tmp: Option<PathBuf>,
    prefix: Option<String>,
}

impl Config {
    fn default() -> Self {
        let root = PathBuf::from("/etc/wfetch");

        Self {
            cache_dir: root.join("cache"),
            src_dir: root.join("cache").join("src"),
            pkgs_dir: root.join("cache").join("pkgs"),
            db_dir: root.join("db"),
            build_dir: root.join("tmp"),
            tmp_dir: root.join("tmp"),
            prefix: "/usr".to_string(),
        }
    }

    fn load(config_path: Option<&Path>) -> Self {
        let mut config = Config::default();

        let paths_to_try: Vec<PathBuf> = config_path
            .map(|p| vec![p.to_path_buf()])
            .or_else(|| {
                std::env::var_os("WFETCH_CONFIG")
                    .map(|p| vec![PathBuf::from(p)])
            })
            .unwrap_or_else(|| {
                vec![
                    PathBuf::from("/etc/wfetch/config.toml"),
                    dirs::home_dir()
                        .map(|h| h.join(".config/wfetch/config.toml"))
                        .unwrap_or_else(|| PathBuf::from(".config/wfetch/config.toml")),
                ]
            });

        for path in &paths_to_try {
            if path.exists() {
                if let Ok(contents) = fs::read_to_string(path) {
                    match toml::from_str::<ConfigToml>(&contents) {
                        Ok(toml_cfg) => {
                            if let Some(v) = toml_cfg.cache {
                                config.cache_dir = v.clone();
                                config.src_dir = v.join("src");
                                config.pkgs_dir = v.join("pkgs");
                            }
                            if let Some(v) = toml_cfg.src {
                                config.src_dir = v;
                            }
                            if let Some(v) = toml_cfg.pkgs {
                                config.pkgs_dir = v;
                            }
                            if let Some(v) = toml_cfg.db {
                                config.db_dir = v;
                            }
                            if let Some(v) = toml_cfg.build {
                                config.build_dir = v;
                            }
                            if let Some(v) = toml_cfg.tmp {
                                config.tmp_dir = v;
                            }
                            if let Some(v) = toml_cfg.prefix {
                                config.prefix = v;
                            }
                        }
                        Err(e) => {
                            eprintln!(
                                "warning: failed to parse config file {}: {}",
                                path.display(),
                                e
                            );
                        }
                    }
                    break;
                }
            }
        }

        for dir in &[
            &config.cache_dir,
            &config.src_dir,
            &config.pkgs_dir,
            &config.db_dir,
            &config.build_dir,
            &config.tmp_dir,
        ] {
            fs::create_dir_all(dir).ok();
        }

        config
    }

    fn load_with_cli_path(config_path: Option<&PathBuf>) -> Self {
        Config::load(config_path.map(|p| p.as_path()))
    }
}

#[derive(Debug, Clone, Default)]
pub(crate) struct PackageMetadata {
    name: String,
    version: String,
    source: String,
    architecture: String,
    libc: String,
    depends: Vec<String>,
    sha256sums: HashMap<String, String>,
}

impl PackageMetadata {
    fn load_from_dir(dir: &Path) -> Option<Self> {
        let meta_path = dir.join("metadata");
        let contents = fs::read_to_string(&meta_path).ok()?;

        let mut metadata = PackageMetadata::default();

        for line in contents.lines() {
            if let Some((key, value)) = line.split_once('=') {
                match key.trim() {
                    "name" => metadata.name = value.trim().to_string(),
                    "version" => metadata.version = value.trim().to_string(),
                    "source" => metadata.source = value.trim().to_string(),
                    "architecture" => metadata.architecture = value.trim().to_string(),
                    "libc" => metadata.libc = value.trim().to_string(),
                    "depends" => {
                        metadata.depends = value.trim()
                            .split_whitespace()
                            .map(|s| s.to_string())
                            .collect();
                    },
                    _ => {}
                }
            }
        }

        Some(metadata)
    }

    fn save_to_dir(&self, dir: &Path) -> io::Result<()> {
        fs::create_dir_all(dir)?;

        let mut contents = String::new();
        contents.push_str(&format!("name={}\n", self.name));
        contents.push_str(&format!("version={}\n", self.version));
        contents.push_str(&format!("source={}\n", self.source));
        contents.push_str(&format!("architecture={}\n", self.architecture));
        contents.push_str(&format!("libc={}\n", self.libc));

        if !self.depends.is_empty() {
            contents.push_str(&format!("depends={}\n", self.depends.join(" ")));
        }

        fs::write(dir.join("metadata"), contents)
    }
}

#[allow(dead_code)]
pub(crate) enum ExitCode_ {
    Success = 0,
    GeneralError = 1,
    InvalidArgs = 2,
    PackageNotFound = 3,
    DownloadFailure = 4,
    ChecksumFailure = 5,
    BuildFailure = 6,
    InstallationFailure = 7,
    DependencyFailure = 8,
    PermissionFailure = 9,
}

impl ExitCode_ {
    fn exit_code(self) -> ExitCode {
        ExitCode::from(self as u8)
    }
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    let config = Config::load_with_cli_path(cli.config.as_ref());

    let result = match &cli.command {
        Commands::Add { url, aur } => {
            if *aur {
                aur::add_package_aur(&config, url.as_str(), cli.yes)
            } else {
                add_package(&config, url.as_str(), cli.yes)
            }
        },
        Commands::Remove { package } => {
            remove_package(&config, package.as_str())
        },
        Commands::List => {
            list_packages(&config)
        },
        Commands::Info { package } => {
            info_package(&config, package.as_str())
        },
        Commands::Update { package } => {
            update_package(&config, package.as_deref(), cli.yes)
        },
        Commands::Clean => {
            clean_cache(&config)
        },
        Commands::Search { term: _ } => {
            eprintln!("error: search is not implemented yet");
            ExitCode_::GeneralError.exit_code()
        },
    };

    result
}

fn add_package(config: &Config, url: &str, yes: bool) -> ExitCode {
    if !yes {
        print_warning(url);
        match get_user_confirmation() {
            Some(true) => {},
            _ => {
                println!("Aborted.");
                return ExitCode_::GeneralError.exit_code();
            }
        }
    }

    if !is_valid_url(url) {
        eprintln!("error: invalid URL: {}", url);
        return ExitCode_::InvalidArgs.exit_code();
    }

    let (_pkg_name, src_dir) = match fetch_source(&config, url) {
        Ok(result) => result,
        Err(e) => {
            eprintln!("error: failed to fetch source: {}", e);
            return ExitCode_::DownloadFailure.exit_code();
        }
    };

    let wbuild_path = src_dir.join("wbuild.sh");
    if !wbuild_path.exists() {
        eprintln!("error: wbuild.sh not found in package source");
        return ExitCode_::BuildFailure.exit_code();
    }

    let metadata = match parse_wbuild_metadata(&wbuild_path) {
        Ok(meta) => meta,
        Err(e) => {
            eprintln!("error: failed to parse wbuild.sh: {}", e);
            return ExitCode_::BuildFailure.exit_code();
        }
    };

    if !metadata.sha256sums.is_empty() {
        if let Err(e) = verify_checksums(&src_dir, &metadata.sha256sums) {
            eprintln!("error: checksum verification failed: {}", e);
            return ExitCode_::ChecksumFailure.exit_code();
        }
    }

    if let Err(e) = check_dependencies(&config, &metadata.depends) {
        eprintln!("error: missing dependency: {}", e);
        return ExitCode_::DependencyFailure.exit_code();
    }

    let build_dir = config.build_dir.join(&metadata.name);
    let destdir = build_dir.join("root");

    let _ = fs::create_dir_all(&build_dir);
    let _ = fs::create_dir_all(&destdir);

    if !run_wbuild_func(&wbuild_path, "prepare", &build_env(&config, &src_dir, &build_dir, &destdir, &metadata)) {
        eprintln!("error: prepare() failed");
        return ExitCode_::BuildFailure.exit_code();
    }

    if !run_wbuild_func(&wbuild_path, "build", &build_env(&config, &src_dir, &build_dir, &destdir, &metadata)) {
        eprintln!("error: build() failed");
        return ExitCode_::BuildFailure.exit_code();
    }

    if !run_wbuild_func(&wbuild_path, "install", &build_env(&config, &src_dir, &build_dir, &destdir, &metadata)) {
        eprintln!("error: install() failed");
        return ExitCode_::InstallationFailure.exit_code();
    }

    let manifest_files = generate_file_manifest(&destdir);

    if !install_files_to_root(&destdir, &manifest_files) {
        eprintln!("error: failed to install files to system");
        return ExitCode_::InstallationFailure.exit_code();
    }

    let pkg_db_dir = config.db_dir.join(&metadata.name);
    let _ = fs::create_dir_all(&pkg_db_dir);

    if let Err(e) = metadata.save_to_dir(&pkg_db_dir) {
        eprintln!("error: failed to save metadata: {}", e);
        return ExitCode_::InstallationFailure.exit_code();
    }

    let mut files_content = String::new();
    for file_path in &manifest_files {
        let relative = file_path.strip_prefix(&destdir).unwrap_or(&file_path);
        files_content.push_str(&relative.display().to_string());
        files_content.push('\n');
    }
    let _ = fs::write(pkg_db_dir.join("files"), files_content);

    println!("Successfully installed {} {}", metadata.name, metadata.version);
    ExitCode_::Success.exit_code()
}

fn remove_package(config: &Config, package: &str) -> ExitCode {
    let pkg_db_dir = config.db_dir.join(package);

    if !pkg_db_dir.exists() {
        eprintln!("error: package not found: {}", package);
        return ExitCode_::PackageNotFound.exit_code();
    }

    let metadata = match PackageMetadata::load_from_dir(&pkg_db_dir) {
        Some(meta) => meta,
        None => {
            eprintln!("error: corrupted package database for {}", package);
            return ExitCode_::GeneralError.exit_code();
        }
    };

    let files_path = pkg_db_dir.join("files");
    let files_content = match fs::read_to_string(&files_path) {
        Ok(content) => content,
        Err(_) => {
            eprintln!("error: failed to read file manifest for {}", package);
            return ExitCode_::GeneralError.exit_code();
        }
    };

    let mut failed = false;
    for line in files_content.lines() {
        let file_path = Path::new(line);
        if file_path.exists() {
            if let Err(e) = fs::remove_file(file_path) {
                eprintln!("warning: failed to remove {}: {}", line, e);
                failed = true;
            }
        }
    }

    if let Err(e) = fs::remove_dir_all(&pkg_db_dir) {
        eprintln!("error: failed to remove package directory: {}", e);
        return ExitCode_::GeneralError.exit_code();
    }

    let wbuild_path = config.src_dir.join(package).join("wbuild.sh");
    if wbuild_path.exists() {
        let _ = Command::new("sh")
            .arg(&wbuild_path)
            .env("DESTDIR", &config.build_dir.join(package).join("root"))
            .env("PREFIX", &config.prefix)
            .env("SRC_DIR", &config.src_dir.join(package))
            .env("BUILD_DIR", &config.build_dir.join(package))
            .env("WFETCH_ROOT", &config.cache_dir)
            .arg("uninstall")
            .status();
    }

    if failed {
        println!("Package removed with warnings: {}", package);
        return ExitCode_::GeneralError.exit_code();
    }

    println!("Removed package: {}", package);
    ExitCode_::Success.exit_code()
}

fn list_packages(config: &Config) -> ExitCode {
    let mut packages = Vec::new();

    if let Ok(entries) = fs::read_dir(&config.db_dir) {
        for entry in entries.flatten() {
            if entry.path().is_dir() {
                if let Some(metadata) = PackageMetadata::load_from_dir(&entry.path()) {
                    packages.push((metadata.name, metadata.version));
                }
            }
        }
    }

    packages.sort_by(|a, b| a.0.cmp(&b.0));

    for (name, version) in packages {
        println!("{} {}", name, version);
    }

    ExitCode_::Success.exit_code()
}

fn info_package(config: &Config, package: &str) -> ExitCode {
    let pkg_db_dir = config.db_dir.join(package);

    if !pkg_db_dir.exists() {
        eprintln!("error: package not found: {}", package);
        return ExitCode_::PackageNotFound.exit_code();
    }

    match PackageMetadata::load_from_dir(&pkg_db_dir) {
        Some(metadata) => {
            println!("Name: {}", metadata.name);
            println!("Version: {}", metadata.version);
            println!("Source: {}", metadata.source);
            println!("Architecture: {}", metadata.architecture);
            println!("Libc: {}", metadata.libc);
            if !metadata.depends.is_empty() {
                println!("Depends: {}", metadata.depends.join(" "));
            }
            ExitCode_::Success.exit_code()
        },
        None => {
            eprintln!("error: corrupted package database for {}", package);
            ExitCode_::GeneralError.exit_code()
        }
    }
}

fn update_package(config: &Config, package: Option<&str>, yes: bool) -> ExitCode {
    if let Some(pkg) = package {
        let pkg_db_dir = config.db_dir.join(pkg);
        if !pkg_db_dir.exists() {
            eprintln!("error: package not found: {}", pkg);
            return ExitCode_::PackageNotFound.exit_code();
        }

        let metadata = match PackageMetadata::load_from_dir(&pkg_db_dir) {
            Some(meta) => meta,
            None => {
                eprintln!("error: corrupted package database for {}", pkg);
                return ExitCode_::GeneralError.exit_code();
            }
        };

        eprintln!("Updating {}...", metadata.name);
        remove_package(config, pkg);
        add_package(config, &metadata.source, yes)
    } else {
        let mut packages = Vec::new();
        if let Ok(entries) = fs::read_dir(&config.db_dir) {
            for entry in entries.flatten() {
                if entry.path().is_dir() {
                    if let Some(metadata) = PackageMetadata::load_from_dir(&entry.path()) {
                        packages.push((metadata.name, metadata.source));
                    }
                }
            }
        }

        let mut any_failed = false;
        for (name, source) in packages {
            eprintln!("Updating {}...", name);
            let result = remove_package(config, &name);
            if result != ExitCode::SUCCESS {
                any_failed = true;
                continue;
            }
            let result = add_package(config, &source, yes);
            if result != ExitCode::SUCCESS {
                any_failed = true;
            }
        }

        if any_failed {
            ExitCode_::GeneralError.exit_code()
        } else {
            ExitCode_::Success.exit_code()
        }
    }
}

fn clean_cache(config: &Config) -> ExitCode {
    let _ = fs::remove_dir_all(&config.pkgs_dir);
    let _ = fs::create_dir_all(&config.pkgs_dir);

    let _ = fs::remove_dir_all(&config.src_dir);
    let _ = fs::create_dir_all(&config.src_dir);

    let _ = fs::remove_dir_all(&config.build_dir);
    let _ = fs::create_dir_all(&config.build_dir);

    println!("Cache cleaned.");
    ExitCode_::Success.exit_code()
}

fn is_valid_url(url: &str) -> bool {
    url.starts_with("http://") || url.starts_with("https://") || url.starts_with("git@") || url.starts_with("ssh://")
}

fn is_git_url(url: &str) -> bool {
    url.ends_with(".git") || url.contains("://") && url.contains("git@")
}

fn fetch_source(config: &Config, url: &str) -> Result<(String, PathBuf), String> {
    let pkg_name = extract_pkg_name(url);
    let dest = if is_git_url(url) {
        config.src_dir.join(&pkg_name)
    } else {
        let archive_dest = config.cache_dir.join(url.split('/').last().unwrap_or("archive"));
        let _ = fs::create_dir_all(archive_dest.parent().unwrap_or(&config.cache_dir));
        download_file(url, &archive_dest)?;
        let extract_dir = config.src_dir.join(&pkg_name);
        let _ = fs::remove_dir_all(&extract_dir);
        extract_archive(&archive_dest, &extract_dir)?;

        extract_dir
    };

    if is_git_url(url) {
        let _ = fs::remove_dir_all(&dest);
        let result = Command::new("git")
            .arg("clone")
            .arg(url)
            .arg(&dest)
            .status();

        match result {
            Ok(status) if status.success() => {},
            _ => return Err("git clone failed".to_string()),
        }
    }

    Ok((pkg_name, dest))
}

pub(crate) fn extract_pkg_name(url: &str) -> String {
    let name = url.split('/').last().unwrap_or("unknown");
    if name.ends_with(".git") {
        name.trim_end_matches(".git").to_string()
    } else if name.contains('-') && name.contains('.') {
        name.split('-').next().unwrap_or(name).to_string()
    } else {
        name.to_string()
    }
}

fn download_file(url: &str, dest: &Path) -> Result<(), String> {
    let client = Client::new();
    let response = client.get(url)
        .send()
        .map_err(|e| format!("network error: {}", e))?;

    if !response.status().is_success() {
        return Err(format!("HTTP status: {}", response.status()));
    }

    let bytes = response.bytes()
        .map_err(|e| format!("failed to read response: {}", e))?;

    fs::write(dest, bytes)
        .map_err(|e| format!("failed to write file: {}", e))?;

    Ok(())
}

fn extract_archive(archive: &Path, dest: &Path) -> Result<(), String> {
    let _ = fs::create_dir_all(dest);

    let result = if archive.extension().map_or(false, |ext| ext == "zip") {
        Command::new("unzip")
            .arg("-q")
            .arg(archive)
            .arg("-d")
            .arg(dest)
            .status()
    } else {
        Command::new("tar")
            .arg("xf")
            .arg(archive)
            .arg("-C")
            .arg(dest)
            .status()
    };

    match result {
        Ok(status) if status.success() => Ok(()),
        _ => Err("extraction command failed".to_string()),
    }
}

fn parse_wbuild_metadata(wbuild_path: &Path) -> Result<PackageMetadata, String> {
    let content = fs::read_to_string(wbuild_path)
        .map_err(|e| format!("failed to read wbuild.sh: {}", e))?;

    let mut metadata = PackageMetadata::default();

    for line in content.lines() {
        let line = line.trim();
        if line.starts_with('#') {
            continue;
        }

        if let Some((key, value)) = line.split_once('=') {
            let key = key.trim();
            let value = value.trim().trim_matches('"');

            match key {
                "name" => metadata.name = value.to_string(),
                "version" => metadata.version = value.to_string(),
                "depends" => {
                    metadata.depends = value.split_whitespace()
                        .map(|s| s.to_string())
                        .collect();
                },
                "libc" => metadata.libc = value.to_string(),
                _ => {}
            }
        }

        if line.starts_with("sha256sums") {
            let parts: Vec<&str> = line.split_whitespace().collect();
            if parts.len() >= 2 {
                metadata.sha256sums.insert(parts[1].to_string(), parts[0].to_string());
            }
        }
    }

    if metadata.name.is_empty() {
        metadata.name = wbuild_path.parent()
            .and_then(|n| n.to_str())
            .unwrap_or("unknown")
            .to_string();
    }

    if metadata.version.is_empty() {
        metadata.version = "unknown".to_string();
    }

    if metadata.architecture.is_empty() {
        metadata.architecture = get_architecture();
    }

    if metadata.libc.is_empty() {
        metadata.libc = get_libc();
    }

    Ok(metadata)
}

fn verify_checksums(src_dir: &Path, sums: &HashMap<String, String>) -> Result<(), String> {
    for (filename, expected) in sums {
        let file_path = src_dir.join(filename);
        if !file_path.exists() {
            return Err(format!("file not found: {}", filename));
        }

        let actual = compute_sha256(&file_path)
            .map_err(|e| format!("failed to compute checksum: {}", e))?;

        if actual != *expected {
            return Err(format!("checksum mismatch for {}: expected {}, got {}", filename, expected, actual));
        }
    }

    Ok(())
}

fn compute_sha256(path: &Path) -> Result<String, String> {
    let output = Command::new("sha256sum")
        .arg(path)
        .output()
        .map_err(|e| format!("sha256sum command failed: {}", e))?;

    if !output.status.success() {
        return Err("sha256sum command failed".to_string());
    }

    let stdout = String::from_utf8_lossy(&output.stdout);
    let hash = stdout.split_whitespace().next().ok_or("empty checksum output")?;

    Ok(hash.to_string())
}

fn check_dependencies(_config: &Config, depends: &[String]) -> Result<(), String> {
    for dep in depends {
        let pkg_dir = _config.db_dir.join(dep);
        if !pkg_dir.exists() {
            return Err(dep.clone());
        }
    }

    Ok(())
}

fn build_env(config: &Config, src_dir: &Path, build_dir: &Path, destdir: &Path, metadata: &PackageMetadata) -> Vec<(&'static str, String)> {
    vec![
        ("SRC_DIR", src_dir.display().to_string()),
        ("BUILD_DIR", build_dir.display().to_string()),
        ("DESTDIR", destdir.display().to_string()),
        ("PREFIX", config.prefix.clone()),
        ("WFETCH_ROOT", config.cache_dir.display().to_string()),
        ("WFC_ARCH", metadata.architecture.clone()),
        ("WFC_LIBC", metadata.libc.clone()),
        ("PKG_NAME", metadata.name.clone()),
        ("PKG_VERSION", metadata.version.clone()),
    ]
}

fn run_wbuild_func(wbuild_path: &Path, func_name: &str, env_vars: &[(&'static str, String)]) -> bool {
    let script = format!("{{ source {}; {} ; }}", wbuild_path.display(), func_name);

    let mut cmd = Command::new("sh");
    cmd.arg("-c").arg(&script);

    for (key, value) in env_vars {
        cmd.env(key, value);
    }

    match cmd.status() {
        Ok(status) => status.success(),
        Err(_) => false,
    }
}

fn generate_file_manifest(destdir: &Path) -> Vec<PathBuf> {
    let mut files = Vec::new();
    collect_files(destdir, &mut files);
    files
}

fn collect_files(dir: &Path, files: &mut Vec<PathBuf>) {
    if let Ok(entries) = fs::read_dir(dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_file() {
                files.push(path);
            } else if path.is_dir() {
                collect_files(&path, files);
            }
        }
    }
}

fn install_files_to_root(src: &Path, files: &[PathBuf]) -> bool {
    for file_path in files {
        let relative = file_path.strip_prefix(src).unwrap_or(file_path);
        let dest_path = Path::new("/").join(relative);

        if let Some(parent) = dest_path.parent() {
            if let Err(e) = fs::create_dir_all(parent) {
                eprintln!("error: failed to create directory {}: {}", parent.display(), e);
                return false;
            }
        }

        if let Err(e) = fs::copy(file_path, &dest_path) {
            eprintln!("error: failed to copy {} to {}: {}", file_path.display(), dest_path.display(), e);
            return false;
        }
    }

    true
}

pub(crate) fn get_architecture() -> String {
    let output = Command::new("uname")
        .arg("-m")
        .output();

    match output {
        Ok(o) if o.status.success() => {
            String::from_utf8_lossy(&o.stdout).trim().to_string()
        },
        _ => "unknown".to_string(),
    }
}

pub(crate) fn get_libc() -> String {
    let ld_path = Path::new("/lib/ld-musl.so.1");
    if ld_path.exists() {
        return "musl".to_string();
    }

    "glibc".to_string()
}

pub(crate) fn print_warning(url: &str) {
    eprintln!("warning: package contains executable build instructions.");
    eprintln!("source: {}", url);
    eprintln!();
}

pub(crate) fn get_user_confirmation() -> Option<bool> {
    print!("Continue? [y/N] ");
    io::stdout().flush().ok();

    let mut input = String::new();
    match io::stdin().read_line(&mut input) {
        Ok(_) => {
            match input.trim().to_lowercase().as_str() {
                "y" | "yes" => Some(true),
                _ => Some(false),
            }
        },
        Err(_) => None,
    }
}
