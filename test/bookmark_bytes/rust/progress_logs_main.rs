use bookmark_owner_tests::Text;
#[allow(dead_code)]
#[path = "../../../rust/crates/xp-ffi/src/app/epub/progress_mapping/mod.rs"]
mod progress_mapping;
use progress_mapping::{diagnostics, Diagnostic};
use std::io::{self, BufRead};
use xp_core::sync::mapping::{Event, Position, Span};

fn main() {
    let mutate = std::env::var_os("XP_BOOKMARK_DIAG_CONTROL").is_some();
    for line in io::stdin().lock().lines() {
        let line = line.unwrap();
        let parts: Vec<_> = line.split_whitespace().collect();
        let kind = parts[0];
        let id: u32 = parts[1].parse().unwrap();
        let ratio = f32::from_bits(parts[2].parse().unwrap());
        let flag: u32 = parts[3].parse().unwrap();
        let text: &[u8] = match flag {
            0 => b"",
            1 => b"div",
            2 => "한글-ancêtre".as_bytes(),
            _ => b"before\0after",
        };
        let mut p = Position {
            spine: 2,
            page: 3,
            pages: 11,
            offset: 17,
            paragraph: 4,
            li: 2,
            has_li: (flag % 2) as u8,
            ..Default::default()
        };
        p.anchor[..text.len()].copy_from_slice(text);
        let event = Event {
            id,
            a: 4,
            b: if flag.is_multiple_of(2) { -1 } else { 5 },
            c: 7,
            d: 8,
            ratio,
            target: 31,
            total: 97,
            text: Span::new(text),
        };
        let diagnostic = match kind {
            "M" => Diagnostic::Map {
                position: &p,
                event: &event,
                text,
            },
            "S" => Diagnostic::Saved {
                position: &p,
                percentage: ratio,
                path: text,
            },
            "X" => Diagnostic::Resolved {
                mode: id,
                spine: 2,
                value: 4,
                ratio,
                path: text,
            },
            "E" => Diagnostic::Xml {
                code: 2,
                final_input: flag != 0,
            },
            "A" => Diagnostic::ParserAllocation,
            _ => panic!("unknown diagnostic"),
        };
        diagnostics::emit(
            diagnostic,
            |_| "syntax error",
            |severity, origin, message| {
                print!("{}:", severity ^ u8::from(mutate));
                for byte in origin {
                    print!("{byte:02x}");
                }
                print!(":");
                for byte in message {
                    print!("{byte:02x}");
                }
                println!();
            },
        );
    }
}
