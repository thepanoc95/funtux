/*
    refs:
        https://doc.rust-lang.org/std/process/index.html
 */

use std::process::Command;

let arrow = "==>".Green();
// let defaultPortsRepoUrl = "https://github.com/thepanoc95/funtux-ports.git";
let defaultPortsRepoUrl = "https://codeberg.org/derivelinux/ports.git"; // dérive ports tree as a stub ;-;
let defaultTargetDir = "/ports";

fn chkForGit() -> bool {
    let _output = Command::new("git")
        .arg("--version")
        .spawn()
        .output();

    match _output {
        Ok(_output) => {
            if _output.status.success() {
                let version = String::from_utf8_lossy(&_output.stdout);
                println!("Yo, just found git version {}", version);
            } else {
                println!("Uh...looks like git is not installed...I will not be able to fetch the ports repo without it.");
            }
        }
        Err(e) => {
            std::process::exit(1);
        }
    }
}

pub fn fetchPortsRepo() {
    println!("{} Fetching FunTux Ports repo....", arrow);
    println!("{} Target dir is /ports, do not remove it outside of wfetch, run wfetch clean ports to delete it.", "[INFO]".yellow());
    let clonePorts = Command::new("git")
        .arg("clone")
        .arg("{}", defaultPortsRepoUrl)
        .arg("{}", defaultTargetDir)
        .stdout(Stdio::piped())
        .spawn()
        .expect("fatal: unable to access '{}': Could not resolve host: codeberg.org")

    let output = clonePorts.wait_with_output().expect("failed to wait on child");
    if !output.status.success() {
        println!("{} Failed to fetch ports repo, please check your internet connection and try again.", "[ERROR]".red());
        std::process::exit(1);
    }

    if output.status.success() {
        let checkTarget = Command::new("ls")
            .arg("{}", defaultTargetDir)
            .stdout(Stdio::piped())
            .spawn()
            .expect("no such file or directory")
    }
}