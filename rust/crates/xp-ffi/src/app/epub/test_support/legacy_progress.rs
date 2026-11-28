// Frozen synthetic book oracles only; actual ReaderBook compositions use production mapping and export.
use super::super::{abi, app_abi, session_book::Loaded};
use core::{ffi::c_void, mem::MaybeUninit, ptr::NonNull};

pub(super) type SavedSink =
    unsafe extern "C" fn(*mut c_void, *const abi::XpEpubSavedProgress) -> u8;

pub(in crate::app::epub) unsafe fn saved(
    raw: NonNull<c_void>,
    position: &abi::XpEpubPosition,
    mut consume: impl FnMut(&abi::XpEpubSavedProgress) -> u8,
) -> Option<u8> {
    let Loaded::Legacy {
        handle, services, ..
    } = (unsafe { raw.cast::<Loaded>().as_ref() })
    else {
        return None;
    };
    Some(invoke_saved(
        services.progress_to_saved.expect("synthetic progress save"),
        *handle,
        position,
        &mut consume,
    ))
}
type SavedCallback =
    unsafe extern "C" fn(*mut c_void, *const abi::XpEpubPosition, SavedSink, *mut c_void) -> u8;
fn invoke_saved<F: FnMut(&abi::XpEpubSavedProgress) -> u8>(
    callback: SavedCallback,
    handle: NonNull<c_void>,
    position: &abi::XpEpubPosition,
    consume: &mut F,
) -> u8 {
    unsafe extern "C" fn receive<F: FnMut(&abi::XpEpubSavedProgress) -> u8>(
        context: *mut c_void,
        row: *const abi::XpEpubSavedProgress,
    ) -> u8 {
        unsafe { (&mut *context.cast::<F>())(&*row) }
    }
    unsafe {
        callback(
            handle.as_ptr(),
            position,
            receive::<F>,
            (consume as *mut F).cast(),
        )
    }
}

pub(in crate::app::epub) unsafe fn mapped(
    raw: NonNull<c_void>,
    saved: &abi::XpEpubSavedProgress,
    spine: i32,
    pages: i32,
) -> Option<abi::XpEpubPosition> {
    let Loaded::Legacy {
        handle, services, ..
    } = (unsafe { raw.cast::<Loaded>().as_ref() })
    else {
        return None;
    };
    let mut out = MaybeUninit::uninit();
    unsafe {
        services.progress_to_reader.expect("synthetic progress map")(
            handle.as_ptr(),
            saved,
            spine,
            pages,
            out.as_mut_ptr(),
        );
    }
    Some(unsafe { out.assume_init() })
}
pub(in crate::app::epub) unsafe fn save_file(
    raw: NonNull<c_void>,
    spine: i32,
    page: i32,
    count: i32,
    offset: Option<u32>,
) -> Option<bool> {
    let Loaded::Legacy {
        handle, services, ..
    } = (unsafe { raw.cast::<Loaded>().as_ref() })
    else {
        return None;
    };
    let value = abi::XpEpubPosition {
        spine,
        page,
        count,
        visible_offset: offset.unwrap_or(0),
        has_visible_offset: u8::from(offset.is_some()),
        paragraph: 0,
        list_item: 0,
        has_paragraph: 0,
        has_list_item: 0,
        resolved_spine: 0,
        mapped_page: 0,
        anchor: [0; 64],
    };
    Some(unsafe {
        services.progress_save.expect("synthetic progress file")(handle.as_ptr(), &value) != 0
    })
}
pub(in crate::app::epub) unsafe fn export(
    raw: NonNull<c_void>,
    title: app_abi::Text,
    author: app_abi::Text,
    chapter: app_abi::Text,
    page: i32,
    text: app_abi::XpAppNativeText,
) -> Option<bool> {
    let Loaded::Legacy { services, .. } = (unsafe { raw.cast::<Loaded>().as_ref() }) else {
        return None;
    };
    Some(unsafe {
        services
            .clippings_export
            .expect("synthetic clipping export")(title, author, chapter, page, text)
            != 0
    })
}

#[test]
fn synthetic_saved_callback_borrows_only_during_delivery() {
    unsafe extern "C" fn callback(
        handle: *mut c_void,
        position: *const abi::XpEpubPosition,
        sink: SavedSink,
        context: *mut c_void,
    ) -> u8 {
        assert_eq!(unsafe { (*position).spine }, 3);
        let bytes = *b"/x";
        let row = abi::XpEpubSavedProgress {
            xpath: app_abi::Text::from_bytes(&bytes),
            percentage: 0.25,
        };
        unsafe {
            *handle.cast::<u32>() += 1;
            sink(context, &row)
        }
    }
    let mut count = 0u32;
    let position = abi::XpEpubPosition {
        spine: 3,
        page: 0,
        count: 1,
        visible_offset: 0,
        has_visible_offset: 0,
        paragraph: 0,
        list_item: 0,
        has_paragraph: 0,
        has_list_item: 0,
        resolved_spine: 0,
        mapped_page: 0,
        anchor: [0; 64],
    };
    let mut output = [0; 2];
    assert_eq!(
        invoke_saved(
            callback,
            NonNull::from(&mut count).cast(),
            &position,
            &mut |row| {
                assert_eq!(row.percentage, 0.25);
                output.copy_from_slice(unsafe { row.xpath.bytes().unwrap() });
                1
            }
        ),
        1
    );
    assert_eq!((count, output), (1, *b"/x"));
}
