/*
    refs:
        https://doc.rust-lang.org/std/process/index.html
 */

use std::process::{Command, Stdio};

use colored::Colorize;

const ARROW: &str = "==>";
// let defaultPortsRepoUrl = "https://github.com/thepanoc95/funtux-ports.git";
const DEFAULT_PORTS_REPO_URL: &str = "https://codeberg.org/derivelinux/ports.git"; // dérive ports tree as a stub ;-;
const DEFAULT_TARGET_DIR: &str = "/ports";

pub fn chk_for_git() -> bool {
    let output = Command::new("git")
        .arg("--version")
        .output();

    match output {
        Ok(output) => {
            if output.status.success() {
                let version = String::from_utf8_lossy(&output.stdout);
                println!("Yo, just found git version {}", version);
                true
            } else {
                println!("Uh...looks like git is not installed...I will not be able to fetch the ports repo without it.");
                false
            }
        }
        Err(_e) => false,
    }
}

pub fn fetch_ports_repo() {
    println!("{} Fetching FunTux Ports repo....", ARROW.green());
    println!("{} Target dir is /ports, do not remove it outside of wfetch, run wfetch clean ports to delete it.", "[INFO]".yellow());
    let clone_ports = Command::new("git")
        .arg("clone")
        .arg(DEFAULT_PORTS_REPO_URL)
        .arg(DEFAULT_TARGET_DIR)
        .stdout(Stdio::piped())
        .spawn()
        .expect("fatal: unable to access '{}': Could not resolve host: codeberg.org");

    let output = clone_ports.wait_with_output().expect("failed to wait on child");
    if !output.status.success() {
        println!("{} Failed to fetch ports repo, please check your internet connection and try again.", "[ERROR]".red());
        std::process::exit(1);
    }

    if output.status.success() {
        let _check_target = Command::new("ls")
            .arg(DEFAULT_TARGET_DIR)
            .stdout(Stdio::piped())
            .spawn()
            .expect("no such file or directory");
    }
}