use crate::{
    abi, definition::Definition, owner_tests::fail_next_allocation, selection_physical::*,
};
use core::{cell::Cell, ffi::c_void, ptr};

#[repr(C)]
struct Native {
    self_pointer: *mut DefinitionStorage,
    drops: *const Cell<usize>,
    text: [u8; 16],
}
struct Fill {
    drops: Cell<usize>,
    fail: bool,
    calls: Cell<usize>,
}
unsafe extern "C" fn fill(context: *mut c_void, text: *mut u8, length: u32) -> u8 {
    let state = unsafe { &*context.cast::<Fill>() };
    state.calls.set(state.calls.get() + 1);
    if state.fail {
        return 0;
    }
    assert_eq!(length, 5);
    unsafe { ptr::copy_nonoverlapping(b"hello".as_ptr(), text, 5) };
    1
}
unsafe extern "C" fn plain(
    storage: *mut DefinitionStorage,
    _: abi::Text,
    length: u32,
    fill: DefinitionFill,
    context: *mut c_void,
) -> *mut c_void {
    let mut text = [0; 16];
    if unsafe { fill(context, text.as_mut_ptr(), length) } == 0 {
        return ptr::null_mut();
    }
    let state = unsafe { &*context.cast::<Fill>() };
    unsafe {
        storage.cast::<Native>().write(Native {
            self_pointer: storage,
            drops: &state.drops,
            text,
        })
    };
    storage.cast()
}
unsafe extern "C" fn destroy(storage: *mut DefinitionStorage) {
    let native = unsafe { &*storage.cast::<Native>() };
    assert_eq!(native.self_pointer, storage);
    assert_eq!(
        unsafe { (*native.self_pointer.cast::<Native>()).text },
        native.text
    );
    let drops = unsafe { &*native.drops };
    drops.set(drops.get() + 1);
}
fn physical() -> Physical {
    Physical {
        definition_plain: plain,
        definition_drop: destroy,
        lookup_init: unused_init,
        lookup_drop: unused_drop,
        lookup_open: unused_result,
        lookup_index: unused_result,
        lookup_find: unused_result,
        lookup_discard: unused_drop,
        definition_lookup: unused_lookup,
        directional_hints: unused_hints,
        dictionary_popup_layout: unused_layout,
        wait_paint: unused_wait,
    }
}
#[test]
fn inline_definition_target_survives_owner_moves_and_drops_once() {
    let mut fill_state = Fill {
        drops: Cell::new(0),
        fail: false,
        calls: Cell::new(0),
    };
    let ops = physical();
    let owner = unsafe {
        Definition::try_plain(
            &ops,
            abi::Text::from_bytes(b"title"),
            5,
            fill,
            (&mut fill_state as *mut Fill).cast(),
        )
    }
    .unwrap();
    let handle = owner.handle();
    let mut parents = Vec::new();
    parents.push(owner);
    parents.reserve(32);
    let owner = parents.pop().unwrap();
    assert_eq!(owner.handle(), handle);
    let native = unsafe { &*handle.as_ptr().cast::<Native>() };
    assert_eq!(&native.text[..5], b"hello");
    assert_eq!(native.self_pointer.cast::<c_void>(), handle.as_ptr());
    drop(owner);
    assert_eq!(fill_state.drops.get(), 1);
}
#[test]
fn failed_fill_never_destroys_an_unconstructed_native_viewer() {
    let mut state = Fill {
        drops: Cell::new(0),
        fail: true,
        calls: Cell::new(0),
    };
    let owner = unsafe {
        Definition::try_plain(
            &physical(),
            abi::Text::from_bytes(b"title"),
            5,
            fill,
            (&mut state as *mut Fill).cast(),
        )
    };
    assert!(owner.is_none());
    assert_eq!(state.drops.get(), 0);
    assert_eq!(state.calls.get(), usize::from(state.fail));
}
#[test]
fn failed_definition_owner_allocation_does_not_call_fill() {
    let mut state = Fill {
        drops: Cell::new(0),
        fail: false,
        calls: Cell::new(0),
    };
    let ops = physical();
    fail_next_allocation();
    let owner = unsafe {
        Definition::try_plain(
            &ops,
            abi::Text::from_bytes(b"title"),
            5,
            fill,
            (&mut state as *mut Fill).cast(),
        )
    };
    assert!(owner.is_none());
    assert_eq!(state.drops.get(), 0);
    assert_eq!(state.calls.get(), usize::from(state.fail));
}

unsafe extern "C" fn unused_init(_: *mut LookupStorage, _: abi::Text) {
    unreachable!()
}
unsafe extern "C" fn unused_drop(_: *mut LookupStorage) {
    unreachable!()
}
unsafe extern "C" fn unused_result(_: *mut LookupStorage, _: *mut u8) -> u8 {
    unreachable!()
}
unsafe extern "C" fn unused_lookup(
    _: *mut DefinitionStorage,
    _: *mut LookupStorage,
) -> *mut c_void {
    unreachable!()
}
unsafe extern "C" fn unused_hints(_: *const abi::Text) {
    unreachable!()
}
unsafe extern "C" fn unused_layout(_: *mut xp_ui::dictionary::popup::Layout) {
    unreachable!()
}
unsafe extern "C" fn unused_wait() {
    unreachable!()
}
