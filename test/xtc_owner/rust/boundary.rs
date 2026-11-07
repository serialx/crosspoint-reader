//! Scripted C3 allocation limits; actual native helper/allocator traces are separate C3 evidence.
#[path = "../../../rust/crates/xp-ffi/src/xtc_owner/mod.rs"]
mod xtc_owner;
use core::{cell::Cell, ffi::c_void, ptr};
use xp_core::{book_io::xtc_reader as parent, xtc::Header};
use xtc_owner::{abi::*, *};

struct File {
    header: [u8; 56],
    table: [u8; 16],
    page: [u8; 22],
    at: u64,
    size: u64,
    table_at: u64,
    opened: bool,
}
impl File {
    fn new(count: u64) -> Self {
        let table_at = 256 + count * 96;
        let mut file = Self {
            header: [0; 56],
            table: [0; 16],
            page: [0; 22],
            at: 0,
            size: table_at + 16,
            table_at,
            opened: false,
        };
        file.header[..4].copy_from_slice(b"XTC\0");
        file.header[4] = 1;
        file.header[6] = 1;
        file.header[11] = 1;
        file.header[24..32].copy_from_slice(&table_at.to_le_bytes());
        file.header[32..40].copy_from_slice(&128u64.to_le_bytes());
        file.header[48..56].copy_from_slice(&256u64.to_le_bytes());
        file.table[..8].copy_from_slice(&128u64.to_le_bytes());
        file.table[8..12].copy_from_slice(&23u32.to_le_bytes());
        file.table[12] = 8;
        file.table[14] = 1;
        file.page[..4].copy_from_slice(b"XTG\0");
        file.page[4] = 8;
        file.page[6] = 1;
        file.page[10] = 1;
        file
    }
}
unsafe extern "C" fn open(p: *mut c_void, _: *const u8) -> bool {
    println!("open");
    let f = &mut *p.cast::<File>();
    f.at = 0;
    f.opened = true;
    true
}
unsafe extern "C" fn parent_open(p: *mut c_void) -> bool {
    open(p, ptr::null())
}
unsafe extern "C" fn is_open(p: *mut c_void) -> bool {
    (*p.cast::<File>()).opened
}
unsafe extern "C" fn close(p: *mut c_void) {
    println!("close");
    (*p.cast::<File>()).opened = false;
}
unsafe extern "C" fn read(p: *mut c_void, out: *mut u8, n: usize) -> i32 {
    let f = &mut *p.cast::<File>();
    println!("read {} {n}", f.at);
    let source = if f.at == 0 && n == 56 {
        &f.header[..]
    } else if f.at == 11 && n == 1 {
        &f.header[11..12]
    } else if f.at == 48 && n == 8 {
        &f.header[48..56]
    } else if f.at == f.table_at && n == 16 {
        &f.table[..]
    } else if f.at == 128 && n == 22 {
        &f.page[..]
    } else {
        panic!("unexpected scripted read")
    };
    assert_eq!(source.len(), n);
    ptr::copy_nonoverlapping(source.as_ptr(), out, n);
    f.at += n as u64;
    n as i32
}
unsafe extern "C" fn seek(p: *mut c_void, at: u64) -> bool {
    println!("seek {at}");
    (*p.cast::<File>()).at = at;
    true
}
unsafe extern "C" fn size(p: *mut c_void) -> u64 {
    (*p.cast::<File>()).size
}
unsafe extern "C" fn diagnostic(_: *mut c_void, event: u32, a: u32, b: u32, c: u32, d: u32) {
    println!("log {event} {a} {b} {c} {d}");
}
fn required(n: usize, kind: u32) -> ! {
    match kind {
        2 if n > i32::MAX as usize / 28 => println!("length_error vector::reserve"),
        2 => println!("bad_alloc {}", n * 28),
        3 if n > i32::MAX as usize => {
            println!("length_error cannot create std::vector larger than max_size()")
        }
        3 => println!("bad_alloc {n}"),
        _ => panic!("wrong allocation kind {kind} {n}"),
    }
    std::process::exit(0)
}
unsafe extern "C" fn allocate(n: usize, kind: u32) -> *mut u8 {
    required(n, kind)
}
unsafe extern "C" fn release(_: *mut u8, _: usize) {
    panic!("no allocated buffer in boundary model")
}
unsafe extern "C" fn metadata(_: *mut c_void, _: u32, _: *const u8, _: usize) {}
unsafe extern "C" fn chapters(
    _: *mut c_void,
    action: u32,
    count: usize,
    _: *const u8,
    _: u16,
    _: u16,
) -> usize {
    if action == 1 {
        required(count, 2)
    }
    0
}
unsafe extern "C" fn stream_allocate(_: *mut c_void, n: usize) -> *mut u8 {
    required(n, 3)
}
unsafe extern "C" fn emit(_: *mut c_void, _: *const u8, _: usize, _: usize) {
    panic!("allocation cannot return")
}

fn main() {
    let args: Vec<_> = std::env::args().collect();
    let n: usize = args[3].parse().unwrap();
    let mut file = File::new(if args[2] == "chapter" { n as u64 } else { 1 });
    let context = ptr::addr_of_mut!(file).cast();
    unsafe {
        if args[1] == "parent" {
            let state = parent::State {
                header: Cell::new(Header::default()),
                width: Cell::new(480),
                height: Cell::new(800),
                depth: Cell::new(1),
                opened: Cell::new(false),
                has_chapters: Cell::new(false),
                chapters_loaded: Cell::new(false),
                error: Cell::new(0),
            };
            let io = parent::Services {
                context,
                open: parent_open,
                is_open,
                close,
                read,
                seek,
                size,
                metadata,
                chapters,
                diagnostic: Some(diagnostic),
            };
            assert_eq!(parent::open(&state, &io), 0);
            if args[2] == "chapter" {
                parent::chapters(&state, &io);
            } else {
                parent::stream(
                    &state,
                    &io,
                    0,
                    n,
                    &parent::Stream {
                        context,
                        allocate: stream_allocate,
                        emit,
                    },
                );
            }
        } else {
            let leaves = XpXtcLeaves {
                open,
                is_open,
                close,
                read,
                seek,
                size,
                allocate,
                release,
                diagnostic,
            };
            let mut storage = core::mem::MaybeUninit::uninit();
            xp_xtc_owner_init(storage.as_mut_ptr(), context, &leaves);
            assert_eq!(
                xp_xtc_owner_open(storage.as_mut_ptr(), c"book".as_ptr().cast(), 4),
                0
            );
            if args[2] == "chapter" {
                xp_xtc_owner_view(
                    storage.as_mut_ptr(),
                    true,
                    core::mem::MaybeUninit::uninit().as_mut_ptr(),
                );
            } else {
                xp_xtc_owner_stream(storage.as_mut_ptr(), 0, n, &XpXtcStream { context, emit });
            }
        }
    }
    panic!("the boundary allocator must terminate before returning")
}
