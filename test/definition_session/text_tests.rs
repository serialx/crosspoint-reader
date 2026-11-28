use super::{abi, definition_text::OwnedText};
use std::{
    alloc::{alloc, dealloc, Layout},
    cell::{Cell, RefCell},
    ffi::c_void,
    ptr,
};
thread_local! {
 static FAIL: Cell<bool> = const { Cell::new(false) };
 static BLOCKS: RefCell<Vec<(*mut u8, Layout)>> = const { RefCell::new(Vec::new()) };
 static ATTEMPTS: Cell<usize> = const {Cell::new(0)};
}
// Rust allocator witness for Miri; native compositions use matched malloc/free.
#[no_mangle]
unsafe extern "C" fn xp_test_definition_malloc(bytes: usize) -> *mut c_void {
    ATTEMPTS.with(|n| n.set(n.get() + 1));
    if FAIL.with(Cell::get) {
        return ptr::null_mut();
    }
    let layout = Layout::from_size_align(bytes, 1).unwrap();
    let data = unsafe { alloc(layout) };
    BLOCKS.with(|b| b.borrow_mut().push((data, layout)));
    data.cast()
}
#[no_mangle]
unsafe extern "C" fn xp_test_definition_free(data: *mut c_void) {
    BLOCKS.with(|b| {
        let mut b = b.borrow_mut();
        let i = b.iter().position(|(p, _)| *p == data.cast()).unwrap();
        let (p, l) = b.remove(i);
        unsafe { dealloc(p, l) };
    });
}
fn bytes(text: &OwnedText) -> &[u8] {
    let view = text.view();
    unsafe { std::slice::from_raw_parts(view.data, view.len) }
}
#[test]
fn query_moves_reuse_the_same_block_and_drop_once() {
    ATTEMPTS.with(|a| a.set(0));
    let mut pending = OwnedText::default();
    assert!(pending.assign(&[b'x'; 4096]));
    let pointer = pending.view().data;
    let mut current = std::mem::take(&mut pending);
    let mut history = std::mem::take(&mut current);
    assert_eq!(history.view().data, pointer);
    history.clear();
    assert!(history.is_empty());
    assert!(history.assign(b"a"));
    assert_eq!(history.view().data, pointer);
    assert_eq!(ATTEMPTS.with(Cell::get), 1);
    assert_eq!(bytes(&history), b"a");
    drop(history);
    assert!(BLOCKS.with(|b| b.borrow().is_empty()));
}
#[test]
fn query_failed_growth_preserves_bytes_capacity_and_owner() {
    let mut text = OwnedText::default();
    assert!(text.assign(b"old"));
    let pointer = text.view().data;
    FAIL.with(|f| f.set(true));
    assert!(!text.assign(&[b'y'; 4096]));
    FAIL.with(|f| f.set(false));
    assert_eq!(text.view().data, pointer);
    assert_eq!(bytes(&text), b"old");
    drop(text);
    assert!(BLOCKS.with(|b| b.borrow().is_empty()));
}
#[test]
fn empty_query_has_no_allocation_and_initial_query_failure_is_fallible() {
    ATTEMPTS.with(|a| a.set(0));
    let mut text = OwnedText::default();
    assert!(text.assign(b""));
    assert_eq!(ATTEMPTS.with(Cell::get), 0);
    FAIL.with(|f| f.set(true));
    assert!(!text.assign(b"initial"));
    FAIL.with(|f| f.set(false));
    assert!(text.is_empty());
    assert!(BLOCKS.with(|b| b.borrow().is_empty()));
}
#[test]
fn native_owned_result_is_adopted_without_copy_or_allocator_reconstruction() {
    let data = unsafe { xp_test_definition_malloc(4) }.cast::<u8>();
    unsafe { ptr::copy_nonoverlapping(b"abc\0".as_ptr(), data, 4) };
    unsafe extern "C" fn release(p: *mut u8, _: u32) {
        unsafe { xp_test_definition_free(p.cast()) };
    }
    let text = unsafe {
        OwnedText::from_wire(abi::DefinitionOwnedText {
            data,
            len: 3,
            capacity: 4,
            release: Some(release),
        })
    };
    assert_eq!(text.view().data, data);
    assert_eq!(bytes(&text), b"abc");
    drop(text);
    assert!(BLOCKS.with(|b| b.borrow().is_empty()));
}
