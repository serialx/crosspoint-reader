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
    let mut output = String::from("#[repr(u32)]\npub(crate) enum Event {\n");
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

fn generate_bidi_workspace_contract() {
    let header = "include/xp_ffi.h";
    println!("cargo:rerun-if-changed={header}");
    let source = fs::read_to_string(header).expect("read bidi workspace contract");
    let mut output = String::new();
    for name in ["XP_BIDI_WORKSPACE_SIZE", "XP_BIDI_WORKSPACE_ALIGNMENT"] {
        let prefix = format!("#define {name} ");
        let value: usize = source
            .lines()
            .find_map(|line| line.strip_prefix(&prefix))
            .expect("bidi workspace constant")
            .parse()
            .expect("bidi workspace integer");
        writeln!(output, "const {name}: usize = {value};").unwrap();
    }
    let out = PathBuf::from(env::var_os("OUT_DIR").expect("Cargo OUT_DIR"));
    fs::write(out.join("bidi_workspace.rs"), output).expect("write bidi workspace contract");
}

fn generate_builtin_font_ids() {
    let header = "../../../src/fontIds.h";
    println!("cargo:rerun-if-changed={header}");
    let source = fs::read_to_string(header).expect("read builtin font IDs");
    let ids: Vec<i32> = [12, 14, 16, 18]
        .iter()
        .map(|size| {
            let prefix = format!("#define NOTOSANS_{size}_FONT_ID (");
            source
                .lines()
                .find_map(|line| line.strip_prefix(&prefix))
                .and_then(|value| value.strip_suffix(')'))
                .expect("builtin font ID")
                .parse()
                .expect("font ID integer")
        })
        .collect();
    let out = PathBuf::from(env::var_os("OUT_DIR").expect("Cargo OUT_DIR"));
    fs::write(
        out.join("builtin_font_ids.rs"),
        format!("const BUILTIN_FONT_IDS: [i32; 4] = {ids:?};"),
    )
    .expect("write builtin font IDs");
}

fn main() {
    check_book_constants();
    if std::env::args().any(|arg| arg == "--write-header") {
        return;
    }
    generate_plugin_contract();
    generate_bidi_workspace_contract();
    generate_builtin_font_ids();
}
