use crate::bookmarks::{decode, encode, keyboard_name::KeyboardName, mutations, Name};
use std::{alloc::Layout, cell::Cell};
use xp_ui::keyboard::owner::{
    storage::{Block, Memory},
    Result as KeyboardResult,
};

thread_local! {
    static LIVE: Cell<usize> = const { Cell::new(0) };
    static ALLOCATIONS: Cell<usize> = const { Cell::new(0) };
    static DENY: Cell<bool> = const { Cell::new(false) };
}
struct Heap;
unsafe impl Memory for Heap {
    fn allocate(layout: Layout) -> *mut u8 {
        if DENY.with(|flag| flag.replace(false)) {
            return core::ptr::null_mut();
        }
        let pointer = unsafe { std::alloc::alloc_zeroed(layout) };
        if !pointer.is_null() {
            LIVE.with(|n| n.set(n.get() + 1));
            ALLOCATIONS.with(|n| n.set(n.get() + 1));
        }
        pointer
    }
    unsafe fn release(pointer: *mut u8, layout: Layout) {
        LIVE.with(|n| n.set(n.get() - 1));
        unsafe { std::alloc::dealloc(pointer, layout) };
    }
}
fn result(text: &[u8]) -> KeyboardResult<Heap> {
    let mut bytes = Block::default();
    assert!(bytes.grow(text.len() as u32 + 1, 0));
    bytes.as_mut_slice()[..text.len()].copy_from_slice(text);
    KeyboardResult::Text {
        bytes,
        length: text.len() as u32,
    }
}
#[test]
fn keyboard_name_moves_result_into_final_record_and_rollback_without_allocating() {
    let mut rows = decode::<KeyboardName<Heap>>(br#"{"bookmarks":[{"name":"before"}]}"#).unwrap();
    let result = result(b"after");
    let pointer = result.text().unwrap().as_ptr();
    let before = ALLOCATIONS.with(Cell::get);
    let name = KeyboardName::from_result(result).unwrap();
    assert_eq!(name.as_ref().as_ptr(), pointer);
    let old = mutations::replace_name(&mut rows, 0, name).ok().unwrap();
    assert_eq!(rows[0].name.as_ref(), b"after");
    assert_eq!(rows[0].name.terminated(), b"after\0");
    drop(mutations::replace_name(&mut rows, 0, old));
    assert_eq!(rows[0].name.as_ref(), b"before");
    assert_eq!(ALLOCATIONS.with(Cell::get), before);
    drop(rows);
    assert_eq!(LIVE.with(Cell::get), 0);
}
#[test]
fn keyboard_name_preserves_empty_cancel_embedded_nul_and_maximum() {
    assert!(KeyboardName::<Heap>::from_result(KeyboardResult::Cancelled).is_none());
    for text in [b"".as_slice(), b"embedded\0tail", &[b'x'; 128]] {
        let name = KeyboardName::from_result(result(text)).unwrap();
        assert_eq!(name.as_ref(), text);
        assert_eq!(name.terminated().last(), Some(&0));
    }
    let rows = decode::<KeyboardName<Heap>>(br#"{"bookmarks":[{"name":"a\u0000b"},{"name":""}]}"#)
        .unwrap();
    // The native JSON loader constructs names from C strings.
    assert_eq!(rows[0].name.as_ref(), b"a");
    assert_eq!(rows[1].name.terminated(), b"\0");
    let reference =
        decode::<Vec<u8>>(br#"{"bookmarks":[{"name":"a\u0000b"},{"name":""}]}"#).unwrap();
    assert_eq!(encode(&rows).unwrap(), encode(&reference).unwrap());
    drop(rows);
    assert_eq!(LIVE.with(Cell::get), 0);
}
#[test]
fn keyboard_name_decode_failure_and_repeated_moves_release_once() {
    DENY.with(|flag| flag.set(true));
    assert!(KeyboardName::<Heap>::read(4, |out| out.copy_from_slice(b"name")).is_err());
    assert_eq!(LIVE.with(Cell::get), 0);
    for _ in 0..12 {
        let name = KeyboardName::from_result(result(b"visit")).unwrap();
        assert_eq!(name.as_ref(), b"visit");
        drop(name);
        assert_eq!(LIVE.with(Cell::get), 0);
    }
}
