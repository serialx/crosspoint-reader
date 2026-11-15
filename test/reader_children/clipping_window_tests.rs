use crate::clipping_window::Window;
use std::{boxed::Box, mem::MaybeUninit};

fn window() -> Box<Window> {
    let mut value = Box::<Window>::new_uninit();
    unsafe {
        Window::initialize(value.as_mut_ptr());
        value.assume_init()
    }
}
#[test]
fn clipping_window_clamps_reuses_and_invalidates_after_delete() {
    let mut window = window();
    let mut reads = Vec::new();
    window.refresh(12, 100, |index, row| {
        reads.push(index);
        row.chapter[0] = b'A' + index as u8;
        row.preview[..2].copy_from_slice(b"x\0");
        row.preview_length = 1;
    });
    assert_eq!(reads, (4..12).collect::<Vec<_>>());
    assert!(window.row(3).is_none());
    assert_eq!(window.row(11).unwrap().chapter[0], b'L');
    assert!(window.row(12).is_none());
    window.refresh(12, 4, |_, _| panic!("cached window"));
    window.invalidate();
    window.refresh(11, 4, |index, row| {
        row.chapter[0] = b'a' + index as u8;
        assert_eq!(row.preview_length, 0);
        assert_eq!(row.preview[0], 0);
    });
    assert_eq!(window.row(3).unwrap().chapter[0], b'd');
    assert!(window.row(11).is_none());
}
#[test]
fn clipping_window_empty_and_short_lists_keep_only_valid_rows() {
    let mut window = window();
    window.refresh(0, -1, |_, _| panic!("empty"));
    assert!(window.row(0).is_none());
    window.invalidate();
    window.refresh(1, -9, |index, row| {
        assert_eq!(index, 0);
        row.chapter[0] = b'K';
        row.preview_length = 259;
        row.preview[259] = 0;
    });
    assert_eq!(window.row(0).unwrap().preview_length, 259);
    assert!(window.row(1).is_none());
    assert!(window.row(-1).is_none());
}
#[test]
fn clipping_window_initializes_all_storage_in_place() {
    let mut raw = MaybeUninit::<Window>::uninit();
    unsafe { Window::initialize(raw.as_mut_ptr()) };
    let mut window = unsafe { raw.assume_init() };
    window.refresh(8, 0, |_, row| {
        assert!(row.chapter.iter().all(|byte| *byte == 0));
        assert!(row.preview.iter().all(|byte| *byte == 0));
        assert_eq!(row.preview_length, 0);
    });
}
