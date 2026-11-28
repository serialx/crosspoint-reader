extern crate std;
use super::*;
use std::{format, fs, path::PathBuf, process::Command, string::String};
fn root() -> (PathBuf, bool) {
    if let Some(path) = std::env::var_os("CSS_GOLDEN_DIR") {
        return (PathBuf::from(path), false);
    }
    let source = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../test/css_parser");
    let output = std::env::temp_dir().join(format!("xp-css-goldens-{}", std::process::id()));
    let status = Command::new("python3")
        .arg(source.join("unpack_goldens.py"))
        .arg(source.join("goldens"))
        .arg(&output)
        .status()
        .expect("python3 is required to unpack CSS test fixtures");
    assert!(status.success(), "failed to unpack CSS test fixtures");
    (output, true)
}
fn equal(case: &str, kind: &str, got: &[u8], expected: &[u8]) {
    assert_eq!(got.len(), expected.len(), "{case} {kind} length");
    if let Some(i) = got.iter().zip(expected).position(|(a, b)| a != b) {
        panic!(
            "{case} {kind}: byte {i} (style offset {}): {} != {}",
            i % WIRE_BYTES,
            got[i],
            expected[i]
        );
    }
}
#[test]
fn cpp_goldens() {
    let (root, temporary) = root();
    for name in fs::read_to_string(root.join("cases.txt")).unwrap().lines() {
        let dir = root.join(name);
        let mut store = Store::default();
        let mut metadata = String::new();
        let mut complete = true;
        for i in 0..100 {
            let path = dir.join(format!("{i:02}.css"));
            if !path.exists() {
                break;
            }
            let input = fs::read(path).unwrap();
            let mut stream = Stream::default();
            stream.prepare().unwrap();
            for c in input.chunks(512) {
                stream.feed(&mut store, c);
            }
            let result = stream.finish(&mut store);
            complete &= result == ParseResult::Complete;
            metadata.push_str(&format!("{} {}\n", result as u8, store.rule_count()));
        }
        metadata.push_str(&format!("{}\n", u8::from(store.has_first_letter())));
        assert_eq!(
            metadata,
            fs::read_to_string(dir.join("result.txt")).unwrap(),
            "{name}"
        );
        let mut cache = Vec::new();
        cache.extend_from_slice(&[19, u8::from(!complete)]);
        cache.extend_from_slice(&(store.rule_count() as u16).to_le_bytes());
        let mut order = alloc::vec![0; store.rule_count()];
        store.cache_order(&mut order).unwrap();
        let mut record = [0; 2 + 257 + WIRE_BYTES];
        for index in order {
            let n = store.cache_record(index, &mut record).unwrap();
            cache.extend_from_slice(&record[..n]);
        }
        equal(
            name,
            "cache",
            &cache,
            &fs::read(dir.join("cache.bin")).unwrap(),
        );
        let mut loaded = Store::default();
        loaded.cache_begin(store.rule_count()).unwrap();
        let mut pos = 4;
        while pos < cache.len() {
            let len = u16::from_le_bytes([cache[pos], cache[pos + 1]]) as usize;
            pos += 2;
            loaded
                .cache_insert(
                    &cache[pos..pos + len],
                    &cache[pos + len..pos + len + WIRE_BYTES],
                )
                .unwrap();
            pos += len + WIRE_BYTES;
        }
        for instance in [&store, &loaded] {
            let mut resolved = Vec::new();
            for line in fs::read_to_string(dir.join("queries.tsv")).unwrap().lines() {
                let fields: Vec<_> = line.split('\t').collect();
                let mut ancestors = Vec::new();
                if !fields[3].is_empty() {
                    for a in fields[3].split('|') {
                        let p: Vec<_> = a.split('~').collect();
                        ancestors.push(ancestor(p[0].as_bytes(), p[1].as_bytes(), p[2].as_bytes()));
                    }
                }
                for first in [false, true] {
                    resolved.extend_from_slice(
                        &instance
                            .resolve(
                                fields[0].as_bytes(),
                                fields[1].as_bytes(),
                                fields[2].as_bytes(),
                                &ancestors,
                                first,
                            )
                            .0,
                    );
                }
            }
            equal(
                name,
                "resolved",
                &resolved,
                &fs::read(dir.join("resolved.bin")).unwrap(),
            );
        }
        let mut inlines = Vec::new();
        for line in fs::read_to_string(dir.join("inline.txt")).unwrap().lines() {
            inlines.extend_from_slice(&inline(line.as_bytes()).0);
        }
        equal(
            name,
            "inline",
            &inlines,
            &fs::read(dir.join("inline.bin")).unwrap(),
        );
    }
    if temporary {
        fs::remove_dir_all(root).unwrap();
    }
}

#[test]
fn chunk_boundaries() {
    let source=b"/*comment*/div > p.a::first-letter{font-size:150%;float:left} .a{border:thin solid; margin:1px 2em}/*end*/";
    let mut expected = None;
    for size in 1..=source.len() {
        let mut store = Store::default();
        let mut stream = Stream::default();
        stream.prepare().unwrap();
        for bytes in source.chunks(size) {
            stream.feed(&mut store, bytes);
        }
        assert_eq!(stream.finish(&mut store), ParseResult::Complete);
        let result = store.resolve(b"p", b"a", b"", &[ancestor(b"div", b"", b"")], true);
        if let Some(e) = expected {
            assert!(result == e);
        } else {
            expected = Some(result);
        }
    }
}
