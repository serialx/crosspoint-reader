use super::{
    abi, app,
    clippings::{record::Record, Store},
    clippings_native::Native,
    clippings_service::{Gate, Phase, Service},
    sync::ModelCell,
};

// The supported firmware targets are C3 and S3 (scripts/build_rust.py).
#[cfg(target_arch = "xtensa")]
pub(super) type NativeStore = Store<Native, 64>;
#[cfg(not(target_arch = "xtensa"))]
pub(super) type NativeStore = Store<Native, 16>;
pub(super) type ClippingService = Service<NativeStore, ModelCell<Phase>>;

// ModelCell uses the app's paired critical-section callbacks around its phase.
unsafe impl Gate for ModelCell<Phase> {
    fn with<R>(&self, action: impl FnOnce(&mut Phase) -> R) -> R {
        ModelCell::with(self, action)
    }
}

fn with<R>(action: impl FnOnce(&mut NativeStore) -> R) -> Option<R> {
    app()?.clippings.with(action)
}

fn metadata<R>(action: impl FnOnce(&NativeStore) -> R) -> Option<R> {
    app()?.clippings.read(action)
}

fn storage<R>(action: impl FnOnce(&NativeStore) -> R) -> Option<R> {
    Some(app()?.clippings.operation()?.read(action))
}

// ABI pointers borrow valid caller storage for the duration of each call.
// No store reference or native handle is returned across this boundary.
#[no_mangle]
unsafe extern "C" fn xp_clippings_p18_load(
    path: abi::Text,
    title: abi::Text,
    author: abi::Text,
    kind: abi::Text,
) -> u8 {
    let (Some(path), Some(title), Some(author), Some(kind)) = (
        unsafe { path.bytes() },
        unsafe { title.bytes() },
        unsafe { author.bytes() },
        unsafe { kind.bytes() },
    ) else {
        return 0;
    };
    with(|store| store.load(path, title, author, kind)).unwrap_or(false) as u8
}
#[no_mangle]
extern "C" fn xp_clippings_p18_unload() {
    with(|store| store.unload());
}
#[no_mangle]
extern "C" fn xp_clippings_p18_count() -> u32 {
    metadata(|store| store.count() as u32).unwrap_or(0)
}
#[no_mangle]
unsafe extern "C" fn xp_clippings_p18_copy_row(index: u32, output: *mut Record) -> u8 {
    let Some(output) = (unsafe { output.as_mut() }) else {
        return 0;
    };
    metadata(|store| store.row(index as usize).copied())
        .flatten()
        .is_some_and(|row| {
            *output = row;
            true
        }) as u8
}
#[no_mangle]
unsafe extern "C" fn xp_clippings_p18_read_text(
    index: u32,
    output: *mut u8,
    capacity: u32,
    length: *mut u32,
) -> u8 {
    let Some(length) = (unsafe { length.as_mut() }) else {
        return 0;
    };
    *length = 0;
    if capacity as usize > isize::MAX as usize || (capacity != 0 && output.is_null()) {
        return 0;
    }
    let output = if capacity == 0 {
        &mut []
    } else {
        unsafe { core::slice::from_raw_parts_mut(output, capacity as usize) }
    };
    storage(|store| store.read_into(index as usize, output))
        .flatten()
        .is_some_and(|count| {
            *length = count as u32;
            true
        }) as u8
}
#[no_mangle]
extern "C" fn xp_clippings_p18_prepare_sync() -> u8 {
    with(|store| store.prepare_sync()).unwrap_or(false) as u8
}
#[no_mangle]
extern "C" fn xp_clippings_p18_finish_uploads() -> u8 {
    with(|store| store.finish_uploads()).unwrap_or(false) as u8
}
#[no_mangle]
unsafe extern "C" fn xp_clippings_p18_apply_remote(
    record: *const Record,
    text: abi::Text,
    deleted: u8,
) -> u8 {
    let (Some(record), Some(text)) = (unsafe { record.as_ref() }, unsafe { text.bytes() }) else {
        return 0;
    };
    with(|store| store.apply_remote(*record, text, deleted != 0)).unwrap_or(false) as u8
}
#[no_mangle]
unsafe extern "C" fn xp_clippings_p18_next_deletion(offset: *mut u32, output: *mut [u8; 65]) -> u8 {
    let (Some(offset), Some(output)) = (unsafe { offset.as_mut() }, unsafe { output.as_mut() })
    else {
        return 0;
    };
    output[0] = 0;
    storage(|store| store.next_deletion(offset, output)).unwrap_or(false) as u8
}
#[no_mangle]
extern "C" fn xp_clippings_p18_finish_deletions() -> u8 {
    storage(|store| store.finish_deletions()).unwrap_or(false) as u8
}
#[no_mangle]
unsafe extern "C" fn xp_clippings_p17_p18_move_book(from: abi::Text, to: abi::Text) -> u8 {
    let (Some(from), Some(to)) = (unsafe { from.bytes() }, unsafe { to.bytes() }) else {
        return 0;
    };
    storage(|store| store.move_book(from, to)).unwrap_or(false) as u8
}
#[no_mangle]
unsafe extern "C" fn xp_clippings_p17_p18_delete_for_path(path: abi::Text, kind: abi::Text) -> u8 {
    let (Some(path), Some(kind)) = (unsafe { path.bytes() }, unsafe { kind.bytes() }) else {
        return 0;
    };
    storage(|store| store.delete_for_path(path, kind)).unwrap_or(false) as u8
}
