use crate::{
    abi,
    owned::{Toolbar, ToolbarRow},
    owner_tests,
    physical::Physical,
    test_resources, tests,
    toolbar_view::Content,
};
use std::{cell::RefCell, sync::OnceLock};
use xp_ui::{
    ffi::{Paint, SceneView},
    reader_chrome::{list, toolbar},
    scene::{Kind, Rect},
};

#[derive(Default)]
struct Record {
    peak: u16,
    texts: Vec<Vec<u8>>,
    icons: Vec<u32>,
    fail: bool,
    owner: Option<*const Toolbar>,
}
thread_local! { static RECORD: RefCell<Record> = RefCell::default(); }
unsafe extern "C" fn width(t: abi::Text, _: i32, _: u8) -> i32 {
    (t.len * 8) as i32
}
unsafe extern "C" fn line(_: i32) -> i32 {
    24
}
unsafe extern "C" fn height(_: abi::Text, _: abi::TextLayout) -> i32 {
    24
}
unsafe extern "C" fn text(_: *const xp_ui::scene::Command, _: Rect, t: abi::Text) {
    assert_eq!(*t.data.add(t.len), 0);
    RECORD.with(|r| r.borrow_mut().texts.push(t.bytes().unwrap().to_vec()));
}
unsafe extern "C" fn icon(id: u32, _: Rect, _: Rect) {
    assert!((toolbar::CONTENTS..=toolbar::NEXT).contains(&id));
    RECORD.with(|r| r.borrow_mut().icons.push(id));
}
unsafe extern "C" fn theme(t: *mut list::Theme, toolbar: u8) {
    assert_eq!(toolbar, 1);
    *t = list::Theme {
        screen: Rect {
            w: 480,
            h: 800,
            ..Default::default()
        },
        safe: Rect {
            w: 480,
            h: 800,
            ..Default::default()
        },
        row_min: 48,
        row_gap: 4,
        row_padding: 4,
        row_inset: 8,
        scroll_width: 3,
        space_sm: 4,
        space_md: 8,
        space_lg: 16,
        ..Default::default()
    };
}
unsafe extern "C" fn paint(scene: *const SceneView, paint: *const Paint) -> u8 {
    let scene = &*scene;
    let paint = &*paint;
    RECORD.with(|r| {
        let mut r = r.borrow_mut();
        r.peak = r.peak.max(scene.command_count);
    });
    if RECORD.with(|r| r.borrow().fail) {
        return 0;
    }
    if let Some(owner) = RECORD.with(|r| r.borrow_mut().owner.take()) {
        (*owner).reset_navigation(8, 5);
    }
    for command in core::slice::from_raw_parts(scene.commands, usize::from(scene.command_count)) {
        if command.kind == Kind::TextHandle as u8 || command.kind == Kind::Bitmap as u8 {
            assert_eq!(
                paint.resource_valid.unwrap()(paint.context, command.resource, command.generation),
                1
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
fn services() -> &'static abi::Resources {
    static S: OnceLock<abi::Resources> = OnceLock::new();
    S.get_or_init(|| abi::Resources {
        text_width: width,
        line_height: line,
        text_box_height: height,
        text_draw: text,
        paint_scene: paint,
        buffer_allocate: tests::allocate,
        buffer_release: tests::release,
        ..test_resources::resources()
    })
}
fn physical() -> Physical {
    Physical {
        list_theme: theme,
        toolbar_icon: icon,
        ..crate::list_adapter_tests::physical()
    }
}
fn content(panel: bool, count: i32, selected: i32) -> Content<'static> {
    Content {
        panel,
        dense: false,
        active_tool: 1,
        progress_permille: 500,
        chapter: Some(c"Chapter"),
        page_info: Some(c"1/2   50%"),
        panel_title: Some(c"Contents"),
        count,
        selected,
    }
}
fn fill(index: i32, row: &mut ToolbarRow) -> bool {
    row.toggle = index % 2 == 0;
    row.checked = index % 4 == 0;
    row.label.assign(format!("Row {index}").as_bytes()).is_ok()
        && row.value.assign(b"Value").is_ok()
}
#[test]
fn toolbar_paints_native_resources_and_preserves_navigation_received_during_paint() {
    tests::reset(0);
    let owner = Toolbar::try_new(owner_tests::runtime()).unwrap();
    RECORD.with(|r| {
        *r.borrow_mut() = Record {
            owner: Some(&*owner),
            ..Default::default()
        }
    });
    unsafe {
        owner
            .render(services(), &physical(), &content(true, 1000, 0), &mut fill)
            .unwrap();
    }
    owner.model.with(|m| {
        assert!(m.ready);
        assert_eq!(m.navigation.selected, 8);
        assert_eq!(m.navigation.top, 5);
    });
    RECORD.with(|r| {
        assert!(r.borrow().texts.iter().any(|t| t == b"Contents"));
        assert!(r.borrow().icons.contains(&toolbar::CONTENTS));
    });
    unsafe {
        owner
            .render(services(), &physical(), &content(false, 0, -1), &mut fill)
            .unwrap();
    }
    RECORD.with(|r| {
        assert!(r.borrow().texts.iter().any(|t| t == b"Chapter"));
        assert!(r.borrow().icons.contains(&toolbar::CONTENTS));
    });
}
#[test]
fn toolbar_rejects_partial_rows_and_failed_paint_without_refresh() {
    tests::reset(0);
    RECORD.with(|r| *r.borrow_mut() = Record::default());
    let owner = Toolbar::try_new(owner_tests::runtime()).unwrap();
    assert!(unsafe {
        owner.render(
            services(),
            &physical(),
            &content(true, 1000, 0),
            &mut |index, row| index != 3 && fill(index, row),
        )
    }
    .is_err());
    owner.model.with(|m| assert!(!m.ready));
    unsafe {
        owner
            .render(services(), &physical(), &content(true, 1000, 0), &mut fill)
            .unwrap();
    }
    RECORD.with(|r| r.borrow_mut().fail = true);
    assert!(
        unsafe { owner.render(services(), &physical(), &content(true, 1000, 0), &mut fill) }
            .is_err()
    );
    owner.model.with(|m| {
        assert!(!m.ready);
        assert_eq!(m.contact.active, -1);
    });
}
