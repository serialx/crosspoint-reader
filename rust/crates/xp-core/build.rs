use std::{env, path::Path, process::Command};

fn main() {
    let manifest = env::var("CARGO_MANIFEST_DIR").unwrap();
    let root = Path::new(&manifest).join("../../..");
    let script = root.join("scripts/gen_i18n.py");
    let translations = root.join("lib/I18n/translations");
    println!("cargo:rerun-if-changed={}", script.display());
    println!("cargo:rerun-if-changed={}", translations.display());
    println!("cargo:rerun-if-env-changed=XP_I18N_BUILTIN_LANGS");
    println!("cargo:rerun-if-env-changed=XP_I18N_STRIP_UNUSED");
    println!("cargo:rerun-if-env-changed=PYTHON");
    let mut command = Command::new(env::var("PYTHON").unwrap_or_else(|_| "python3".into()));
    command
        .arg(script)
        .arg(translations)
        .arg(env::var("OUT_DIR").unwrap())
        .arg("--builtin-langs")
        .arg(env::var("XP_I18N_BUILTIN_LANGS").unwrap_or_else(|_| "all".into()))
        .arg("--src-dirs")
        .arg(root.join("src"))
        .arg(root.join("lib"));
    if env::var("XP_I18N_STRIP_UNUSED").as_deref() == Ok("1") {
        command.arg("--strip-unused");
        println!("cargo:rerun-if-changed={}", root.join("src").display());
        println!("cargo:rerun-if-changed={}", root.join("lib").display());
    }
    let output = command.output().expect("run i18n generator");
    assert!(
        output.status.success(),
        "i18n generation failed: {}{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
}
