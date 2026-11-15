use super::*;

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_book_open(
    resources: &'static app_abi::Resources,
    leaves: &'static XpEpubLeaves,
    route: u8,
    write: u8,
) {
    let path = Text::copy(b"/missing/\xff.epub").unwrap();
    let mut slot = writer::io::FileSlot::new();
    match route {
        0 => {
            slot.open(leaves, &path, write != 0, c"ERS");
        }
        1 => {
            let _ = BookCache::new(leaves).load(&mut Metadata::default(), &path);
        }
        2 | 3 => {
            let mut writer = writer::Writer::new(&path, &path, resources, leaves);
            let io = writer.streams.io();
            io.control(
                if route == 2 {
                    xp_core::epub::cache::ITEMS
                } else {
                    xp_core::epub::cache::BOOK
                },
                if write != 0 {
                    xp_core::epub::cache::OPEN_WRITE
                } else {
                    xp_core::epub::cache::OPEN_READ
                },
            );
        }
        4 => {
            css::run(
                &mut None,
                &path,
                resources,
                leaves,
                if write != 0 {
                    css::Action::Save
                } else {
                    css::Action::Inspect
                },
                true,
                None,
            );
        }
        5 => {
            zip::Archive::new(&path, resources, leaves).lookup(b"item.xhtml");
        }
        6 => {
            if write != 0 {
                slot.open(leaves, &path, true, c"TXT");
            } else {
                unsafe extern "C" fn now() -> u32 {
                    0
                }
                unsafe extern "C" fn sink(_: *mut core::ffi::c_void, _: *const u8, _: usize) -> u8 {
                    1
                }
                unsafe {
                    txt::stream(&path, resources, leaves, now, sink, core::ptr::null_mut());
                }
            }
        }
        7 => {
            slot.open(leaves, &path, write != 0, c"EBP");
        }
        _ => unreachable!(),
    }
}
