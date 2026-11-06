use crate::{
    abi,
    chapter_view::ChapterBook,
    menu::{Flags, Items},
    menu_view::Toggles,
    owned::{Chapters, Menu},
    owner_tests,
    physical::Physical,
    resources::Text,
    test_resources, tests,
};
use core::ffi::c_void;
use std::{cell::RefCell, sync::OnceLock};
use xp_ui::{
    ffi::{Paint, SceneView},
    reader_chrome::list,
    scene::{Kind, Rect},
    utility,
};

#[derive(Default)]
struct Record {
    clear: usize,
    prewarms: Vec<Vec<Vec<u8>>>,
    refresh: usize,
    fail_paint: bool,
    mutate_menu: Option<*const Menu>,
    peak: u16,
    selection: u8,
    touch: u8,
    texts: Vec<Vec<u8>>,
}
thread_local! { static RECORD: RefCell<Record> = RefCell::default(); }
unsafe extern "C" fn translate(_: u16) -> abi::Text {
    abi::Text::from_terminated(b"translated label\0")
}
unsafe extern "C" fn width(text: abi::Text, _: i32, _: u8) -> i32 {
    assert_eq!(*text.data.add(text.len), 0);
    (text.len * 8) as i32
}
unsafe extern "C" fn height(_: abi::Text, _: abi::TextLayout) -> i32 {
    24
}
unsafe extern "C" fn line(_: i32) -> i32 {
    24
}
unsafe extern "C" fn text(_: *const xp_ui::scene::Command, _: Rect, value: abi::Text) {
    RECORD.with(|r| r.borrow_mut().texts.push(value.bytes().unwrap().to_vec()));
}
unsafe extern "C" fn footer(_: *const abi::Text) {}
unsafe extern "C" fn refresh(_: u8) {
    RECORD.with(|r| r.borrow_mut().refresh += 1);
}
unsafe extern "C" fn paint(scene: *const SceneView, paint: *const Paint) -> u8 {
    let scene = &*scene;
    let paint = &*paint;
    RECORD.with(|r| {
        let mut r = r.borrow_mut();
        r.peak = r.peak.max(scene.command_count);
    });
    if RECORD.with(|r| r.borrow().fail_paint) {
        return 0;
    }
    let owner = RECORD.with(|r| r.borrow_mut().mutate_menu.take());
    if let Some(owner) = owner {
        (*owner).model.with(|m| {
            m.list.state.list.selected = 7;
            m.list.state.list.navigation_revision += 1;
            m.list.state.list.pending_scroll += 3;
        });
    }
    for c in core::slice::from_raw_parts(scene.commands, usize::from(scene.command_count)) {
        if matches!(c.kind, k if k == Kind::TextHandle as u8 || k == Kind::Page as u8 || k == Kind::Bitmap as u8)
        {
            assert_eq!(
                paint.resource_valid.unwrap()(paint.context, c.resource, c.generation),
                1
            );
            paint.resource.unwrap()(
                paint.context,
                core::ptr::null(),
                c,
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
unsafe extern "C" fn theme(out: *mut list::Theme, toolbar: u8) {
    assert_eq!(toolbar, 0);
    let (selection, touch) = RECORD.with(|r| (r.borrow().selection, r.borrow().touch));
    *out = list::Theme {
        screen: Rect {
            w: 480,
            h: 800,
            ..Default::default()
        },
        safe: Rect {
            w: 480,
            h: 760,
            ..Default::default()
        },
        row_min: 48,
        row_gap: 4,
        row_padding: 4,
        row_inset: 8,
        header_height: 32,
        tab_height: 32,
        scroll_width: 3,
        selection,
        touch,
        ..Default::default()
    };
}
unsafe extern "C" fn header(_: Rect, _: abi::Text, list: u8) {
    assert_eq!(list, 1);
}
unsafe extern "C" fn clear() {
    RECORD.with(|r| r.borrow_mut().clear += 1);
}
unsafe extern "C" fn prewarm(
    _: i32,
    context: *const c_void,
    text: unsafe extern "C" fn(*const c_void, u32) -> abi::Text,
    count: u32,
) {
    assert!(count <= 24);
    let values = (0..count)
        .map(|i| text(context, i).bytes().unwrap().to_vec())
        .collect();
    RECORD.with(|r| r.borrow_mut().prewarms.push(values));
}
unsafe extern "C" fn unused_layout(_: *mut utility::Layout) -> u8 {
    panic!("unused")
}
unsafe extern "C" fn unused_footer(_: Rect, _: *const abi::Text) {
    panic!("unused")
}
unsafe extern "C" fn unused_qr(_: Rect, _: abi::Text) {
    panic!("unused")
}
unsafe extern "C" fn unused_icon(_: u32, _: Rect, _: Rect) {
    panic!("unused")
}
pub(crate) fn physical() -> Physical {
    Physical {
        list_theme: theme,
        utility_layout: unused_layout,
        header,
        utility_footer: unused_footer,
        qr: unused_qr,
        toolbar_icon: unused_icon,
        clear_fonts: clear,
        prewarm_rows: prewarm,
    }
}
fn services() -> &'static abi::Resources {
    static SERVICES: OnceLock<abi::Resources> = OnceLock::new();
    SERVICES.get_or_init(|| abi::Resources {
        translate,
        text_width: width,
        text_box_height: height,
        line_height: line,
        text_draw: text,
        footer_draw: footer,
        refresh,
        paint_scene: paint,
        buffer_allocate: tests::allocate,
        buffer_release: tests::release,
        ..test_resources::resources()
    })
}
fn menu() -> Box<Menu> {
    Menu::try_new(
        owner_tests::runtime(),
        Items::new(Flags {
            footnotes: true,
            bookmarks: true,
            clippings: true,
            frontlight: true,
            bluetooth: true,
            dictionary: true,
        }),
        Text::copy(b"Book title").unwrap(),
        Text::copy(b"Chapter1/2 Book50%").unwrap(),
        0,
    )
    .unwrap()
}
#[test]
fn menu_publishes_hits_without_overwriting_concurrent_navigation_and_rejects_failed_paint() {
    tests::reset(0);
    let owner = menu();
    RECORD.with(|r| {
        *r.borrow_mut() = Record {
            mutate_menu: Some(&*owner),
            ..Default::default()
        }
    });
    unsafe {
        owner
            .render(
                services(),
                &physical(),
                Toggles {
                    night: false,
                    bluetooth: true,
                    frontlight: false,
                },
            )
            .unwrap();
    }
    owner.model.with(|m| {
        assert!(m.list.ready);
        assert_eq!(m.list.state.list.selected, 7);
        assert_eq!(m.list.state.list.pending_scroll, 3);
    });
    RECORD.with(|r| {
        assert_eq!(r.borrow().refresh, 1);
        r.borrow_mut().fail_paint = true;
    });
    assert!(unsafe {
        owner.render(
            services(),
            &physical(),
            Toggles {
                night: false,
                bluetooth: true,
                frontlight: false,
            },
        )
    }
    .is_err());
    owner
        .model
        .with(|m| assert!(!m.list.ready && m.list.gate.failed()));
    RECORD.with(|r| assert_eq!(r.borrow().refresh, 1));
}
#[test]
fn menu_host_command_bound_for_all_values_and_selection_styles() {
    tests::reset(0);
    RECORD.with(|r| *r.borrow_mut() = Record::default());
    let owner = menu();
    for style in 0..4 {
        for touch in 0..2 {
            for orientation in 0..4 {
                for page_turn in 0..5 {
                    for selected in 0..19 {
                        for toggles in 0..8 {
                            RECORD.with(|r| {
                                let mut r = r.borrow_mut();
                                r.selection = style;
                                r.touch = touch;
                                r.texts.clear();
                            });
                            owner.model.with(|m| {
                                m.menu.orientation = orientation;
                                m.menu.page_turn = page_turn;
                                m.list.state.list.selected = selected;
                                m.list.state.list.follow = 1;
                            });
                            unsafe {
                                owner
                                    .render(
                                        services(),
                                        &physical(),
                                        Toggles {
                                            night: toggles & 1 != 0,
                                            bluetooth: toggles & 2 != 0,
                                            frontlight: toggles & 4 != 0,
                                        },
                                    )
                                    .unwrap();
                            }
                        }
                    }
                }
            }
        }
    }
    RECORD.with(|r| {
        assert!(r.borrow().peak <= 128);
        println!(
            "Menu host command bound: {} (24,320 compositions)",
            r.borrow().peak
        );
    });
}
struct Book {
    reads: Vec<i32>,
    fail_storage: Option<i32>,
}
impl ChapterBook for Book {
    fn count(&self) -> Option<i32> {
        Some(1000)
    }
    fn label(&mut self, index: i32, text: &mut Text) -> bool {
        self.reads.push(index);
        if self.fail_storage == Some(index) {
            return false;
        }
        // Same default row used by the real book for a mid-window read failure.
        if index == 31 {
            text.assign_indented(0, b"").is_ok()
        } else {
            text.assign_indented(
                (index as usize % 3) * 2,
                format!("Chapter {index}").as_bytes(),
            )
            .is_ok()
        }
    }
}
#[test]
fn chapter_window_prewarm_and_failed_storage_publication() {
    tests::reset(0);
    RECORD.with(|r| *r.borrow_mut() = Record::default());
    let owner = Chapters::try_new(owner_tests::runtime(), 31).unwrap();
    let mut book = Book {
        reads: Vec::new(),
        fail_storage: Some(31),
    };
    assert!(unsafe { owner.enter(services(), &physical()) });
    assert!(unsafe { owner.render(services(), &physical(), &mut book) }.is_err());
    owner.model.with(|m| assert!(!m.ready && m.gate.failed()));
    RECORD.with(|r| {
        assert_eq!(r.borrow().clear, 1);
        assert_eq!(r.borrow().refresh, 0);
    });
    book.fail_storage = None;
    unsafe {
        owner.render(services(), &physical(), &mut book).unwrap();
    }
    let reads = book.reads.len();
    let prewarms = RECORD.with(|r| r.borrow().prewarms.len());
    unsafe {
        owner.render(services(), &physical(), &mut book).unwrap();
    }
    assert_eq!(book.reads.len(), reads);
    RECORD.with(|r| {
        assert_eq!(r.borrow().prewarms.len(), prewarms);
        assert!(r
            .borrow()
            .prewarms
            .iter()
            .any(|rows| rows.iter().any(Vec::is_empty)));
    });
    owner.model.with(|m| {
        m.state.list.selected = 987;
        m.state.list.follow = 1;
    });
    unsafe {
        owner.render(services(), &physical(), &mut book).unwrap();
    }
    assert!(book.reads.len() > reads);
    RECORD.with(|r| println!("Chapter exercised host command peak: {}", r.borrow().peak));
}
