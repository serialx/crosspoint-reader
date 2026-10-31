use std::io::{BufRead, BufReader};
use std::process::{Command, Stdio};
use xp_core::hyphen::{self, Break};

fn compact_root() -> std::path::PathBuf {
    std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../test/hyphenation_eval")
}

#[test]
fn cpp_language_tags() {
    let tags = std::fs::read_to_string(compact_root().join("golden-tags.txt")).unwrap();
    let mut count = 0;
    for line in tags.lines().filter(|s| !s.starts_with('#')) {
        let (tag, id) = line.split_once(' ').unwrap();
        assert_eq!(
            hyphen::language(if tag == "-" { b"" } else { tag.as_bytes() }),
            id.parse::<i32>().unwrap(),
            "{tag}"
        );
        count += 1;
    }
    assert_eq!(count, 31);
}

#[test]
fn cpp_golden_outputs() {
    let full = std::env::var_os("XP_HYPHEN_GOLDEN_FULL");
    let root = full
        .as_ref()
        .map_or_else(compact_root, std::path::PathBuf::from);
    let words: Vec<Vec<u8>> = std::fs::read_to_string(root.join("golden-words.txt"))
        .unwrap()
        .lines()
        .filter(|s| !s.starts_with('#'))
        .map(|s| {
            if s == "-" {
                return Vec::new();
            }
            assert_eq!(s.len() % 2, 0);
            s.as_bytes()
                .as_chunks::<2>()
                .0
                .iter()
                .map(|c| u8::from_str_radix(std::str::from_utf8(c).unwrap(), 16).unwrap())
                .collect()
        })
        .collect();
    assert!(!words.is_empty());
    let mut gzip = full.as_ref().map(|_| {
        Command::new("gzip")
            .arg("-dc")
            .arg(root.join("golden-breaks.txt.gz"))
            .stdout(Stdio::piped())
            .spawn()
            .expect("Full golden verification requires gzip")
    });
    let file: Box<dyn BufRead> = match gzip.as_mut() {
        Some(child) => Box::new(BufReader::new(child.stdout.take().unwrap())),
        None => Box::new(BufReader::new(
            std::fs::File::open(root.join("golden-breaks.txt")).unwrap(),
        )),
    };
    let mut count = 0;
    for line in file.lines() {
        let line = line.unwrap();
        if line.starts_with('#') {
            continue;
        }
        let mut fields = line.split_whitespace();
        let tag = fields.next().unwrap();
        let fallback = fields.next().unwrap() == "1";
        let index: usize = fields.next().unwrap().parse().unwrap();
        assert_eq!(index, count % words.len());
        let expected: Vec<Break> = fields
            .map(|s| {
                let (offset, insert) = s.split_once(':').unwrap();
                Break {
                    byte_offset: offset.parse().unwrap(),
                    insert_hyphen: insert.parse().unwrap(),
                    pad: [0; 3],
                }
            })
            .collect();
        let mut actual = Vec::new();
        hyphen::breaks(
            hyphen::language(if tag == "-" { b"" } else { tag.as_bytes() }),
            &words[index],
            fallback,
            &mut |b| actual.push(b),
        );
        assert_eq!(
            actual, expected,
            "{tag} {fallback} word {index}: {:?}",
            words[index]
        );
        count += 1;
    }
    assert_eq!(
        count,
        words.len() * 2 * if full.is_some() { 31 } else { 12 }
    );
    if let Some(mut child) = gzip {
        assert!(child.wait().unwrap().success());
    }
}
