use moka::config::Config;
use moka::repo;

fn make_config(label: &str) -> Config {
    let tmp = std::env::temp_dir().join(format!(
        "moka-test-{}-{}",
        std::process::id(),
        label
    ));
    let _ = std::fs::remove_dir_all(&tmp);
    std::fs::create_dir_all(&tmp).unwrap();
    let cfg = Config::from_root(&tmp);

    // Create a hello package: app-misc/hello-1.0
    let pkgdir = cfg.repo_dir.join("app-misc").join("hello-1.0");
    std::fs::create_dir_all(&pkgdir).unwrap();
    std::fs::write(
        pkgdir.join("meta.ebuild"),
        "DESCRIPTION=Prints hello\nLICENSE=GPL-3\n",
    )
    .unwrap();
    std::fs::write(
        pkgdir.join("src_compile.sh"),
        "echo '#!/bin/sh\necho hello' > hello\n",
    )
    .unwrap();
    std::fs::write(
        pkgdir.join("src_install.sh"),
        "install -d \"$DESTDIR/usr/bin\"\ninstall -m 0755 hello \"$DESTDIR/usr/bin/hello\"\n",
    )
    .unwrap();

    cfg
}

#[test]
fn lists_and_searches_packages() {
    let cfg = make_config("search");
    let pkgs = repo::list_packages(&cfg);
    assert_eq!(pkgs.len(), 1);
    assert_eq!(pkgs[0].atom.name, "hello");
    assert_eq!(pkgs[0].atom.category, "app-misc");
    assert_eq!(pkgs[0].atom.version, "1.0");

    let found = repo::find_package(&cfg, "hello").unwrap();
    assert_eq!(found.atom.to_string(), "app-misc/hello-1.0");

    let hits = repo::search_packages(&cfg, "prints");
    assert_eq!(hits.len(), 1);
}

#[test]
fn builds_and_installs_package() {
    let cfg = make_config("build");
    cfg.ensure_dirs().unwrap();

    let pkg = repo::find_package(&cfg, "hello").unwrap();
    let mut ctx = moka::build::BuildCtx::new(&cfg, &pkg).unwrap();
    ctx.build().unwrap();

    let installed = ctx.install_into_root().unwrap();
    assert!(installed.iter().any(|f| f == "usr/bin/hello"));

    let hello = cfg.root.join("usr/bin/hello");
    assert!(hello.is_file(), "expected {} to exist", hello.display());

    // Cleanup
    let _ = std::fs::remove_dir_all(&cfg.root);
}
