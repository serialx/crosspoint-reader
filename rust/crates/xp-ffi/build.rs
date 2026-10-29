//! Expand the C++ X-macro contract into Rust constants at build time.
use std::{env, fmt::Write, fs, path::PathBuf};

fn main() {
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
