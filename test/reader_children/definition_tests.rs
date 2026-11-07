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
        definition: crate::definition_physical::DefinitionPhysical::unused(),
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

thread_local! {
    static TRANSFER_LOG: std::cell::RefCell<Vec<&'static str>> = const { std::cell::RefCell::new(Vec::new()) };
    static TRANSFER_LENGTH: Cell<usize> = const { Cell::new(0) };
}
fn transfer_event(event: &'static str) {
    TRANSFER_LOG.with(|log| log.borrow_mut().push(event));
}
unsafe extern "C" fn release_transfer(data: *mut u8, capacity: u32) {
    transfer_event("free");
    unsafe {
        std::alloc::dealloc(
            data,
            std::alloc::Layout::array::<u8>(capacity as usize).unwrap(),
        )
    };
}
// A labeled native witness: C++ string lifetime is covered by the C++ composition.
unsafe extern "C" fn transfer_bridge(
    child: *mut c_void,
    pop: abi::DefinitionPop,
    sink: abi::DefinitionSelectionSink,
    context: *mut c_void,
) {
    assert_eq!(
        unsafe { (*child.cast::<Native>()).self_pointer.cast::<c_void>() },
        child
    );
    transfer_event("take");
    let parent = unsafe { pop(context) };
    transfer_event("release-pages");
    if !parent.is_null() {
        let length = TRANSFER_LENGTH.with(Cell::get);
        let mut text = abi::DefinitionOwnedText {
            data: ptr::null_mut(),
            len: 0,
            capacity: 0,
            release: None,
        };
        if length != 0 {
            transfer_event("allocate");
            let data =
                unsafe { std::alloc::alloc(std::alloc::Layout::array::<u8>(length + 1).unwrap()) };
            assert!(!data.is_null());
            unsafe {
                ptr::write_bytes(data, b'x', length);
                data.add(length).write(0);
            }
            text = abi::DefinitionOwnedText {
                data,
                len: length as u32,
                capacity: (length + 1) as u32,
                release: Some(release_transfer),
            };
        }
        let data = text.data;
        let capacity = text.capacity;
        let result = abi::DefinitionSelection {
            text,
            start_page: 1,
            end_page: 2,
            start_word: 3,
            end_word: 4,
            status: 1,
            reserved: [0; 3],
        };
        if unsafe { sink(context, parent, result) } == 0 && !data.is_null() {
            unsafe { release_transfer(data, capacity) };
        }
    }
    transfer_event("native-result-drop");
}
#[test]
fn definition_transfer_owns_text_after_child_destruction_and_frees_once() {
    for length in [0, 1, 15, 4096] {
        TRANSFER_LOG.with(|log| log.borrow_mut().clear());
        TRANSFER_LENGTH.with(|v| v.set(length));
        let mut state = Fill {
            drops: Cell::new(0),
            fail: false,
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
        }
        .unwrap();
        let target = owner.handle();
        let parent = Box::new(Cell::new(0));
        let parent_target = core::ptr::NonNull::from(parent.as_ref()).cast();
        unsafe {
            Definition::with_selection_result(
                target,
                transfer_bridge,
                || {
                    drop(owner);
                    transfer_event("child-drop");
                    Some(parent_target)
                },
                |target, result| {
                    assert_eq!(state.drops.get(), 1);
                    assert_eq!(target, parent_target);
                    transfer_event("rust-move");
                    drop(parent); // The continuation may destroy its parent as well as the child.
                    drop(result);
                },
            );
        }
        let mut expected = vec!["take", "child-drop", "release-pages"];
        if length != 0 {
            expected.push("allocate");
        }
        expected.push("rust-move");
        if length != 0 {
            expected.push("free");
        }
        expected.push("native-result-drop");
        TRANSFER_LOG.with(|log| assert_eq!(*log.borrow(), expected));
    }
}
#[test]
fn definition_body_uses_initialized_layout_and_no_header_query() {
    unsafe extern "C" fn layout(
        out: *mut xp_ui::dictionary::definition::Layout,
        x: i32,
        y: i32,
    ) -> u8 {
        assert_eq!((x, y), (-1, -1));
        unsafe {
            out.write(xp_ui::dictionary::definition::Layout {
                screen: xp_ui::scene::Rect {
                    x: 0,
                    y: 0,
                    w: 480,
                    h: 800,
                },
                left: 13,
                right: 17,
                top: 19,
                bottom: 23,
                side_padding: 7,
                top_padding: 5,
                header_height: 41,
                spacing: 3,
                hints_height: 33,
                ..Default::default()
            })
        };
        1
    }
    let mut physical = crate::definition_physical::DefinitionPhysical::unused();
    physical.layout = layout;
    let body = unsafe { physical.body() };
    assert_eq!((body.x, body.y, body.w, body.h), (20, 68, 436, 696));
}
