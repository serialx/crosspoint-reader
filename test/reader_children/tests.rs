use super::{abi, commands::Commands, test_resources};
use std::{
    alloc::{alloc, dealloc, Layout},
    cell::RefCell,
    collections::BTreeMap,
    sync::OnceLock,
};

#[derive(Default)]
struct Trace {
    calls: usize,
    fail: usize,
    live: BTreeMap<usize, (*mut u8, usize)>,
    events: Vec<(bool, usize)>,
}
thread_local! { static TRACE: RefCell<Trace> = RefCell::default(); }
pub(crate) unsafe extern "C" fn allocate(domain: u8, bytes: usize) -> *mut u8 {
    assert_eq!(domain, abi::MEMORY_DEFAULT);
    TRACE.with(|trace| {
        let mut trace = trace.borrow_mut();
        trace.calls += 1;
        trace.events.push((true, bytes));
        if trace.calls == trace.fail {
            return std::ptr::null_mut();
        }
        let pointer = alloc(Layout::from_size_align(bytes, 8).unwrap());
        assert!(!pointer.is_null());
        pointer.write_bytes(0xa5, bytes);
        trace.live.insert(pointer.addr(), (pointer, bytes));
        pointer
    })
}
pub(crate) unsafe extern "C" fn release(domain: u8, pointer: *mut u8) {
    assert_eq!(domain, abi::MEMORY_DEFAULT);
    TRACE.with(|trace| {
        let mut trace = trace.borrow_mut();
        let (original, bytes) = trace.live.remove(&pointer.addr()).unwrap();
        assert_eq!(pointer, original);
        trace.events.push((false, bytes));
        dealloc(pointer, Layout::from_size_align(bytes, 8).unwrap());
    });
}
fn resources() -> &'static abi::Resources {
    static RESOURCES: OnceLock<abi::Resources> = OnceLock::new();
    RESOURCES.get_or_init(|| abi::Resources {
        buffer_allocate: allocate,
        buffer_release: release,
        ..test_resources::resources()
    })
}
pub(crate) fn reset(fail: usize) {
    TRACE.with(|trace| {
        assert!(trace.borrow().live.is_empty());
        *trace.borrow_mut() = Trace {
            fail,
            ..Trace::default()
        };
    });
}

#[test]
fn qr_command_block_cannot_grow_beyond_its_four_native_commands() {
    reset(0);
    let mut commands = Commands::fixed(4);
    assert!(commands.recover(resources()));
    assert_eq!(commands.as_mut_slice().len(), 4);
    assert!(!commands.grow(resources()));
    TRACE.with(|trace| assert_eq!(trace.borrow().calls, 1));
    drop(commands);
    TRACE.with(|trace| assert!(trace.borrow().live.is_empty()));
}

#[test]
fn native_command_starts_growth_cap_and_repaint_reuse() {
    for start in [48, 32, 24] {
        reset(0);
        let mut commands = Commands::new(start);
        assert!(commands.recover(resources()));
        assert_eq!(commands.as_mut_slice().len(), usize::from(start));
        assert!(commands
            .as_mut_slice()
            .iter()
            .all(|c| *c == Default::default()));
        commands.as_mut_slice()[0].resource = 77;
        assert!(commands.recover(resources()));
        assert_eq!(commands.as_mut_slice()[0].resource, 77);
        TRACE.with(|t| assert_eq!(t.borrow().calls, 1));
        let mut expected = start;
        while expected < 128 {
            expected = (expected + 16).min(128);
            assert!(commands.grow(resources()));
            assert_eq!(commands.as_mut_slice().len(), usize::from(expected));
        }
        assert!(!commands.grow(resources()));
        assert_eq!(commands.as_mut_slice().len() * 32, 4096);
        drop(commands);
        reset(0);
    }
}

#[test]
fn failed_command_growth_drops_unpublished_scratch_before_allocation_and_recovers() {
    reset(2);
    let mut commands = Commands::new(48);
    assert!(commands.recover(resources()));
    assert!(!commands.grow(resources()));
    assert!(commands.as_mut_slice().is_empty());
    TRACE.with(|t| {
        let t = t.borrow();
        assert!(t.live.is_empty());
        assert_eq!(t.events, [(true, 1536), (false, 1536), (true, 2048)]);
    });
    assert!(commands.recover(resources()));
    assert_eq!(commands.as_mut_slice().len(), 64);
    commands.clear();
    assert!(commands.as_mut_slice().is_empty());
    assert!(!commands.recover(resources()));
    drop(commands);
    reset(0);
}

#[test]
fn initial_command_failure_is_fallible_and_retry_keeps_the_native_capacity() {
    reset(1);
    let mut commands = Commands::new(32);
    assert!(!commands.recover(resources()));
    assert!(commands.as_mut_slice().is_empty());
    assert!(commands.recover(resources()));
    assert_eq!(commands.as_mut_slice().len(), 32);
    drop(commands);
    reset(0);
}

pub(crate) fn allocation_events() -> Vec<(bool, usize)> {
    TRACE.with(|trace| trace.borrow().events.clone())
}
