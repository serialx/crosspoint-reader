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
    help: usize,
    bookmarks: usize,
    deletes: usize,
    footer_select: Vec<u8>,
    loading: usize,
    prewarms: Vec<Vec<Vec<u8>>>,
    refresh: usize,
    fail_paint: bool,
    invalid_theme: bool,
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
    RECORD.with(|r| if r.borrow().invalid_theme { -1 } else { 24 })
}
unsafe extern "C" fn text(_: *const xp_ui::scene::Command, _: Rect, value: abi::Text) {
    RECORD.with(|r| r.borrow_mut().texts.push(value.bytes().unwrap().to_vec()));
}
unsafe extern "C" fn footer(labels: *const abi::Text) {
    RECORD.with(|r| r.borrow_mut().footer_select = (*labels.add(1)).bytes().unwrap().to_vec());
}
unsafe extern "C" fn refresh(mode: u8) {
    assert_eq!(mode, xp_ui::scene::Refresh::Fast as u8);
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
unsafe extern "C" fn theme(
    out: *mut list::Theme,
    facts: *mut xp_ui::reader_chrome::runtime::ThemeFacts,
    toolbar: u8,
) {
    *facts = xp_ui::reader_chrome::runtime::ThemeFacts {
        body_line_height: 0,
        help_font: 0,
        bookmark_help_font: 0,
    };
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
fn invalid_theme_rejects_menu_before_and_after_publication() {
    tests::reset(0);
    RECORD.with(|r| *r.borrow_mut() = Record::default());
    let owner = menu();
    for (invalid, refreshes) in [(true, 0), (false, 1), (true, 1), (false, 2)] {
        RECORD.with(|r| r.borrow_mut().invalid_theme = invalid);
        let result = unsafe {
            owner.render(
                services(),
                &physical(),
                Toggles {
                    night: false,
                    bluetooth: false,
                    frontlight: false,
                },
            )
        };
        assert_eq!(result.is_err(), invalid);
        owner.model.with(|m| {
            assert_eq!(m.list.ready, !invalid);
            assert_eq!(m.list.gate.failed(), invalid);
        });
        RECORD.with(|r| assert_eq!(r.borrow().refresh, refreshes));
    }
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
fn invalid_theme_rejects_chapters_before_and_after_publication() {
    tests::reset(0);
    RECORD.with(|r| *r.borrow_mut() = Record::default());
    let owner = Chapters::try_new(owner_tests::runtime(), 0).unwrap();
    let mut book = Book {
        reads: Vec::new(),
        fail_storage: None,
    };
    assert!(unsafe { owner.enter(services(), &physical()) });
    for (invalid, refreshes) in [(true, 0), (false, 1), (true, 1), (false, 2)] {
        RECORD.with(|r| r.borrow_mut().invalid_theme = invalid);
        let result = unsafe { owner.render(services(), &physical(), &mut book) };
        assert_eq!(result.is_err(), invalid);
        owner.model.with(|m| {
            assert_eq!(m.ready, !invalid);
            assert_eq!(m.gate.failed(), invalid);
        });
        RECORD.with(|r| assert_eq!(r.borrow().refresh, refreshes));
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

unsafe extern "C" fn clipping_header(_: Rect, _: abi::Text, list: u8) {
    assert_eq!(list, 0);
}
unsafe extern "C" fn clipping_help(_: Rect, text: abi::Text) {
    assert_eq!(text.bytes().unwrap(), b"translated label");
    RECORD.with(|r| r.borrow_mut().help += 1);
}
unsafe extern "C" fn clipping_bookmark(kind: u8, bounds: Rect, clip: Rect, paint: u8) {
    assert_eq!(kind, 0);
    assert!(bounds.w > 0 && bounds.h > 0);
    assert!(clip.w > 0 && clip.h > 0);
    assert!(paint <= 1);
    RECORD.with(|r| r.borrow_mut().bookmarks += 1);
}
unsafe extern "C" fn clipping_loading(_: abi::Text) -> Rect {
    RECORD.with(|r| r.borrow_mut().loading += 1);
    Rect::default()
}
fn clipping_services() -> &'static abi::Resources {
    static SERVICES: OnceLock<abi::Resources> = OnceLock::new();
    SERVICES.get_or_init(|| abi::Resources {
        popup_draw: clipping_loading,
        ..*services()
    })
}
fn clipping_details() -> crate::clipping_view::Details {
    crate::clipping_view::Details {
        help: clipping_help,
        bookmark: clipping_bookmark,
    }
}
#[test]
fn clipping_list_draws_help_bookmarks_and_loading_once_and_reuses_previews() {
    tests::reset(0);
    RECORD.with(|r| *r.borrow_mut() = Record::default());
    let owner = crate::owned::Clippings::try_new(owner_tests::runtime()).unwrap();
    let physical = Physical {
        header: clipping_header,
        ..physical()
    };
    let details = clipping_details();
    let mut reads = Vec::new();
    unsafe {
        owner
            .render(
                clipping_services(),
                &physical,
                &details,
                8,
                &mut |index, row| {
                    reads.push(index);
                    if index != 0 {
                        row.chapter[..2].copy_from_slice(b"K\0");
                    }
                    row.preview[..8].copy_from_slice(b"preview\0");
                    row.preview_length = 7;
                },
            )
            .unwrap();
    }
    assert_eq!(reads, (0..8).collect::<Vec<_>>());
    owner.model.with(|m| assert!(m.ready && !m.gate.failed()));
    RECORD.with(|r| {
        let r = r.borrow();
        assert_eq!(r.loading, 1);
        assert_eq!(r.help, 1);
        assert!(r.bookmarks > 0);
        assert!(r.texts.iter().any(|text| text == b"preview"));
        assert_eq!(r.clear, 0);
        assert!(r.prewarms.is_empty());
    });
    unsafe {
        owner
            .render(clipping_services(), &physical, &details, 8, &mut |_, _| {
                panic!("cached row")
            })
            .unwrap();
    }
    RECORD.with(|r| assert_eq!(r.borrow().loading, 1));
    RECORD.with(|r| r.borrow_mut().fail_paint = true);
    assert!(unsafe {
        owner.render(clipping_services(), &physical, &details, 8, &mut |_, _| {
            panic!("cached row")
        })
    }
    .is_err());
    owner.model.with(|m| assert!(!m.ready && m.gate.failed()));
}
#[test]
fn clipping_list_empty_and_command_oom_have_no_preview_reads() {
    tests::reset(1);
    RECORD.with(|r| *r.borrow_mut() = Record::default());
    let owner = crate::owned::Clippings::try_new(owner_tests::runtime()).unwrap();
    let physical = Physical {
        header: clipping_header,
        ..physical()
    };
    assert!(unsafe {
        owner.render(
            clipping_services(),
            &physical,
            &clipping_details(),
            0,
            &mut |_, _| panic!("empty"),
        )
    }
    .is_err());
    owner.model.with(|m| assert!(m.gate.failed()));
    unsafe {
        owner
            .render(
                clipping_services(),
                &physical,
                &clipping_details(),
                0,
                &mut |_, _| panic!("empty"),
            )
            .unwrap();
    }
    owner.model.with(|m| assert!(m.ready));
    RECORD.with(|r| {
        let r = r.borrow();
        assert_eq!(r.loading, 0);
        assert_eq!(r.bookmarks, 0);
        assert_eq!(r.help, 0);
    });
}

unsafe extern "C" fn bookmark_icon(kind: u8, _: Rect, _: Rect, _: u8) {
    RECORD.with(|r| match kind {
        0 => r.borrow_mut().bookmarks += 1,
        1 => r.borrow_mut().deletes += 1,
        _ => panic!("icon kind"),
    });
}
struct BookmarkToc {
    calls: usize,
    fail: bool,
}
impl crate::bookmark_window::Toc for BookmarkToc {
    fn subtitle(&mut self, _: u16, prefix: &[u8], out: &mut Text) -> bool {
        self.calls += 1;
        !self.fail && out.assign_parts(&[prefix, b"TOC title"]).is_ok()
    }
}
#[test]
fn bookmarks_borrow_final_labels_and_publish_only_complete_scenes() {
    use crate::{bookmark_owned::Bookmarks, bookmark_view::Details, bookmarks::decode};
    tests::reset(0);
    RECORD.with(|r| *r.borrow_mut() = Record::default());
    let owner = Bookmarks::try_new(owner_tests::runtime()).unwrap();
    let mut rows = decode::<Text>(br#"{"bookmarks":[{"summary":"summary","percentage":0.5},{"name":"renamed","summary":"hidden","pc":10,"pp":2}]}"#).unwrap();
    let details = Details {
        help: clipping_help,
        icon: bookmark_icon,
    };
    let mut toc = BookmarkToc {
        calls: 0,
        fail: false,
    };
    assert!(unsafe { owner.enter(services()) });
    unsafe {
        owner
            .render(services(), &physical(), &details, &rows, &mut toc)
            .unwrap()
    };
    owner.model.with(|m| assert!(m.ready));
    RECORD.with(|r| {
        let r = r.borrow();
        assert!(r.texts.iter().any(|t| t == b"summary"));
        assert!(r.texts.iter().any(|t| t == b"renamed"));
        assert!(r.texts.iter().any(|t| t == b"50% - TOC title"));
        assert!(!r.texts.iter().any(|t| t == b"hidden"));
        assert_eq!((r.help, r.bookmarks), (1, 2));
        assert!(!r.footer_select.is_empty());
    });
    let reads = toc.calls;
    owner.model.with(|m| m.state.revealed = 1);
    RECORD.with(|r| r.borrow_mut().touch = 1);
    unsafe {
        owner
            .render(services(), &physical(), &details, &rows, &mut toc)
            .unwrap()
    };
    assert_eq!(toc.calls, reads);
    RECORD.with(|r| assert_eq!(r.borrow().deletes, 1));
    toc.fail = true;
    unsafe { owner.invalidate() };
    assert!(unsafe { owner.render(services(), &physical(), &details, &rows, &mut toc) }.is_err());
    owner.model.with(|m| assert!(!m.ready && m.gate.failed()));
    rows.clear();
    unsafe { owner.invalidate() };
    unsafe {
        owner
            .render(services(), &physical(), &details, &rows, &mut toc)
            .unwrap()
    };
    RECORD.with(|r| assert!(r.borrow().footer_select.is_empty()));
    unsafe { owner.exit(|_| panic!("no popup")) };
    owner.model.with(|m| assert!(!m.ready));
    assert!(unsafe { (*owner.frame.get()).list.commands.as_mut_slice() }.is_empty());
}
