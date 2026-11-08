pub use xp_core::{font, sdfont};
#[path = "../../rust/crates/xp-core/src/sdfont/parser.rs"]
mod parser;
use parser::{Allocator, ParsedFont, Reader};
use std::{
    alloc::{alloc, dealloc, Layout},
    cell::RefCell,
    ptr,
};

#[derive(Default)]
struct Memory {
    attempts: usize,
    fail_at: usize,
    live: Vec<(*mut u8, Layout)>,
}
thread_local! { static MEMORY: RefCell<Memory> = RefCell::default(); }
unsafe extern "C" fn allocate(bytes: usize) -> *mut u8 {
    MEMORY.with(|memory| {
        let mut m = memory.borrow_mut();
        m.attempts += 1;
        if m.attempts == m.fail_at {
            return ptr::null_mut();
        }
        let layout =
            Layout::from_size_align(bytes.max(1), std::mem::align_of::<sdfont::pool::Resident>())
                .unwrap();
        let p = alloc(layout);
        if !p.is_null() {
            m.live.push((p, layout));
        }
        p
    })
}
unsafe extern "C" fn free(p: *mut u8, _: bool) {
    if p.is_null() {
        return;
    }
    MEMORY.with(|memory| {
        let mut m = memory.borrow_mut();
        let i = m
            .live
            .iter()
            .position(|entry| entry.0 == p)
            .expect("unknown free");
        let (_, layout) = m.live.swap_remove(i);
        dealloc(p, layout);
    });
}
unsafe extern "C" fn free_styles(p: *mut u8, _: usize) {
    free(p, false);
}
const ALLOCATOR: Allocator = Allocator {
    styles: allocate,
    resident: allocate,
    free_styles,
    free_resident: free,
};
struct Input<'a> {
    bytes: &'a [u8],
    position: usize,
    calls: usize,
    fail_at: usize,
}
impl Reader for Input<'_> {
    fn seek(&mut self, offset: u32) -> bool {
        self.calls += 1;
        if self.calls == self.fail_at {
            return false;
        }
        self.position = offset as usize;
        self.position <= self.bytes.len()
    }
    fn read(&mut self, bytes: &mut [u8]) -> usize {
        self.calls += 1;
        if self.calls == self.fail_at {
            return 0;
        }
        let count = bytes
            .len()
            .min(self.bytes.len().saturating_sub(self.position));
        bytes[..count].copy_from_slice(&self.bytes[self.position..self.position + count]);
        self.position += count;
        count
    }
}
fn input(bytes: &[u8], fail_at: usize) -> Input<'_> {
    Input {
        bytes,
        position: 0,
        calls: 0,
        fail_at,
    }
}
fn record(bytes: &[u8]) -> Vec<u8> {
    let mut output = Vec::new();
    let mut value = |v: u32| output.extend_from_slice(&v.to_le_bytes());
    let font = unsafe { ParsedFont::load(&mut input(bytes, 0), ALLOCATOR) };
    value(u32::from(font.is_some()));
    if let Some(f) = font {
        value(f.content_hash());
        value(u32::from(f.style_count()));
        for id in 0..4 {
            let s = f.style(id);
            value(u32::from(s.is_some()));
            let Some(s) = s else {
                continue;
            };
            let h = &s.header;
            for v in [
                h.interval_count,
                h.glyph_count,
                u32::from(h.advance_y),
                h.ascender as u32,
                h.descender as u32,
                u32::from(h.is2_bit),
                u32::from(h.kern_left_entry_count),
                u32::from(h.kern_right_entry_count),
                u32::from(h.kern_left_class_count),
                u32::from(h.kern_right_class_count),
                u32::from(h.ligature_pair_count),
            ] {
                value(v);
            }
            for v in s.offsets {
                value(v);
            }
            value(u32::from(s.intervals_are_bmp16));
            for j in 0..h.interval_count as usize {
                let (first, last, offset) = unsafe {
                    if s.intervals_are_bmp16 {
                        let iv = &*s.bmp_intervals.add(j);
                        (
                            u32::from(iv.first),
                            u32::from(iv.last),
                            u32::from(iv.offset),
                        )
                    } else {
                        let iv = &*s.full_intervals.add(j);
                        (iv.first, iv.last, iv.offset)
                    }
                };
                value(first);
                value(last);
                value(offset);
            }
        }
    }
    MEMORY.with(|m| assert!(m.borrow().live.is_empty()));
    output
}
fn main() {
    use std::io::{Read, Write};
    let mut stdin = std::io::stdin().lock();
    let mut stdout = std::io::BufWriter::new(std::io::stdout().lock());
    loop {
        let mut size = [0; 4];
        match stdin.read_exact(&mut size) {
            Ok(()) => (),
            Err(e) if e.kind() == std::io::ErrorKind::UnexpectedEof => break,
            Err(e) => panic!("{e}"),
        }
        let mut bytes = vec![0; u32::from_le_bytes(size) as usize];
        stdin.read_exact(&mut bytes).unwrap();
        let output = record(&bytes);
        stdout
            .write_all(&(output.len() as u32).to_le_bytes())
            .unwrap();
        stdout.write_all(&output).unwrap();
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    fn fixture() -> Vec<u8> {
        let mut bytes = vec![0; 152];
        bytes[..8].copy_from_slice(b"CPFONT\0\0");
        bytes[8] = 4;
        bytes[12] = 2;
        for (toc, id) in [(32, 0), (64, 3)] {
            bytes[toc] = id;
            bytes[toc + 4..toc + 8].copy_from_slice(&1u32.to_le_bytes());
            bytes[toc + 8..toc + 12].copy_from_slice(&2u32.to_le_bytes());
            bytes[toc + 17] = 1;
            bytes[toc + 19] = 1;
            bytes[toc + 24..toc + 28].copy_from_slice(&96u32.to_le_bytes());
        }
        bytes[96..100].copy_from_slice(&65u32.to_le_bytes());
        bytes[100..104].copy_from_slice(&66u32.to_le_bytes());
        bytes
    }
    fn variants() -> Vec<(Vec<u8>, Vec<usize>)> {
        let compact = fixture();
        let mut full = compact.clone();
        full[96..100].copy_from_slice(&0x10000u32.to_le_bytes());
        full[100..104].copy_from_slice(&0x10001u32.to_le_bytes());
        let mut distinct = compact.clone();
        distinct[88..92].copy_from_slice(&108u32.to_le_bytes());
        distinct[108..112].copy_from_slice(&67u32.to_le_bytes());
        distinct[112..116].copy_from_slice(&68u32.to_le_bytes());
        vec![
            (compact, vec![1, 2]),
            (full, vec![1, 2]),
            (distinct, vec![1, 2, 4]),
        ]
    }
    #[test]
    fn sharing_stability_and_drop() {
        let bytes = fixture();
        let mut reader = input(&bytes, 0);
        let font = unsafe { ParsedFont::load(&mut reader, ALLOCATOR) }.unwrap();
        let address = font.style(0).unwrap().bmp_intervals;
        assert_eq!(address, font.style(3).unwrap().bmp_intervals);
        let moved = Box::new(font);
        assert_eq!(address, moved.style(0).unwrap().bmp_intervals);
        assert!(moved.style(1).is_none());
        assert!(moved.style(4).is_none());
        drop(moved);
        MEMORY.with(|m| assert!(m.borrow().live.is_empty()));
    }
    #[test]
    fn every_load_allocation_failure_releases_partial_tables() {
        for (bytes, required) in variants() {
            MEMORY.with(|m| *m.borrow_mut() = Memory::default());
            let _ = record(&bytes);
            let allocations = MEMORY.with(|m| m.borrow().attempts);
            for fail_at in 1..=allocations {
                MEMORY.with(|m| {
                    *m.borrow_mut() = Memory {
                        fail_at,
                        ..Memory::default()
                    }
                });
                let font = unsafe { ParsedFont::load(&mut input(&bytes, 0), ALLOCATOR) };
                // Kern block indexes are optional; interval/style allocations are required.
                assert_eq!(font.is_none(), required.contains(&fail_at));
                drop(font);
                MEMORY.with(|m| assert!(m.borrow().live.is_empty()));
            }
        }
        MEMORY.with(|m| *m.borrow_mut() = Memory::default());
    }
    #[test]
    fn every_read_and_seek_failure_releases_partial_tables() {
        for (bytes, _) in variants() {
            let mut reader = input(&bytes, 0);
            drop(unsafe { ParsedFont::load(&mut reader, ALLOCATOR) }.unwrap());
            for fail_at in 1..=reader.calls {
                assert!(
                    unsafe { ParsedFont::load(&mut input(&bytes, fail_at), ALLOCATOR) }.is_none()
                );
                MEMORY.with(|m| assert!(m.borrow().live.is_empty()));
            }
        }
    }
    #[test]
    fn every_short_input_and_malformed_directory_is_safe() {
        let bytes = fixture();
        for length in 0..bytes.len() {
            let _ = record(&bytes[..length]);
        }
        for (offset, value) in [(0, 0), (8, 3), (12, 0), (64, 0), (32, 4), (36, 255)] {
            let mut bad = bytes.clone();
            bad[offset] = value;
            assert_eq!(record(&bad), 0u32.to_le_bytes());
        }
    }
}
