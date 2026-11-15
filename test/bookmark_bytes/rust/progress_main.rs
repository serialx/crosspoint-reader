use bookmark_owner_tests::Text;
#[allow(dead_code)]
#[path = "../../../rust/crates/xp-ffi/src/app/epub/progress_mapping/mod.rs"]
mod progress_mapping;
use progress_mapping::{Chapter, Diagnostic, Source};
use std::io::{self, BufRead, Write};

struct Book {
    data: Vec<u8>,
    spine: i32,
    chunk: usize,
}
struct Loan<'a>(&'a Book);
impl Chapter for Loan<'_> {
    fn read(&self, consume: &mut dyn FnMut(&[u8])) -> bool {
        for bytes in self.0.data.chunks(self.0.chunk) {
            consume(bytes);
        }
        true
    }
}
impl Source for Book {
    type Chapter<'a> = Loan<'a>;
    fn spine_count(&self) -> i32 {
        self.spine + 1
    }
    fn chapter(&self, spine: i32) -> Option<Loan<'_>> {
        (spine == self.spine).then_some(Loan(self))
    }
    fn query(&mut self, _: u32, _: i32, _: usize, _: &[u8]) -> i64 {
        unreachable!()
    }
    fn progress(&self, _: i32, _: f32) -> Option<f32> {
        unreachable!()
    }
    fn entity(_: &[u8]) -> i32 {
        unreachable!()
    }
    fn log(&self, _: Diagnostic<'_>) {}
    fn reverse(&mut self, _: i32, _: &mut dyn FnMut(&[u8])) -> bool {
        unreachable!()
    }
}
fn main() {
    let args: Vec<_> = std::env::args().collect();
    assert_eq!(args.len(), 4);
    let book = Book {
        data: std::fs::read(&args[1]).unwrap(),
        spine: args[2].parse().unwrap(),
        chunk: args[3].parse().unwrap(),
    };
    let control = std::env::var("XP_BOOKMARK_XPATH_CONTROL").unwrap_or_default();
    let mut out = io::stdout().lock();
    for line in io::stdin().lock().lines() {
        let line = line.unwrap();
        let (kind, value) = line.split_once(' ').unwrap();
        let (mode, mut value, mut ratio) = match kind {
            "p" => (0, value.parse::<u32>().unwrap() as u16 as u32, 0.0),
            "o" => (1, value.parse().unwrap(), 0.0),
            "g" => (2, 0, value.parse().unwrap()),
            _ => panic!("unknown query"),
        };
        if control == "offset" && mode == 1 {
            value = value.wrapping_add(1);
        }
        if control == "progress" && mode == 2 {
            ratio *= 0.5;
        }
        let result = progress_mapping::resolve(&book, book.spine, mode, value, ratio).unwrap();
        out.write_all(result.bytes()).unwrap();
        out.write_all(b"\n").unwrap();
    }
}
