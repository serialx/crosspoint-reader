use super::*;
use crate::app::epub::{
    children::definition_owned::DefinitionSession, render_resources::PrewarmStorage,
};
use xp_ui::{
    ffi::{Paint, SceneView},
    scene::{Command, Kind, Rect},
};
thread_local! {
    static RENDER_OWNER: Cell<*const DefinitionSession> = const { Cell::new(ptr::null()) };
    static PAINT_FAIL: Cell<bool> = const { Cell::new(false) };
    static SCANS: Cell<bool> = const { Cell::new(false) };
}
unsafe extern "C" fn begin(_: *mut PrewarmStorage) -> u8 {
    assert!(!SCANS.with(|v| v.replace(true)));
    event("scan");
    1
}
unsafe extern "C" fn finish(_: *mut PrewarmStorage) {
    assert!(SCANS.with(Cell::get));
    event("prewarm");
}
unsafe extern "C" fn drop_scan(_: *mut PrewarmStorage) {
    assert!(SCANS.with(|v| v.replace(false)));
    event("prewarm-drop");
}
unsafe extern "C" fn settings(resolve: u8, out: *mut crate::app::epub::abi::XpEpubSettings) {
    assert_eq!(resolve, 1);
    assert!(SCANS.with(Cell::get));
    unsafe {
        out.write(core::mem::zeroed());
        (*out).font = 7;
    }
}
unsafe extern "C" fn header(_: *const PayloadStorage, _: Rect, _: i32, _: i32) {
    event("header");
}
unsafe extern "C" fn body(_: *const PayloadStorage, _: i32, _: i32, _: i32, _: i32) {
    assert!(SCANS.with(Cell::get));
    event("body");
}
unsafe extern "C" fn footer(_: *const abi::Text) {
    event("footer");
}
unsafe extern "C" fn translate(_: u16) -> abi::Text {
    abi::Text::from_bytes(b"message")
}
unsafe extern "C" fn width(_: abi::Text, _: i32, _: u8) -> i32 {
    32
}
unsafe extern "C" fn height(_: i32) -> i32 {
    16
}
unsafe extern "C" fn text(_: *const Command, _: Rect, _: abi::Text) {
    event("message");
}
unsafe extern "C" fn popup(out: *mut xp_ui::dictionary::popup::Layout) {
    unsafe {
        out.write(xp_ui::dictionary::popup::Layout {
            screen: Rect {
                x: 0,
                y: 0,
                w: 480,
                h: 800,
            },
            top_ratio: 0.5,
            ..Default::default()
        })
    };
}
unsafe extern "C" fn refresh(value: u8) {
    assert_eq!(value, xp_ui::scene::Refresh::Fast as u8);
    assert!(SCANS.with(Cell::get));
    RENDER_OWNER.with(|v| {
        unsafe { &*v.get() }
            .model
            .with(|m| assert!(m.ready && !m.gate.failed()))
    });
    event("refresh");
}
unsafe extern "C" fn paint(scene: *const SceneView, paint: *const Paint) -> u8 {
    let scene = unsafe { &*scene };
    let paint = unsafe { &*paint };
    assert_eq!(scene.refresh, xp_ui::scene::Refresh::None as u8);
    assert!(scene.command_count <= 4);
    for command in
        unsafe { core::slice::from_raw_parts(scene.commands, usize::from(scene.command_count)) }
    {
        if command.kind == Kind::Page as u8 || command.kind == Kind::TextHandle as u8 {
            assert_eq!(
                unsafe {
                    paint.resource_valid.unwrap()(
                        paint.context,
                        command.resource,
                        command.generation,
                    )
                },
                1
            );
            unsafe { paint.resource.unwrap()(paint.context, ptr::null(), command, command.rect) };
        }
    }
    u8::from(!PAINT_FAIL.with(Cell::get))
}
#[test]
fn render_publishes_before_refresh_and_drops_prewarm_after_both_scenes_or_rejection() {
    static PHYSICAL: DefinitionPhysical = {
        let mut p = physical();
        p.draw_header = header;
        p.draw_body = body;
        p
    };
    let mut leaves = leaves();
    leaves.input_snapshot = snapshot;
    leaves.settings_snapshot = settings;
    leaves.fonts_prewarm_begin = begin;
    leaves.fonts_prewarm_finish_scan = finish;
    leaves.fonts_prewarm_drop = drop_scan;
    leaves.child_selection_physical.dictionary_popup_layout = popup;
    let mut services = test_resources::resources();
    services.paint_scene = paint;
    services.refresh = refresh;
    services.translate = translate;
    services.footer_draw = footer;
    services.text_width = width;
    services.line_height = height;
    services.text_draw = text;
    for message in [false, true] {
        for failed in [false, true] {
            let owner = unsafe {
                DefinitionOwner::try_plain(
                    runtime(),
                    &PHYSICAL,
                    abi::Text::from_bytes(b"title"),
                    0,
                    fill,
                    ptr::null_mut(),
                )
            }
            .unwrap();
            RENDER_OWNER.with(|v| v.set(owner.pointer().as_ptr()));
            owner
                .model
                .with(|m| m.state.message_visible = u8::from(message));
            PAINT_FAIL.with(|v| v.set(failed));
            events();
            let guard = DefinitionSession::lock(runtime());
            assert_eq!(unsafe { owner.render(&services, &leaves) }.is_err(), failed);
            drop(guard);
            let seen = events();
            assert_eq!(
                &seen[..6],
                &["header", "scan", "body", "prewarm", "body", "footer"]
            );
            assert_eq!(seen.last(), Some(&"prewarm-drop"));
            assert_eq!(seen.contains(&"refresh"), !failed);
            assert_eq!(seen.contains(&"message"), message && !failed);
            owner.model.with(|m| {
                assert_eq!(m.ready, !failed);
                assert_eq!(m.gate.failed(), failed);
                assert_eq!(m.discard_pending, failed);
            });
            if failed {
                unsafe {
                    (*owner.navigation.get()).pending.assign(b"stale");
                }
                events();
                assert!(unsafe { owner.input(runtime(), &leaves, true) }.is_null());
                assert!(unsafe { (*owner.navigation.get()).pending.is_empty() });
                assert!(!events().contains(&"find"));
            }
            RENDER_OWNER.with(|v| v.set(ptr::null()));
        }
    }
}

#[cfg(feature = "app-test")]
#[path = "definition_frontlight_tests.rs"]
mod frontlight_tests;
