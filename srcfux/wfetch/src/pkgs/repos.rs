use serde::Deserialize;
use std::fs;
use std::path::{Path, PathBuf};

#[derive(Deserialize, Debug, Clone)]
pub struct ReposConfig {
    pub repos: Vec<RepoEntry>,
}

#[derive(Deserialize, Debug, Clone)]
pub struct RepoEntry {
    pub url: String,
    pub name: String,
    #[serde(default = "default_enabled")]
    pub enabled: bool,
}

fn default_enabled() -> bool {
    true
}

pub fn read_repo_config(repos_path: Option<&Path>) -> std::io::Result<ReposConfig> {
    let paths_to_try: Vec<PathBuf> = repos_path
        .map(|p| vec![p.to_path_buf()])
        .or_else(|| {
            std::env::var_os("WFETCH_REPOS_CONFIG")
                .map(|p| vec![PathBuf::from(p)])
        })
        .unwrap_or_else(|| {
            vec![
                PathBuf::from("/etc/wfetch/repos.toml"),
                dirs::home_dir()
                    .map(|h| h.join(".config/wfetch/repos.toml"))
                    .unwrap_or_else(|| PathBuf::from(".config/wfetch/repos.toml")),
            ]
        });

    for path in &paths_to_try {
        if path.exists() {
            let content = fs::read_to_string(path)?;
            return toml::from_str(&content)
                .map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e));
        }
    }

    // If no config file found, return a default repo
    Ok(ReposConfig {
        repos: vec![RepoEntry {
            url: "https://codeberg.org/derivelinux/ports.git".to_string(),
            name: "ports".to_string(),
            enabled: true,
        }],
    })
}

pub fn read_repo_config_default() -> std::io::Result<ReposConfig> {
    read_repo_config(None)
}