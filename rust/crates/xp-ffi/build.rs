//! Check the committed C ABI constants on every Cargo build.
#[path = "src/book_limits.rs"]
mod book;
#[path = "../xp-core/src/css/limits.rs"]
mod css;

fn main() {
    let header = "include/xp_book_limits.h";
    let mut output = String::from(
        "// Generated from Rust constants by xp-ffi/build.rs; do not edit.\n\
         // Regenerate from rust/crates/xp-ffi:\n\
         // rustc build.rs -o /tmp/xp-book-limits && /tmp/xp-book-limits --write-header\n\
         #pragma once\n",
    );
    for (name, value) in [
        ("XP_BOOK_METADATA_WINDOW", book::METADATA_WINDOW as usize),
        ("XP_CSS_READ_BUFFER_BYTES", css::READ_BUFFER_BYTES),
        ("XP_CSS_CACHE_VERSION", css::CACHE_VERSION as usize),
        ("XP_CSS_WIRE_BYTES", css::WIRE_BYTES),
        ("XP_CSS_MAX_RULES", css::MAX_RULES),
        ("XP_CSS_SELECTOR_CAP", css::SELECTOR_CAP),
        ("XP_CSS_MAX_SELECTOR_BYTES", css::MAX_SELECTOR_BYTES),
        ("XP_CSS_MAX_CACHE_KEY_BYTES", css::MAX_CACHE_KEY_BYTES),
        ("XP_CSS_CACHE_BUFFER_BYTES", css::CACHE_BUFFER_BYTES),
    ] {
        output.push_str(&format!("#define {name} {value}\n"));
    }
    if std::env::args().any(|arg| arg == "--write-header") {
        std::fs::write(header, output).expect("write book ABI constants");
    } else {
        assert_eq!(
            std::fs::read_to_string(header).expect("read book ABI constants"),
            output,
            "Regenerate include/xp_book_limits.h with the command in its header",
        );
    }
    for path in [header, "src/book_limits.rs", "../xp-core/src/css/limits.rs"] {
        println!("cargo:rerun-if-changed={path}");
    }
}
