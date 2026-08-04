//! moka: Portage-like source-based package manager for FunTux Linux.

use moka::{build, config::Config, pack, pkg, recipe, repo, resolve, util, util::R};
use std::path::PathBuf;

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let cfg = Config::new();
    if let Err(e) = run(&cfg, &args) {
        eprintln!("error: {}", e);
        std::process::exit(1);
    }
}

fn run(cfg: &Config, args: &[String]) -> R<()> {
    if args.is_empty() {
        usage();
        return Ok(());
    }
    match args[0].as_str() {
        "repo-setup" => {
            let url = args.get(1).ok_or("usage: moka repo-setup <git-url>")?;
            repo::repo_setup(cfg, url)
        }
        "search" | "s" => {
            let kw = args.get(1).ok_or("usage: moka search <keyword>")?;
            for p in repo::search_packages(cfg, kw) {
                println!("{}  {}", p.atom, p.description);
            }
            Ok(())
        }
        "info" => {
            let query = args.get(1).ok_or("usage: moka info <atom>")?;
            let p = repo::find_package(cfg, query)?;
            println!("{}", p.atom);
            if !p.description.is_empty() {
                println!("  {}", p.description);
            }
            for (k, v) in &p.ebuild {
                println!("  {}={}", k, v);
            }
            println!("  phases: {}", p.phases.keys().cloned().collect::<Vec<_>>().join(", "));
            Ok(())
        }
        "build" => {
            let query = args.get(1).ok_or("usage: moka build <atom>")?;
            cfg.ensure_dirs()?;
            repo::ensure_repo(cfg)?;
            // Install any missing deps into the root, then stage-build the target.
            let plan = resolve::resolve(cfg, &[query.to_string()])?;
            for p in plan.iter().take(plan.len().saturating_sub(1)) {
                install_pkg(cfg, p)?;
            }
            let target = match plan.last() {
                Some(p) => p.clone(),
                None => repo::find_package(cfg, query)?,
            };
            let mut ctx = build::BuildCtx::new(cfg, &target)?;
            ctx.build()?;
            println!("build ok: {}", target.atom);
            Ok(())
        }
        "install" => {
            let query = args.get(1).ok_or("usage: moka install <atom>")?;
            cfg.ensure_dirs()?;
            repo::ensure_repo(cfg)?;
            let plan = resolve::resolve(cfg, &[query.to_string()])?;
            if plan.is_empty() {
                println!("`{}` is already installed", query);
                return Ok(());
            }
            for p in &plan {
                install_pkg(cfg, p)?;
            }
            Ok(())
        }
        "pkg" => {
            let query = args.get(1).ok_or("usage: moka pkg <atom> [outdir]")?;
            cfg.ensure_dirs()?;
            repo::ensure_repo(cfg)?;
            // Install missing deps so the build can link against them.
            let plan = resolve::resolve(cfg, &[query.to_string()])?;
            for p in plan.iter().take(plan.len().saturating_sub(1)) {
                install_pkg(cfg, p)?;
            }
            let target = match plan.last() {
                Some(p) => p.clone(),
                None => repo::find_package(cfg, query)?,
            };
            let mut ctx = build::BuildCtx::new(cfg, &target)?;
            ctx.build()?;
            let outdir = args
                .get(2)
                .map(PathBuf::from)
                .unwrap_or_else(|| cfg.cache_dir.join("bin"));
            let out = pack::pack(cfg, &target, &ctx.image, &outdir)?;
            println!("packed {}", out.display());
            Ok(())
        }
        "resolve" | "deps" => {
            let query = args.get(1).ok_or("usage: moka resolve <atom>")?;
            cfg.ensure_dirs()?;
            repo::ensure_repo(cfg)?;
            let plan = resolve::resolve(cfg, &[query.to_string()])?;
            for p in &plan {
                println!("{}", p.atom);
            }
            Ok(())
        }
        "uninstall" => {
            let query = args.get(1).ok_or("usage: moka uninstall <atom>")?;
            uninstall(cfg, query)
        }
        "list" | "installed" => {
            for ip in pkg::installed_packages(&cfg.vdb_dir) {
                println!("{}", ip.atom);
            }
            Ok(())
        }
        "update" | "sync" => {
            repo::ensure_repo(cfg)?;
            util::sh_ok(&format!(
                "git -C {} pull --ff-only",
                util::sq(&cfg.repo_dir.display().to_string())
            ))
        }
        "recipe-gen" => {
            let url = args.get(1).ok_or("usage: moka recipe-gen <git-url> [version]")?;
            let version = args.get(2).map(String::as_str).unwrap_or("1.0");
            cfg.ensure_dirs()?;
            recipe::recipe_gen(cfg, url, version)
        }
        "help" | "-h" | "--help" => {
            usage();
            Ok(())
        }
        other => Err(format!("unknown command `{}`", other)),
    }
}

fn install_pkg(cfg: &Config, p: &pkg::Package) -> R<()> {
    let mut ctx = build::BuildCtx::new(cfg, p)?;
    ctx.build()?;
    let files = ctx.install_into_root()?;
    let rec = pkg::InstalledPkg {
        atom: p.atom.clone(),
        files,
        build_log: ctx.log.clone(),
    };
    pkg::record_install(&cfg.vdb_dir, &rec)?;
    println!("installed {}", p.atom);
    Ok(())
}

fn uninstall(cfg: &Config, query: &str) -> R<()> {
    let inst = pkg::installed_packages(&cfg.vdb_dir);
    let mut matches: Vec<pkg::InstalledPkg> = inst.into_iter().filter(|p| p.atom.matches(query)).collect();
    if matches.is_empty() {
        return Err(format!("`{}` is not installed", query));
    }
    if matches.len() > 1 {
        return Err(format!(
            "ambiguous: {}",
            matches.iter().map(|m| m.atom.to_string()).collect::<Vec<_>>().join(", ")
        ));
    }
    let ip = matches.remove(0);
    for rel in &ip.files {
        let target = cfg.root.join(rel);
        let _ = std::fs::remove_file(&target);
    }
    let dir = cfg.vdb_dir.join(&ip.atom.category).join(format!(
        "{}-{}",
        ip.atom.name, ip.atom.version
    ));
    util::rm_rf(&dir)?;
    println!("uninstalled {}", ip.atom);
    Ok(())
}

fn usage() {
    println!(
        "moka - FunTux package manager

USAGE:
    moka repo-setup <git-url>    clone the recipe tree
    moka search <keyword>        search recipes
    moka info <atom>             show recipe details
    moka build <atom>            build a package (staged, not installed)
    moka install <atom>          build and install a package (deps first)
    moka pkg <atom> [outdir]     build and pack a pacman .pkg.tar.zst
    moka resolve <atom>          print the dependency build order
    moka uninstall <atom>        remove an installed package
    moka list                    list installed packages
    moka update                  pull latest recipe tree
    moka recipe-gen <git-url>    record an interactive build into a recipe"
    );
}
