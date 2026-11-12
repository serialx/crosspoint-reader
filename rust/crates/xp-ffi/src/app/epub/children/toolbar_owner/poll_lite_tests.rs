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

thread_local! {
    static REPAINTS: Cell<u32> = const { Cell::new(0) };
    static NOTE: Cell<u8> = const { Cell::new(0) };
    static RADIO_READS: Cell<u32> = const { Cell::new(0) };
    static RENDER_HELD: Cell<bool> = const { Cell::new(false) };
    static MODEL_HELD: Cell<bool> = const { Cell::new(false) };
    static RENDER_BLOCKED: Cell<bool> = const { Cell::new(false) };
}
static TOKEN: u8 = 0;
unsafe extern "C" fn note_lock() -> *mut c_void {
    assert!(!MODEL_HELD.with(Cell::get));
    if RENDER_BLOCKED.with(Cell::get) {
        return core::ptr::null_mut();
    }
    assert!(!RENDER_HELD.with(|held| held.replace(true)));
    (&TOKEN as *const u8).cast_mut().cast()
}
unsafe extern "C" fn note_unlock(token: *mut c_void) {
    assert_eq!(token, (&TOKEN as *const u8).cast_mut().cast());
    assert!(!MODEL_HELD.with(Cell::get));
    assert!(RENDER_HELD.with(|held| held.replace(false)));
}
unsafe extern "C" fn note_model_lock() {
    assert!(!MODEL_HELD.with(|held| held.replace(true)));
}
unsafe extern "C" fn note_model_unlock() {
    assert!(MODEL_HELD.with(|held| held.replace(false)));
}
unsafe extern "C" fn radio(out: *mut abi::XpEpubRadio, peer: *mut abi::XpEpubPeer) {
    assert!(peer.is_null());
    assert!(RENDER_HELD.with(Cell::get));
    assert!(!MODEL_HELD.with(Cell::get));
    RADIO_READS.with(|reads| reads.set(reads.get() + 1));
    unsafe {
        out.write(core::mem::zeroed());
        (*out).note = NOTE.with(Cell::get);
    }
}
unsafe extern "C" fn repaint_note(context: *mut c_void) {
    assert!(context.is_null());
    assert!(RENDER_HELD.with(Cell::get));
    assert!(!MODEL_HELD.with(Cell::get));
    REPAINTS.with(|value| value.set(value.get() + 1));
}
unsafe extern "C" fn unexpected_finish(_: *mut c_void) {
    panic!("unexpected finish");
}
unsafe extern "C" fn unexpected_push(_: *mut c_void, _: *mut c_void) {
    panic!("unexpected push");
}
unsafe extern "C" fn unexpected_reopen(_: *mut c_void, _: app_abi::Text) {
    panic!("unexpected reopen");
}
fn note_owner() -> Box<EpubSession> {
    REPAINTS.with(|value| value.set(0));
    RADIO_READS.with(|value| value.set(0));
    RENDER_BLOCKED.with(|value| value.set(false));
    const LEAVES: abi::XpEpubLeaves = abi::XpEpubLeaves {
        radio_status: radio,
        ..test_leaves::leaves()
    };
    static RUNTIME: OnceLock<app_abi::Runtime> = OnceLock::new();
    static RESOURCES: OnceLock<app_abi::Resources> = OnceLock::new();
    let mut owner = EpubSession::try_new(
        b"",
        false,
        0,
        &LEAVES,
        RUNTIME.get_or_init(|| app_abi::Runtime {
            render_try_lock: note_lock,
            render_unlock: note_unlock,
            model_lock: note_model_lock,
            model_unlock: note_model_unlock,
            ..test_resources::runtime()
        }),
        RESOURCES.get_or_init(test_resources::resources),
    )
    .unwrap();
    owner.navigation_hooks = Some(crate::app::epub::test_support::NavigationHooks {
        context: core::ptr::null_mut(),
        finish: unexpected_finish,
        push: unexpected_push,
        reopen: unexpected_reopen,
        update: repaint_note,
    });
    owner
}

#[test]
fn poll_lite_undrawn_note_never_polls_and_locked_render_defers() {
    let owner = note_owner();
    for note in 0..=3 {
        NOTE.with(|value| value.set(note));
        owner.loop_link_note();
        assert_eq!(owner.model(|state| state.link_note_drawn), None);
        assert_eq!(RADIO_READS.with(Cell::get), 0);
        assert_eq!(REPAINTS.with(Cell::get), 0);
        assert!(!RENDER_HELD.with(Cell::get));
    }
    owner.model(|state| state.link_note_drawn = Some(1));
    RENDER_BLOCKED.with(|value| value.set(true));
    owner.loop_link_note();
    assert_eq!(owner.model(|state| state.link_note_drawn), Some(1));
    assert_eq!(RADIO_READS.with(Cell::get), 0);
}

#[test]
fn poll_lite_drawn_note_tracks_appearance_change_disable_and_reenable() {
    let owner = note_owner();
    // A rendered empty title is tracked too: enabling can introduce a note.
    for (drawn, current) in [(0, 1), (1, 2), (2, 3), (3, 0), (0, 1), (1, 0)] {
        owner.model(|state| state.link_note_drawn = Some(drawn));
        NOTE.with(|value| value.set(drawn));
        let before = RADIO_READS.with(Cell::get);
        let repaints = REPAINTS.with(Cell::get);
        owner.loop_link_note();
        assert_eq!(owner.model(|state| state.link_note_drawn), Some(drawn));
        assert_eq!(REPAINTS.with(Cell::get), repaints);
        NOTE.with(|value| value.set(current));
        owner.loop_link_note();
        assert_eq!(owner.model(|state| state.link_note_drawn), None);
        assert_eq!(RADIO_READS.with(Cell::get), before + 2);
        assert_eq!(REPAINTS.with(Cell::get), repaints + 1);
        owner.loop_link_note();
        assert_eq!(RADIO_READS.with(Cell::get), before + 2);
        assert_eq!(REPAINTS.with(Cell::get), repaints + 1);
        assert!(!RENDER_HELD.with(Cell::get));
    }
}
