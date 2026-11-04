use super::*;
use core::ffi::c_void;

type Observe = unsafe extern "C" fn(*mut c_void, u8, u16, *const u8, usize);

// Observers borrow bytes only for the call; no synthetic owner replaces Page.
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_page_owner(
    resources: &'static app_abi::Resources,
    leaves: &'static XpEpubLeaves,
    input: app_abi::Text,
    path: app_abi::Text,
    visible: u32,
    observe: Observe,
    context: *mut c_void,
) -> u8 {
    let page = if path.len == 0 {
        Page::from_bytes(
            resources,
            leaves,
            unsafe { input.bytes() }.unwrap(),
            visible,
        )
    } else {
        let mut file = FileSlot::new();
        let path = crate::app::resources::Text::copy(unsafe { path.bytes() }.unwrap()).unwrap();
        assert!(file.open(leaves, &path, false, c"PGE"));
        // The file fixture has a four-byte prefix and trailing section data.
        assert!(file.seek(4));
        Page::load(resources, leaves, &mut file, input.len as u32, visible)
    };
    let Some(page) = page else { return 0 };
    assert_eq!(page.view().visible_offset(), visible);
    unsafe { observe(context, 0, 0, page.arena().as_ptr(), page.arena().len()) };
    let mut metadata = Metadata::default();
    for _ in 0..2 {
        if !metadata.assign(resources, leaves, &page) {
            assert_eq!(metadata.footnote_count(), 0);
            assert_eq!(metadata.link_count(), 0);
            return 1;
        }
    }
    drop(page);
    for index in 0..metadata.footnote_count() {
        let (number, href) = metadata.footnote(index).unwrap();
        unsafe {
            observe(context, 1, index, number.as_ptr(), number.len());
            observe(context, 2, index, href.as_ptr(), href.len());
        }
    }
    for index in 0..metadata.link_count() {
        let link = metadata.link(index).unwrap();
        unsafe { observe(context, 3, index, link.href.as_ptr(), link.href.len()) };
        for (field, value) in [link.x, link.y, link.width, link.height]
            .into_iter()
            .enumerate()
        {
            let bytes = value.to_le_bytes();
            unsafe { observe(context, 4 + field as u8, index, bytes.as_ptr(), bytes.len()) };
        }
    }
    assert!(metadata.footnote(metadata.footnote_count()).is_none());
    assert!(metadata.link(metadata.link_count()).is_none());
    metadata.clear();
    assert_eq!(metadata.footnote_count(), 0);
    assert_eq!(metadata.link_count(), 0);
    2
}
