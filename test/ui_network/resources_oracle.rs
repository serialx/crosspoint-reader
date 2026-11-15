use std::{
    fs,
    path::PathBuf,
    process::Command,
    time::{SystemTime, UNIX_EPOCH},
};
use xp_ui::network::wifi_resources::{sort_rows, text, Buffer, Global, Row, SmallZ};

struct Scratch(PathBuf);
impl Drop for Scratch {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}
fn decode(hex: &str) -> Vec<u8> {
    if hex == "-" {
        return Vec::new();
    }
    hex.as_bytes()
        .as_chunks::<2>()
        .0
        .iter()
        .map(|b| u8::from_str_radix(std::str::from_utf8(b).unwrap(), 16).unwrap())
        .collect()
}
fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}
fn width(bytes: &[u8], mode: u8) -> i32 {
    bytes
        .iter()
        .take_while(|b| **b != 0)
        .map(|b| {
            if mode == 0 {
                1
            } else {
                match *b {
                    b' ' => 3,
                    b'W' => 9,
                    0x80..=0xbf => 0,
                    0xc0..=0xff => 11,
                    _ => 6,
                }
            }
        })
        .sum()
}
#[test]
fn frozen_native_text_matches_prepared_rust_text_and_scan_fixtures() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../test/ui_network");
    let fixtures = Command::new("python3")
        .arg(root.join("resources_fixtures.py"))
        .output()
        .unwrap();
    assert!(
        fixtures.status.success(),
        "{}",
        String::from_utf8_lossy(&fixtures.stderr)
    );
    let fixtures = String::from_utf8(fixtures.stdout).unwrap();
    let mut rows = Buffer::<Row>::new();
    for line in fixtures.lines() {
        let fields: Vec<_> = line.split_whitespace().collect();
        if fields[0] == "row" {
            let name = decode(fields[1]);
            if name.is_empty() {
                continue;
            }
            let rssi = fields[2].parse().unwrap();
            if let Some(found) = rows.as_mut_slice().iter_mut().find(|r| r.ssid() == name) {
                if rssi > found.rssi() {
                    found.replace_signal(rssi, fields[3] == "1");
                }
            } else {
                rows.try_push(
                    Row::scan(
                        &name,
                        rssi,
                        fields[3] == "1",
                        fields[4] == "1",
                        fields[5].parse().unwrap(),
                    )
                    .unwrap(),
                )
                .unwrap();
            }
        } else if fields[0] == "sizes" {
            for count in fields[1..].iter().map(|s| s.parse::<u16>().unwrap()) {
                let mut ties = Buffer::<Row>::new();
                ties.try_reserve(usize::from(count)).unwrap();
                for i in (0..count).rev() {
                    ties.try_push(Row::scan(b"tie", -50, false, true, i).unwrap())
                        .unwrap();
                }
                sort_rows(ties.as_mut_slice());
                assert!(ties
                    .as_slice()
                    .iter()
                    .enumerate()
                    .all(|(i, r)| usize::from(r.scan_index()) == i));
            }
        }
    }
    sort_rows(rows.as_mut_slice());
    for row in rows.as_mut_slice() {
        row.rebuild_status();
    }
    rows.try_push(Row::hidden()).unwrap();
    assert_eq!(rows.as_slice().len(), 3);
    assert_eq!(rows.as_slice()[0].ssid(), b"alpha");
    assert_eq!(&rows.as_slice()[0].status(), b"+   ||\0\0\0");
    assert_eq!(rows.as_slice()[1].ssid(), b"zeta");
    assert_eq!(rows.as_slice()[1].scan_index(), 0);
    assert_eq!(rows.as_slice()[1].rssi(), -40);
    assert!(!rows.as_slice()[1].encrypted());
    assert!(rows.as_slice()[2].is_hidden());

    let directory = std::env::temp_dir().join(format!(
        "xp-wifi-oracle-{}-{}",
        std::process::id(),
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    fs::create_dir(&directory).unwrap();
    let scratch = Scratch(directory);
    let native = include_str!("native_oracle/wifi_truncated_text.txt");
    let template = r#"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
namespace EpdFontFamily { enum Style { REGULAR }; }
struct GfxRenderer {
  int mode;
  int getTextWidth(int, const char* text, EpdFontFamily::Style) const {
    int n=0;
    for(const unsigned char* p=reinterpret_cast<const unsigned char*>(text);*p;++p)
      n+=mode==0?1:(*p==' '?3:*p=='W'?9:*p>=0xc0?11:*p>=0x80?0:6);
    return n;
  }
  std::string truncatedText(int, const char*, int, EpdFontFamily::Style) const;
};
// NATIVE_TRUNCATION
int main(int, char** argv) {
  std::ifstream input(argv[1]); int mode, width; std::string encoded;
  while(input>>mode>>width>>encoded) {
    std::string text;
    if(encoded!="-") for(size_t i=0;i<encoded.size();i+=2)
      text.push_back(static_cast<char>(std::stoi(encoded.substr(i,2),nullptr,16)));
    auto result=GfxRenderer{mode}.truncatedText(0,text.c_str(),width,EpdFontFamily::REGULAR);
    constexpr char digits[]="0123456789abcdef";
    for(unsigned char b:result) std::cout<<digits[b>>4]<<digits[b&15];
    std::cout<<'\n';
  }
}
"#;
    fs::write(
        scratch.0.join("native.cpp"),
        template.replace("// NATIVE_TRUNCATION", native),
    )
    .unwrap();
    let compile = Command::new("c++")
        .args(["-std=c++17", "-O2"])
        .arg(scratch.0.join("native.cpp"))
        .arg("-o")
        .arg(scratch.0.join("native"))
        .output()
        .unwrap();
    assert!(
        compile.status.success(),
        "{}",
        String::from_utf8_lossy(&compile.stderr)
    );
    let strings: &[&[u8]] = &[
        b"",
        b"a",
        b"abcdef",
        b"WW WW",
        "éabcd".as_bytes(),
        "雪とnetwork".as_bytes(),
        b"a\0ignored",
        b"\x80\x81abc",
        b"a\xc0\x80z",
        b"a\xff\xbf",
        &[b'w'; 1024],
    ];
    let mut input = String::new();
    let mut expected = Vec::new();
    for bytes in strings {
        for mode in 0..2 {
            for limit in [-1, 0, 1, 2, 3, 4, 5, 6, 7, 12, 18, 32, 64] {
                input.push_str(&format!(
                    "{mode} {limit} {}\n",
                    if bytes.is_empty() {
                        "-".into()
                    } else {
                        hex(bytes)
                    }
                ));
                let mut out = SmallZ::<Global>::new();
                out.try_reserve(bytes.len() + 3).unwrap();
                let split = bytes.len() / 2;
                text::truncate_joined(&mut out, &bytes[..split], &bytes[split..], limit, |z| {
                    width(z, mode)
                })
                .unwrap();
                expected.push(hex(out.as_bytes()));
            }
        }
    }
    fs::write(scratch.0.join("input.txt"), input).unwrap();
    let output = Command::new(scratch.0.join("native"))
        .arg(scratch.0.join("input.txt"))
        .output()
        .unwrap();
    assert!(output.status.success());
    let actual = String::from_utf8(output.stdout).unwrap();
    let actual: Vec<_> = actual.lines().collect();
    assert_eq!(actual.len(), expected.len());
    for (case, (actual, expected)) in actual.iter().zip(expected).enumerate() {
        assert_eq!(*actual, expected, "native text case {case}");
    }
}
