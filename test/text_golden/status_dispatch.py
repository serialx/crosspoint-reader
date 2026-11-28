"""Compile the actual status dispatch without app-test; pixel tests cover its composer."""
from pathlib import Path
import argparse
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('root', type=Path)
a = p.parse_args()
src = (a.root / 'rust/crates/xp-ffi/src/app/epub/view_values.rs').read_text()
def method(name):
    start = src.index('    fn ' + name + '(')
    brace = src.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (src[end] == '{') - (src[end] == '}')
        end += 1
    return src[start:end]
code = r'''
#![allow(dead_code)]
use std::{cell::{Cell, RefCell}, marker::PhantomData, mem::MaybeUninit};
#[derive(Clone, Copy, Default, Debug, PartialEq)]
struct Facts { marker: u32 }
mod app_abi {
    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct Text(pub &'static [u8]);
}
mod abi {
    use super::app_abi::Text;
    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct XpEpubStatus {
        pub title: Text, pub bookmarked: u8, pub building: u8,
        pub page: i32, pub pages: i32, pub book_progress: f32, pub y_offset: i32,
    }
}
thread_local! {
    static READS: Cell<usize> = const { Cell::new(0) };
    static PAINT: RefCell<Vec<(Facts, abi::XpEpubStatus)>> = const { RefCell::new(Vec::new()) };
}
mod status_paint {
    pub unsafe fn paint(f: &super::Facts, value: &super::abi::XpEpubStatus) {
        super::PAINT.with(|p| p.borrow_mut().push((*f, *value)));
    }
}
unsafe extern "C" fn facts(out: *mut Facts) {
    READS.with(|n| n.set(n.get()+1));
    out.write(Facts { marker: 0x1357 });
}
struct Leaves { status_facts: unsafe extern "C" fn(*mut Facts) }
struct ResourceAccess<'a>(PhantomData<&'a ()>);
struct State { bookmarked: bool }
struct ReaderSection;
impl ReaderSection { unsafe fn building(section: bool) -> bool { section } }
mod dispatch {
    use super::*;
    struct Session { leaves: Leaves, section_value: Option<bool>, bookmarked: bool }
    impl Session {
        fn model<T>(&self, f: impl FnOnce(&State) -> T) -> T { f(&State { bookmarked: self.bookmarked }) }
        fn section(&self, _: &ResourceAccess<'_>) -> Option<bool> { self.section_value }
        METHODS
    }
    #[test]
    fn shipping_facts_and_direct_paint_preserve_values() {
        for (section, bookmark) in [(None, false), (Some(false), true), (Some(true), false)] {
            READS.with(|n| n.set(0)); PAINT.with(|p| p.borrow_mut().clear());
            let session = Session { leaves: Leaves { status_facts: facts }, section_value: section, bookmarked: bookmark };
            let value = abi::XpEpubStatus { title: app_abi::Text(b"old"), bookmarked: 99, building: 99, page: 2, pages: 37, book_progress: 5.5, y_offset: -3 };
            let snapshot = session.status_facts();
            session.paint_status_title(&ResourceAccess(PhantomData), &snapshot, &value, app_abi::Text(b"replacement"));
            assert_eq!(READS.with(Cell::get), 1);
            PAINT.with(|p| {
                let p = p.borrow(); assert_eq!(p.len(), 1);
                assert_eq!(p[0].0, Facts { marker: 0x1357 });
                assert_eq!(p[0].1, abi::XpEpubStatus { title: app_abi::Text(b"replacement"), bookmarked: u8::from(bookmark), building: u8::from(section == Some(true)), ..value });
            });
        }
    }
}
'''.replace('METHODS', method('status_facts') + '\n' + method('paint_status_title'))
with tempfile.TemporaryDirectory(prefix='status-shipping-') as directory:
    root = Path(directory)
    source = root / 'dispatch.rs'
    source.write_text(code)
    binary = root / 'dispatch'
    subprocess.run(['rustc', '--edition=2021', '--test', '-Dwarnings', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
