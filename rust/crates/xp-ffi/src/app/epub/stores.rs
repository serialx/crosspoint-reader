use super::{abi::XpEpubLeaves, app_abi, book_owner::log, Text};
use crate::settings::{self as records, Bytes, JsonOps, PersistOps, Rows, State};
use core::{mem::MaybeUninit, ptr};

fn bytes(value: app_abi::Text) -> Bytes {
    Bytes {
        data: value.data,
        len: value.len,
    }
}
fn text(value: Bytes) -> app_abi::Text {
    app_abi::Text {
        data: value.data,
        len: value.len,
    }
}

unsafe fn with_rows<T>(leaves: &XpEpubLeaves, action: impl FnOnce(&Rows) -> T) -> T {
    let mut storage = MaybeUninit::<[usize; 32]>::uninit();
    let mut rows = MaybeUninit::uninit();
    unsafe { (leaves.store_rows)(storage.as_mut_ptr().cast(), rows.as_mut_ptr()) };
    let result = action(unsafe { rows.assume_init_ref() });
    unsafe { (leaves.store_rows_close)(storage.as_mut_ptr().cast()) };
    result
}

#[inline(never)]
unsafe fn save(leaves: &XpEpubLeaves, rows: Option<&Rows>) -> bool {
    let mut storage = MaybeUninit::<[usize; 48]>::uninit();
    let mut json = MaybeUninit::<JsonOps>::uninit();
    let mut file = MaybeUninit::<PersistOps>::uninit();
    let mut state = MaybeUninit::<State>::uninit();
    unsafe {
        (leaves.store_document)(
            u8::from(rows.is_some()),
            storage.as_mut_ptr().cast(),
            json.as_mut_ptr(),
            file.as_mut_ptr(),
            state.as_mut_ptr(),
        )
    };
    unsafe {
        if let Some(rows) = rows {
            records::xp_store_rows_save(0, json.as_ptr(), rows);
        } else {
            records::xp_store_state_save(state.as_ptr(), json.as_ptr());
        }
    }
    let ok = unsafe { records::xp_store_document(file.as_ptr(), true) == 0 };
    if !ok {
        log::with_tail(
            leaves,
            1,
            b"PERSIST",
            format_args!("Failed to write "),
            &[if rows.is_some() {
                b"/.crosspoint/recent.json"
            } else {
                b"/.crosspoint/state.json"
            }],
        );
    }
    unsafe {
        (leaves.store_document)(
            2,
            storage.as_mut_ptr().cast(),
            ptr::null_mut(),
            ptr::null_mut(),
            ptr::null_mut(),
        )
    };
    ok
}
pub(super) unsafe fn save_state(leaves: &XpEpubLeaves) {
    unsafe {
        save(leaves, None);
    }
}

pub(super) unsafe fn add(
    leaves: &XpEpubLeaves,
    resources: &app_abi::Resources,
    value: &app_abi::Book,
) {
    unsafe {
        with_rows(leaves, |rows| {
            records::prune(
                rows,
                |index| (resources.path_exists)(text(rows.get(index, 0))) != 0,
                |destination, source| (leaves.store_move_row)(destination, source),
            );
            let args = [
                bytes(value.path),
                bytes(value.title),
                bytes(value.author),
                bytes(value.cover),
            ];
            records::xp_store_mutate(rows, 0, args.as_ptr());
            save(leaves, Some(rows));
        })
    };
}
pub(super) unsafe fn remove(leaves: &XpEpubLeaves, path: app_abi::Text) -> bool {
    unsafe {
        with_rows(leaves, |rows| {
            if records::xp_store_mutate(rows, 2, &bytes(path)) == 0 {
                return false;
            }
            if !save(leaves, Some(rows)) {
                log::with_tail(
                    leaves,
                    1,
                    b"RBS",
                    format_args!("Failed to persist removal of recent book: "),
                    &[path.bytes().unwrap_or_default()],
                );
            }
            true
        })
    }
}
pub(super) unsafe fn update_path(
    leaves: &XpEpubLeaves,
    from: app_abi::Text,
    to: app_abi::Text,
    old_cache: app_abi::Text,
    new_cache: app_abi::Text,
) {
    unsafe {
        with_rows(leaves, |rows| {
            let index = records::xp_store_find(rows, 0, bytes(from));
            if index == usize::MAX {
                return;
            }
            rows.set(index, 0, bytes(to));
            let cover = rows.get(index, 3);
            if records::xp_store_prefix(cover, bytes(old_cache)) {
                let mut path = Text::default();
                if path
                    .assign_parts(&[
                        new_cache.bytes().unwrap_or_default(),
                        &text(cover).bytes().unwrap_or_default()[old_cache.len..],
                    ])
                    .is_err()
                {
                    return;
                }
                rows.set(index, 3, bytes(path.wire()));
            }
            save(leaves, Some(rows));
        })
    };
}
