use crate::{
    abi, owned::Qr, owner_tests, physical::Physical, resources::Text, test_resources, tests,
};
use core::ffi::c_void;
use std::{cell::RefCell, sync::OnceLock};
use xp_core::i18n::ids::*;
use xp_ui::{
    ffi::{Paint, SceneView},
    reader_chrome::list,
    scene::{Kind, Rect},
    utility,
};

#[derive(Default)]
struct Record {
    payload: usize,
    calls: Vec<&'static str>,
    fail: bool,
}
thread_local! { static RECORD: RefCell<Record> = RefCell::default(); }
unsafe extern "C" fn translate(key: u16) -> abi::Text {
    abi::Text::from_terminated(match key {
        STR_DISPLAY_QR => b"Display QR code\0",
        STR_BACK => b"Back\0",
        _ => panic!("unexpected key"),
    })
}
unsafe extern "C" fn header(_: Rect, title: abi::Text, list: u8) {
    assert_eq!(list, 0);
    assert_eq!(title.bytes().unwrap(), b"Display QR code");
    RECORD.with(|r| r.borrow_mut().calls.push("header"));
}
unsafe extern "C" fn footer(clip: Rect, labels: *const abi::Text) {
    assert_eq!(clip.w, 480);
    assert_eq!((*labels).bytes().unwrap(), b"Back");
    for i in 1..4 {
        assert!((*labels.add(i)).bytes().unwrap().is_empty());
    }
    RECORD.with(|r| r.borrow_mut().calls.push("footer"));
}
unsafe extern "C" fn qr(_: Rect, payload: abi::Text) {
    RECORD.with(|r| {
        let mut r = r.borrow_mut();
        assert_eq!(payload.data.addr(), r.payload);
        assert_eq!(payload.bytes().unwrap(), b"owned payload");
        r.calls.push("qr");
    });
}
unsafe extern "C" fn refresh(_: u8) {
    RECORD.with(|r| r.borrow_mut().calls.push("refresh"));
}
unsafe extern "C" fn paint(scene: *const SceneView, paint: *const Paint) -> u8 {
    if RECORD.with(|r| r.borrow().fail) {
        return 0;
    }
    let scene = &*scene;
    let paint = &*paint;
    assert_eq!(scene.command_count, 4);
    for command in core::slice::from_raw_parts(scene.commands, 4) {
        if command.kind == Kind::Bitmap as u8 {
            assert_eq!(
                paint.resource_valid.unwrap()(paint.context, command.resource, 1),
                1
            );
            assert_eq!(
                paint.resource_valid.unwrap()(paint.context, command.resource, 2),
                0
            );
            paint.resource.unwrap()(
                paint.context,
                core::ptr::null(),
                command,
                Rect {
                    w: 480,
                    h: 800,
                    ..Default::default()
                },
            );
        }
    }
    1
}
unsafe extern "C" fn list_theme(_: *mut list::Theme, _: u8) {
    panic!("unused")
}
unsafe extern "C" fn layout(_: *mut utility::Layout) -> u8 {
    panic!("unused")
}
unsafe extern "C" fn icon(_: u32, _: Rect, _: Rect) {
    panic!("unused")
}
unsafe extern "C" fn clear() {
    panic!("unused")
}
unsafe extern "C" fn prewarm(
    _: i32,
    _: *const c_void,
    _: unsafe extern "C" fn(*const c_void, u32) -> abi::Text,
    _: u32,
) {
    panic!("unused")
}
fn physical() -> Physical {
    Physical {
        list_theme,
        utility_layout: layout,
        header,
        utility_footer: footer,
        qr,
        toolbar_icon: icon,
        clear_fonts: clear,
        prewarm_rows: prewarm,
    }
}
fn services() -> &'static abi::Resources {
    static SERVICES: OnceLock<abi::Resources> = OnceLock::new();
    SERVICES.get_or_init(|| abi::Resources {
        translate,
        paint_scene: paint,
        refresh,
        buffer_allocate: tests::allocate,
        buffer_release: tests::release,
        ..test_resources::resources()
    })
}
fn geometry() -> utility::Layout {
    utility::Layout {
        screen: Rect {
            w: 480,
            h: 800,
            ..Default::default()
        },
        top_padding: 8,
        header_height: 40,
        spacing: 8,
        ..Default::default()
    }
}
#[test]
fn qr_borrows_moved_payload_and_reuses_only_four_commands() {
    tests::reset(0);
    let owner = Qr::try_new(
        owner_tests::runtime(),
        Text::copy(b"owned payload").unwrap(),
    )
    .unwrap();
    RECORD.with(|r| {
        *r.borrow_mut() = Record {
            payload: owner.payload.bytes().as_ptr().addr(),
            ..Default::default()
        }
    });
    for _ in 0..2 {
        assert_eq!(
            unsafe { owner.render(services(), &physical(), |out| *out = geometry()) }.unwrap(),
            4
        );
    }
    RECORD.with(|r| {
        assert_eq!(
            r.borrow().calls,
            ["header", "qr", "footer", "refresh", "header", "qr", "footer", "refresh"]
        )
    });
    drop(owner);
    assert_eq!(tests::allocation_events(), [(true, 128), (false, 128)]);
}
#[test]
fn qr_allocation_and_paint_failure_do_not_refresh_or_copy_payload() {
    tests::reset(1);
    let owner = Qr::try_new(
        owner_tests::runtime(),
        Text::copy(b"owned payload").unwrap(),
    )
    .unwrap();
    RECORD.with(|r| {
        *r.borrow_mut() = Record {
            payload: owner.payload.bytes().as_ptr().addr(),
            fail: true,
            ..Default::default()
        }
    });
    assert!(unsafe { owner.render(services(), &physical(), |out| *out = geometry()) }.is_err());
    assert!(unsafe { owner.render(services(), &physical(), |out| *out = geometry()) }.is_err());
    RECORD.with(|r| assert!(r.borrow().calls.is_empty()));
    drop(owner);
    assert_eq!(
        tests::allocation_events(),
        [(true, 128), (true, 128), (false, 128)]
    );
}

#[test]
fn failed_qr_paint_suppresses_old_release_after_recovery() {
    use xp_ui::model::{EffectKind, Input, BACK, CONFIRM, TOUCH_HELD, TOUCH_RELEASED};
    tests::reset(0);
    let owner = Qr::try_new(
        owner_tests::runtime(),
        Text::copy(b"owned payload").unwrap(),
    )
    .unwrap();
    RECORD.with(|r| {
        *r.borrow_mut() = Record {
            payload: owner.payload.bytes().as_ptr().addr(),
            fail: true,
            ..Default::default()
        }
    });
    assert!(unsafe { owner.render(services(), &physical(), |out| *out = geometry()) }.is_err());
    assert!(
        owner
            .input(Input {
                held: CONFIRM,
                touch_flags: TOUCH_HELD,
                ..Default::default()
            })
            .retry
    );
    assert_eq!(
        owner
            .input(Input {
                released: BACK,
                ..Default::default()
            })
            .effect
            .kind,
        EffectKind::Cancel as u8
    );
    owner.input(Input {
        held: CONFIRM,
        touch_flags: TOUCH_HELD,
        ..Default::default()
    });
    RECORD.with(|r| r.borrow_mut().fail = false);
    assert!(unsafe { owner.render(services(), &physical(), |out| *out = geometry()) }.is_ok());
    assert_eq!(
        owner
            .input(Input {
                released: CONFIRM,
                touch_flags: TOUCH_RELEASED,
                ..Default::default()
            })
            .effect
            .kind,
        EffectKind::None as u8
    );
    assert_eq!(
        owner
            .input(Input {
                released: CONFIRM,
                ..Default::default()
            })
            .effect
            .kind,
        EffectKind::Cancel as u8
    );
}
