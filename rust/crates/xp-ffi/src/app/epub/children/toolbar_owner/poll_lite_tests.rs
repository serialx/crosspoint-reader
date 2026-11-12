use super::*;
use xp_ui::reader::input::{Query, Services};

thread_local! {
    static TABLE_READS: Cell<u32> = const { Cell::new(0) };
    static EDGE: Cell<bool> = const { Cell::new(false) };
    static ORIENTATION: Cell<u8> = const { Cell::new(0) };
    static BOARD_TOUCH: Cell<u8> = const { Cell::new(0) };
    static LONG_MENU: Cell<u8> = const { Cell::new(2) };
}

unsafe extern "C" fn table() -> *const Services {
    TABLE_READS.with(|n| n.set(n.get() + 1));
    const SERVICES: Services = Services {
        context: core::ptr::null_mut(),
        button: None,
        read: Some(consume),
        tap: None,
    };
    &SERVICES
}
unsafe extern "C" fn consume(context: *mut c_void, query: u8) -> u32 {
    assert!(context.is_null());
    assert_eq!(query, Query::BackGesture as u8);
    EDGE.with(|edge| u32::from(edge.replace(false)))
}
unsafe extern "C" fn settings(resolve: u8, out: *mut abi::XpEpubSettings) {
    assert_eq!(resolve, 0);
    unsafe {
        out.write(core::mem::zeroed());
        (*out).orientation = ORIENTATION.with(Cell::get);
        (*out).long_menu = LONG_MENU.with(Cell::get);
    }
}
unsafe extern "C" fn facts(out: *mut abi::XpEpubInputFacts) {
    unsafe {
        out.write(core::mem::zeroed());
        (*out).board_touch = BOARD_TOUCH.with(Cell::get);
    }
}
fn owner() -> Box<EpubSession> {
    const LEAVES: abi::XpEpubLeaves = abi::XpEpubLeaves {
        input_services: table,
        settings_snapshot: settings,
        input_facts: facts,
        ..test_leaves::leaves()
    };
    static RUNTIME: OnceLock<app_abi::Runtime> = OnceLock::new();
    static RESOURCES: OnceLock<app_abi::Resources> = OnceLock::new();
    EpubSession::try_new(
        b"",
        false,
        0,
        &LEAVES,
        RUNTIME.get_or_init(test_resources::runtime),
        RESOURCES.get_or_init(test_resources::resources),
    )
    .unwrap()
}

#[test]
fn poll_lite_table_cache_keeps_consuming_reads_live_and_is_per_session() {
    TABLE_READS.with(|n| n.set(0));
    let first = owner();
    for _ in 0..2 {
        EDGE.with(|edge| edge.set(true));
        assert_eq!(first.mapped_query(Query::BackGesture), 1);
        assert_eq!(first.mapped_query(Query::BackGesture), 0);
        assert_eq!(TABLE_READS.with(Cell::get), 1);
    }
    drop(first);
    let second = owner();
    EDGE.with(|edge| edge.set(true));
    assert_eq!(second.mapped_query(Query::BackGesture), 1);
    assert_eq!(TABLE_READS.with(Cell::get), 2);
}

#[test]
fn poll_lite_settings_and_non_idle_facts_are_never_cached() {
    let owner = owner();
    for orientation in [0, 3, 1, 2, 0] {
        ORIENTATION.with(|value| value.set(orientation));
        assert_eq!(owner.settings(false).orientation, orientation);
        for (menu, expected) in [(2, 400), (1, 0), (6, 400)] {
            LONG_MENU.with(|value| value.set(menu));
            BOARD_TOUCH.with(|value| value.set(0));
            assert_eq!(owner.confirm_long_press_threshold(), expected);
            BOARD_TOUCH.with(|value| value.set(1));
            assert_eq!(owner.confirm_long_press_threshold(), 0);
        }
    }
}
