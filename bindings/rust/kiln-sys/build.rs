//! Builds kiln with CMake (the host build in `$KILN_BUILD_DIR`, default `<kiln>/build/host`),
//! links what a C app links (taken from the notes-c sample's link line), and runs bindgen on
//! the headers.
use std::path::{Path, PathBuf};
use std::process::Command;

/// The Android ABI for a Rust target, if it is one.
fn android_abi(target: &str) -> Option<&'static str> {
    match target {
        "aarch64-linux-android" => Some("arm64-v8a"),
        "x86_64-linux-android" => Some("x86_64"),
        _ => None,
    }
}

fn main() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../..").canonicalize().unwrap();
    let target = std::env::var("TARGET").unwrap();
    println!("cargo:rerun-if-env-changed=KILN_BUILD_DIR");
    println!("cargo:rerun-if-env-changed=KILN_NDK");
    println!("cargo:rerun-if-changed={}", root.join("include/kiln/kiln.h").display());
    println!("cargo:rerun-if-changed={}", root.join("include/kiln/kiln_m3.h").display());
    // kiln's sources: CMake rebuilds what changed.
    for dir in ["core/src", "render", "foundation/src", "material3/src", "platform"] {
        println!("cargo:rerun-if-changed={}", root.join(dir).display());
    }

    let mut clang_args = vec![format!("-I{}", root.join("include").display())];
    if let Some(abi) = android_abi(&target) {
        // Android: kiln's NDK build for the ABI (tools/build-android.sh configures it).
        let build = std::env::var("KILN_BUILD_DIR").map(PathBuf::from).unwrap_or_else(|_| root.join("build/android").join(abi));
        if !build.join("build.ninja").exists() {
            run(Command::new(root.join("tools/build-android.sh")).arg(abi));
        }
        run(Command::new("cmake").arg("--build").arg(&build)
            .args(["--target", "kiln_android", "kiln_material3", "-j", "8"]));
        link_android(&build);
        let ndk = std::env::var("KILN_NDK").unwrap_or_else(|_| "/opt/android-ndk".into());
        clang_args.push(format!("--target={target}31"));   // minSdk 31
        clang_args.push(format!("--sysroot={ndk}/toolchains/llvm/prebuilt/linux-x86_64/sysroot"));
    } else {
        let build = std::env::var("KILN_BUILD_DIR").map(PathBuf::from).unwrap_or_else(|_| root.join("build/host"));
        if !build.join("build.ninja").exists() {
            run(Command::new("cmake").args(["-S"]).arg(&root).arg("-B").arg(&build).args(["-G", "Ninja"]));
        }
        run(Command::new("cmake").arg("--build").arg(&build).args(["--target", "notes-c", "-j", "8"]));
        link(&build);
    }

    let bindings = bindgen::Builder::default()
        .header("wrapper.h")
        .clang_args(clang_args)
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

/// Android: the archives a kiln app links (as tools/build-aar merges them), whole where only
/// Java reaches them, then libc++ and the platform libraries.
fn link_android(build: &Path) {
    println!("cargo:rustc-link-search=native={}", build.display());
    for (name, whole) in [("kiln_android", true), ("kiln_material3", true), ("kiln_foundation", false), ("kiln_core", false),
                          ("mcu", false), ("hearth", false), ("hearth_vulkan_deps", false), ("clipper2", false), ("skia_shadow", false)] {
        println!("cargo:rerun-if-changed={}", build.join(format!("lib{name}.a")).display());
        let kind = if whole { "static:+whole-archive,-bundle" } else { "static:-bundle" };
        println!("cargo:rustc-link-lib={kind}={name}");
    }
    for lib in ["c++_static", "c++abi"] {
        println!("cargo:rustc-link-lib=static:-bundle={lib}");
    }
    for lib in ["android", "jnigraphics", "log", "vulkan", "m"] {
        println!("cargo:rustc-link-lib=dylib={lib}");
    }
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
