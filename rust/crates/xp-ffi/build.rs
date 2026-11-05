//! Expand the C++ X-macro contract into Rust constants at build time.
use std::{env, fmt::Write, fs, path::PathBuf};

fn generate_plugin_contract() {
    let source = "include/xp_plugin_contract.def";
    println!("cargo:rerun-if-changed={source}");
    let input = fs::read_to_string(source).expect("read plugin contract");
    let mut events = Vec::new();
    let mut constants = String::new();
    for line in input.lines().map(str::trim) {
        if line.is_empty() || line.starts_with("//") {
            continue;
        }
        let (kind, fields) = line.split_once('(').expect("contract record");
        let fields: Vec<_> = fields
            .strip_suffix(')')
            .expect("contract closing parenthesis")
            .split(',')
            .map(str::trim)
            .collect();
        match kind {
            "XP_PLUGIN_EVENT" => {
                assert_eq!(fields.len(), 3);
                let id: usize = fields[1].parse().expect("event ID");
                assert_eq!(id, events.len(), "event IDs must be contiguous");
                assert!(id < 8, "event masks are eight bits");
                assert!(fields[2].starts_with('"') && fields[2].ends_with('"'));
                events.push((fields[0], id, fields[2].trim_matches('"')));
            }
            "XP_PLUGIN_LIMIT" => {
                assert_eq!(fields.len(), 2);
                let value: usize = fields[1].parse().expect("plugin limit");
                writeln!(constants, "const {}: usize = {value};", fields[0]).unwrap();
            }
            _ => panic!("unknown plugin contract record: {kind}"),
        }
    }
    let mut output = String::from("#[repr(u32)]\nenum Event {\n");
    for (name, id, _) in &events {
        writeln!(output, "{name} = {id},").unwrap();
    }
    output.push_str("}\n");
    for (name, id, _) in &events {
        writeln!(
            output,
            "const _: () = assert!(Event::{name} as usize == {id});"
        )
        .unwrap();
    }
    writeln!(output, "const EVENTS: [&[u8]; {}] = [", events.len()).unwrap();
    for (_, _, wire) in &events {
        writeln!(output, "b\"{wire}\\0\",").unwrap();
    }
    output.push_str("];\n");
    output.push_str(&constants);
    // The JSON algorithm uses slots 0..3; changing the adapter alone is invalid.
    output.push_str("const _: () = assert!(JSON_SLOTS == 4);\n");
    let out = PathBuf::from(env::var_os("OUT_DIR").expect("Cargo OUT_DIR"));
    fs::write(out.join("plugin_contract.rs"), output).expect("write plugin contract");
}

#[path = "src/book_limits.rs"]
mod book;
#[path = "../xp-core/src/css/limits.rs"]
mod css;

fn check_book_constants() {
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

fn main() {
    check_book_constants();
    if std::env::args().any(|arg| arg == "--write-header") {
        return;
    }
    generate_plugin_contract();
}
