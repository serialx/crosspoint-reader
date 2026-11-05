use crate::chapters::Window;
use std::collections::VecDeque;
use std::fmt::Write;

pub fn compare_input(input: &str) -> String {
    let mut owner = Box::<Window>::new_uninit();
    let mut owner = unsafe {
        Window::initialize(owner.as_mut_ptr());
        owner.assume_init()
    };
    let mut total = 0;
    let mut start = 0;
    let mut prewarms = 0;
    let mut rows = VecDeque::new();
    let mut output = String::new();
    for line in input.lines() {
        let fields: Vec<_> = line.split_whitespace().collect();
        match fields[0] {
            "W" => {
                total = fields[1].parse().unwrap();
                start = fields[2].parse().unwrap();
            }
            "R" => {
                let index: i32 = fields[1].parse().unwrap();
                let level: usize = fields[2].parse().unwrap();
                let bytes = if fields[3] == "-" {
                    Vec::new()
                } else {
                    fields[3]
                        .as_bytes()
                        .as_chunks::<2>()
                        .0
                        .iter()
                        .map(|digits| {
                            u8::from_str_radix(std::str::from_utf8(digits).unwrap(), 16).unwrap()
                        })
                        .collect()
                };
                rows.push_back((index, level, bytes));
            }
            "E" => {
                let previous = owner.first();
                assert!(owner.refresh(total, start, |index, text| {
                    let (expected, level, title) = rows.pop_front().expect("native TOC read");
                    assert_eq!(index, expected);
                    text.assign_parts(&[&vec![b' '; level.saturating_sub(1) * 2], &title])
                        .is_ok()
                }));
                assert!(rows.is_empty(), "Rust skipped native TOC reads");
                if owner.first() != previous {
                    prewarms += 1;
                }
                writeln!(
                    output,
                    "E {} {}",
                    owner.first().unwrap(),
                    owner.labels().len()
                )
                .unwrap();
                for (index, row) in owner.labels().iter().enumerate() {
                    let mut hex = String::new();
                    for byte in row.bytes() {
                        write!(hex, "{byte:02x}").unwrap();
                    }
                    if hex.is_empty() {
                        hex.push('-');
                    }
                    writeln!(output, "L {index} {hex}").unwrap();
                }
                writeln!(output, "P {prewarms}").unwrap();
            }
            "L" | "P" => {}
            other => panic!("unexpected native trace {other}"),
        }
    }
    output
}
