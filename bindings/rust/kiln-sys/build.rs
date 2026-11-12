//! Builds kiln with CMake (the host build in `$KILN_BUILD_DIR`, default `<kiln>/build/host`),
//! links what a C app links (taken from the notes-c sample's link line), and runs bindgen on
//! the headers.
use std::path::{Path, PathBuf};
use std::process::Command;

fn main() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../..").canonicalize().unwrap();
    let build = std::env::var("KILN_BUILD_DIR").map(PathBuf::from).unwrap_or_else(|_| root.join("build/host"));
    println!("cargo:rerun-if-env-changed=KILN_BUILD_DIR");
    println!("cargo:rerun-if-changed={}", root.join("include/kiln/kiln.h").display());
    println!("cargo:rerun-if-changed={}", root.join("include/kiln/kiln_m3.h").display());

    if !build.join("build.ninja").exists() {
        run(Command::new("cmake").args(["-S"]).arg(&root).arg("-B").arg(&build).args(["-G", "Ninja"]));
    }
    run(Command::new("cmake").arg("--build").arg(&build).args(["--target", "notes-c", "-j", "8"]));
    link(&build);

    let bindings = bindgen::Builder::default()
        .header("wrapper.h")
        .clang_arg(format!("-I{}", root.join("include").display()))
        .allowlist_function("kiln_.*")
        .allowlist_type("kiln_.*")
        .allowlist_var("KILN_.*")
        .rustified_non_exhaustive_enum("kiln_.*")
        .derive_default(true)
        .layout_tests(false)
        .generate()
        .expect("bindgen failed on kiln_m3.h");
    let out = PathBuf::from(std::env::var("OUT_DIR").unwrap());
    bindings.write_to_file(out.join("bindings.rs")).unwrap();
}

fn run(cmd: &mut Command) {
    let status = cmd.status().expect("running cmake");
    assert!(status.success(), "{cmd:?} failed");
}

/// The libraries of the notes-c sample's link command, in order.
fn link(build: &Path) {
    let out = Command::new("ninja").arg("-C").arg(build).args(["-t", "commands", "notes-c"]).output().expect("ninja");
    let text = String::from_utf8_lossy(&out.stdout);
    let line = text.lines().last().expect("no link command");
    let mut whole = false;
    let mut after_output = false;
    println!("cargo:rustc-link-search=native={}", build.display());
    for tok in line.split_whitespace() {
        if tok == "notes-c" {
            after_output = true;
            continue;
        }
        if !after_output || tok == "&&" || tok == ":" {
            continue;
        }
        match tok {
            "-Wl,--push-state,--whole-archive" => whole = true,
            "-Wl,--pop-state" => whole = false,
            _ if tok.ends_with(".a") => {
                let path = if Path::new(tok).is_absolute() { PathBuf::from(tok) } else { build.join(tok) };
                println!("cargo:rustc-link-search=native={}", path.parent().unwrap().display());
                println!("cargo:rerun-if-changed={}", path.display());   // relink when kiln changes
                let name = path.file_name().unwrap().to_str().unwrap().trim_start_matches("lib").trim_end_matches(".a").to_string();
                let kind = if whole { "static:+whole-archive,-bundle" } else { "static:-bundle" };
                println!("cargo:rustc-link-lib={kind}={name}");
            }
            _ if tok.contains(".so") => {
                let path = Path::new(tok);
                println!("cargo:rustc-link-search=native={}", path.parent().unwrap().display());
                let file = path.file_name().unwrap().to_str().unwrap();
                let name = file.trim_start_matches("lib").split(".so").next().unwrap();
                println!("cargo:rustc-link-lib=dylib={name}");
            }
            _ => {}
        }
    }
    println!("cargo:rustc-link-lib=dylib=stdc++");
}
