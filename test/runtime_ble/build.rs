use std::{env, fs, path::PathBuf};
fn main() {
    let source = "../../rust/crates/xp-ffi/src/ble_runtime.rs";
    println!("cargo:rerun-if-changed={source}");
    let text = fs::read_to_string(source).unwrap();
    let start = text.find("unsafe fn hold(held: bool) {").unwrap();
    let body = &text[start..];
    let mut depth = 0;
    let end = body
        .char_indices()
        .find_map(|(i, c)| {
            if c == '{' {
                depth += 1;
            }
            if c == '}' {
                depth -= 1;
                if depth == 0 {
                    return Some(i + 1);
                }
            }
            None
        })
        .unwrap();
    let out = PathBuf::from(env::var_os("OUT_DIR").unwrap()).join("hold.rs");
    fs::write(out, &body[..end]).unwrap();
}
