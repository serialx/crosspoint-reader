use std::{env, fs, path::PathBuf};
fn main() {
    let root = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap()).join("../..");
    let source = root.join("rust/crates/xp-ffi/src/app/epub/fonts.rs");
    println!("cargo:rerun-if-changed={}", source.display());
    let source = fs::read_to_string(source).unwrap();
    let start = source.find("    pub(super) unsafe fn settings(").unwrap();
    let end = source[start..]
        .find("\n    pub(super) unsafe fn font_variant(")
        .unwrap()
        + start;
    let out = PathBuf::from(env::var("OUT_DIR").unwrap());
    fs::write(
        out.join("settings_method.rs"),
        format!("impl XpEpubLeaves {{\n{}\n}}", &source[start..end]),
    )
    .unwrap();
    let header = root.join("src/fontIds.h");
    println!("cargo:rerun-if-changed={}", header.display());
    let header = fs::read_to_string(header).unwrap();
    let ids = [12, 14, 16, 18].map(|size| {
        let prefix = format!("#define NOTOSANS_{size}_FONT_ID (");
        header
            .lines()
            .find_map(|line| line.strip_prefix(&prefix))
            .unwrap()
            .trim_end_matches(')')
            .parse::<i32>()
            .unwrap()
    });
    fs::write(
        out.join("builtin_font_ids.rs"),
        format!("pub const BUILTIN_FONT_IDS:[i32;4]={ids:?};"),
    )
    .unwrap();
}
