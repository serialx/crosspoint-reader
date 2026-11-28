use crate::{chapters::Window, resources::Text};

fn label(index: usize) -> Vec<u8> {
    format!(
        "{:04} 한국어 chapter — {}",
        index,
        "title ".repeat(index % 35)
    )
    .into_bytes()
}
fn window() -> Box<Window> {
    let mut window = Box::<Window>::new_uninit();
    unsafe {
        Window::initialize(window.as_mut_ptr());
        window.assume_init()
    }
}
#[test]
fn long_toc_windows_are_bounded_clamped_and_reused_before_prewarm() {
    let mut window = window();
    let mut reads = Vec::new();
    for (start, expected) in [
        (0, 0),
        (0, 0),
        (23, 23),
        (24, 24),
        (999, 1000 - 24),
        (1000, 1000 - 24),
        (-1, 0),
    ] {
        let old_count = reads.len();
        let old_start = window.first();
        assert!(window.refresh(1000, start, |index, out| {
            reads.push(index);
            out.assign_parts(&[b"  ", &label(index as usize)]).is_ok()
        }));
        assert_eq!(window.first(), Some(expected));
        assert_eq!(
            reads.len() - old_count,
            if old_start == Some(expected) { 0 } else { 24 }
        );
        assert_eq!(window.labels().len(), 24);
        for (i, text) in window.labels().iter().enumerate() {
            assert_eq!(
                text.bytes(),
                [b"  ".as_slice(), label(expected as usize + i).as_slice()].concat()
            );
        }
        assert!(window.label(expected - 1).is_none());
        assert!(window.label(expected + 24).is_none());
    }
}
#[test]
fn partial_failed_fill_cannot_publish_a_window_or_reuse_its_key() {
    let mut window = window();
    assert!(window.refresh(1000, 0, |index, out| out
        .assign(&label(index as usize))
        .is_ok()));
    assert!(!window.refresh(1000, 24, |index, out| {
        if index == 31 {
            return false;
        }
        out.assign(&label(index as usize)).is_ok()
    }));
    assert_eq!(window.first(), None);
    assert!(window.labels().is_empty());
    assert!(window.label(24).is_none());
    let mut reads = Vec::new();
    assert!(window.refresh(1000, 24, |index, out| {
        reads.push(index);
        out.assign(&label(index as usize)).is_ok()
    }));
    assert_eq!(reads, (24..48).collect::<Vec<_>>());
}
#[test]
fn native_empty_row_fallback_mid_window_replaces_old_text_and_finishes_all_rows() {
    let mut window = window();
    assert!(window.refresh(1000, 0, |index, out| out
        .assign(&label(index as usize))
        .is_ok()));
    let mut reads = Vec::new();
    assert!(window.refresh(1000, 24, |index, out| {
        reads.push(index);
        if index == 31 {
            out.assign(b"").is_ok()
        } else {
            out.assign(&label(index as usize)).is_ok()
        }
    }));
    assert_eq!(reads, (24..48).collect::<Vec<_>>());
    assert_eq!(window.first(), Some(24));
    for index in 24..48 {
        assert_eq!(
            window.label(index).unwrap().bytes(),
            if index == 31 {
                Vec::new()
            } else {
                label(index as usize)
            }
        );
    }
}
#[test]
fn empty_and_short_toc_windows_have_no_stale_tail() {
    let mut window = window();
    assert!(window.refresh(9, 8, |index, out| out
        .assign(&label(index as usize))
        .is_ok()));
    assert_eq!(window.first(), Some(0));
    assert_eq!(window.labels().len(), 9);
    assert!(window.refresh(0, 0, |_, _: &mut Text| panic!("empty TOC read")));
    assert_eq!(window.labels().len(), 0);
}
