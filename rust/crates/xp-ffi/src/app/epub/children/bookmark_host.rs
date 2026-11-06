//! Pixel fixture: one decoded record list, the real child, and borrowed drawing/TOC callbacks.
extern crate alloc;
use super::{bookmark_owned::Bookmarks, bookmark_view::Details, bookmark_window::Toc};
use crate::app::{
    abi,
    bookmarks::{decode, Record},
    epub,
    resources::Text,
    stable_owner::StableOwner,
};
use alloc::{boxed::Box, vec::Vec};
use core::ffi::c_void;
use xp_ui::{reader_chrome::ListModel, scene::Rect};

struct Visit {
    owner: StableOwner<Bookmarks>,
    records: Vec<Record<Text>>,
}
struct Titles(unsafe extern "C" fn(u16, abi::TextSink, *mut c_void) -> u8);
impl Toc for Titles {
    fn subtitle(&mut self, spine: u16, prefix: &[u8], output: &mut Text) -> bool {
        struct Fill<'a> {
            prefix: &'a [u8],
            output: &'a mut Text,
        }
        unsafe extern "C" fn receive(context: *mut c_void, text: abi::Text) -> u8 {
            let fill = unsafe { &mut *context.cast::<Fill<'_>>() };
            let Some(text) = (unsafe { text.bytes() }) else {
                return 0;
            };
            u8::from(fill.output.assign_parts(&[fill.prefix, text]).is_ok())
        }
        let mut fill = Fill { prefix, output };
        unsafe { (self.0)(spine, receive, (&mut fill as *mut Fill<'_>).cast()) != 0 }
    }
}
#[no_mangle]
/// # Safety
/// The JSON span is readable for this call; runtime callbacks remain valid until close.
pub unsafe extern "C" fn xp_epub_test_bookmark_open(
    runtime: &'static abi::Runtime,
    json: abi::Text,
) -> *mut c_void {
    let records = decode::<Text>(unsafe { json.bytes() }.unwrap()).unwrap();
    let owner = Bookmarks::try_new(runtime).unwrap().into();
    Box::into_raw(Box::new(Visit { owner, records })).cast()
}
#[no_mangle]
/// # Safety
/// `raw` uniquely owns the live visit returned by open, with no active callbacks.
pub unsafe extern "C" fn xp_epub_test_bookmark_close(raw: *mut c_void) {
    let visit = unsafe { Box::from_raw(raw.cast::<Visit>()) };
    unsafe { visit.owner.exit(|_| panic!("drawing fixture has no popup")) };
}
#[no_mangle]
/// # Safety
/// All tables and output storage are valid; callbacks borrow spans synchronously
/// and do not reenter or destroy the visit. The fixture excludes concurrent render.
pub unsafe extern "C" fn xp_epub_test_bookmark_render(
    raw: *mut c_void,
    services: &'static abi::Resources,
    leaves: &'static epub::abi::XpEpubLeaves,
    selected: i32,
    revealed: i32,
    icon: unsafe extern "C" fn(u8, Rect, Rect, u8),
    title: unsafe extern "C" fn(u16, abi::TextSink, *mut c_void) -> u8,
    output: *mut ListModel,
) -> u16 {
    let visit = unsafe { &*raw.cast::<Visit>() };
    visit.owner.model.with(|model| {
        model.state.list.select(selected);
        model.state.revealed = revealed;
    });
    let result = unsafe {
        visit.owner.render(
            services,
            &leaves.child_drawing,
            &Details {
                help: leaves.child_list_help,
                icon,
            },
            &visit.records,
            &mut Titles(title),
        )
    }
    .unwrap();
    unsafe { output.write(visit.owner.model.with(|model| model.state)) };
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    unsafe extern "C" fn title(_: u16, sink: abi::TextSink, context: *mut c_void) -> u8 {
        let mut source = *b"borrowed chapter\0";
        let ok = unsafe { sink(context, abi::Text::from_terminated(&source)) };
        source.fill(0);
        ok
    }
    #[test]
    fn bookmark_toc_callback_fills_final_subtitle_before_title_loan_ends() {
        let mut text = Text::default();
        assert!(Titles(title).subtitle(0, b"50% - ", &mut text));
        assert_eq!(text.bytes(), b"50% - borrowed chapter");
        let wire = text.wire();
        assert_eq!(unsafe { *wire.data.add(wire.len) }, 0);
    }
}
