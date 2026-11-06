extern crate alloc;

use super::{chapters, child_result::ReaderChildResult, commands::Commands, menu::Items};
use crate::app::{abi, resources::Text, stable_owner::StableOwner, sync::ModelCell};
use alloc::{alloc::alloc, boxed::Box};
use core::{
    alloc::Layout,
    cell::UnsafeCell,
    ffi::c_void,
    ptr::{self, NonNull},
};
use xp_ui::{
    model::ListState,
    reader::percent,
    reader_chrome::{self, hits, list, menu, toolbar},
    scene::Command,
    utility,
};

// Initialize fields directly so the largest child never occupies a task stack.
// The initializer must initialize every field without failing or unwinding.
unsafe fn allocate<T>(initialize: impl FnOnce(*mut T)) -> Option<Box<T>> {
    let owner = NonNull::new(alloc(Layout::new::<T>()).cast::<T>())?;
    initialize(owner.as_ptr());
    Some(Box::from_raw(owner.as_ptr()))
}

pub(crate) enum ReaderChild {
    Menu(StableOwner<Menu>),
    Chapters(StableOwner<Chapters>),
    Percent(StableOwner<Percent>),
    Qr(StableOwner<Qr>),
}

impl ReaderChild {
    pub(crate) fn target(&self) -> ChildTarget {
        match self {
            Self::Menu(owner) => ChildTarget::Menu(owner.pointer()),
            Self::Chapters(owner) => ChildTarget::Chapters(owner.pointer()),
            Self::Percent(owner) => ChildTarget::Percent(owner.pointer()),
            Self::Qr(owner) => ChildTarget::Qr(owner.pointer()),
        }
    }

    #[cfg(test)]
    pub(super) unsafe fn take_result(&self) -> Option<ReaderChildResult> {
        unsafe { self.target().take_result() }
    }
}

// Points into the stable boxes, never into the movable Application owner enum.
#[derive(Clone, Copy)]
pub(crate) enum ChildTarget {
    Menu(NonNull<Menu>),
    Chapters(NonNull<Chapters>),
    Percent(NonNull<Percent>),
    Qr(NonNull<Qr>),
}

impl ChildTarget {
    pub(crate) unsafe fn take_result(self) -> Option<ReaderChildResult> {
        let result = match self {
            Self::Menu(owner) => unsafe { &owner.as_ref().result },
            Self::Chapters(owner) => unsafe { &owner.as_ref().result },
            Self::Percent(owner) => unsafe { &owner.as_ref().result },
            Self::Qr(owner) => unsafe { &owner.as_ref().result },
        };
        unsafe { (*result.get()).take() }
    }
}

pub(super) struct ListPublication {
    pub(super) state: reader_chrome::ListModel,
    pub(super) hits: hits::Layout,
    pub(super) ready: bool,
    pub(super) failed: bool,
    pub(super) suppress_buttons: u32,
    pub(super) suppress_contact: bool,
}
impl ListPublication {
    fn new(selected: i32) -> Self {
        Self {
            state: reader_chrome::ListModel::new(selected),
            hits: hits::Layout::default(),
            ready: false,
            failed: false,
            suppress_buttons: 0,
            suppress_contact: false,
        }
    }
}

pub(super) struct ListFrame {
    pub(super) commands: Commands,
    pub(super) theme: list::Theme,
    pub(super) next: reader_chrome::ListModel,
    pub(super) next_hits: hits::Layout,
    pub(super) consumed_scroll: i32,
}
impl ListFrame {
    unsafe fn initialize(out: *mut Self, start: u16) {
        ptr::addr_of_mut!((*out).commands).write(Commands::new(start));
        ptr::addr_of_mut!((*out).theme).write(list::Theme::default());
        ptr::addr_of_mut!((*out).next).write(reader_chrome::ListModel::new(0));
        ptr::addr_of_mut!((*out).next_hits).write(hits::Layout::default());
        ptr::addr_of_mut!((*out).consumed_scroll).write(0);
    }
}

pub(super) struct MenuPublication {
    pub(super) list: ListPublication,
    pub(super) menu: menu::State,
}
pub(super) struct MenuFrame {
    pub(super) list: ListFrame,
    pub(super) menu: menu::State,
}
#[derive(Clone, Copy)]
#[repr(u8)]
pub(super) enum MenuOption {
    Orientation,
    AutoTurn,
}
pub(super) struct MenuPopup {
    pub(super) handle: Option<NonNull<c_void>>,
    pub(super) parent: *const c_void,
    pub(super) option: MenuOption,
}
pub(crate) struct Menu {
    pub(super) model: ModelCell<MenuPublication>,
    pub(super) frame: UnsafeCell<MenuFrame>,
    pub(super) items: Items,
    pub(super) title: Text,
    pub(super) progress: Text,
    pub(super) result: UnsafeCell<Option<ReaderChildResult>>,
    pub(super) popup: UnsafeCell<MenuPopup>,
}
impl Menu {
    pub(super) fn try_new(
        runtime: &'static abi::Runtime,
        items: Items,
        title: Text,
        progress: Text,
        orientation: u8,
    ) -> Option<Box<Self>> {
        unsafe {
            allocate(|out: *mut Self| {
                ptr::addr_of_mut!((*out).model).write(ModelCell::new(
                    MenuPublication {
                        list: ListPublication::new(0),
                        menu: menu::State::new(orientation),
                    },
                    runtime,
                ));
                let frame = ptr::addr_of_mut!((*out).frame).cast::<MenuFrame>();
                ListFrame::initialize(ptr::addr_of_mut!((*frame).list), 48);
                ptr::addr_of_mut!((*frame).menu).write(menu::State::new(orientation));
                ptr::addr_of_mut!((*out).items).write(items);
                ptr::addr_of_mut!((*out).title).write(title);
                ptr::addr_of_mut!((*out).progress).write(progress);
                ptr::addr_of_mut!((*out).result).write(UnsafeCell::new(None));
                ptr::addr_of_mut!((*out).popup).write(UnsafeCell::new(MenuPopup {
                    handle: None,
                    parent: core::ptr::null(),
                    option: MenuOption::Orientation,
                }));
            })
        }
    }
}

pub(super) struct ChapterFrame {
    pub(super) list: ListFrame,
    pub(super) window: chapters::Window,
}
pub(crate) struct Chapters {
    pub(super) model: ModelCell<ListPublication>,
    pub(super) frame: UnsafeCell<ChapterFrame>,
    pub(super) result: UnsafeCell<Option<ReaderChildResult>>,
}
impl Chapters {
    pub(super) fn try_new(runtime: &'static abi::Runtime, selected: i32) -> Option<Box<Self>> {
        unsafe {
            allocate(|out: *mut Self| {
                ptr::addr_of_mut!((*out).model)
                    .write(ModelCell::new(ListPublication::new(selected), runtime));
                let frame = ptr::addr_of_mut!((*out).frame).cast::<ChapterFrame>();
                ListFrame::initialize(ptr::addr_of_mut!((*frame).list), 32);
                chapters::Window::initialize(ptr::addr_of_mut!((*frame).window));
                ptr::addr_of_mut!((*out).result).write(UnsafeCell::new(None));
            })
        }
    }
}

pub(super) struct PercentPublication {
    pub(super) state: percent::State,
    pub(super) controls: Option<percent::Controls>,
    pub(super) edge_buttons: bool,
}
pub(super) struct Fixed<const N: usize> {
    pub(super) bytes: [u8; N],
    pub(super) len: usize,
}
pub(super) struct PercentLabels {
    pub(super) readout: Fixed<16>,
    pub(super) front: Fixed<64>,
    pub(super) side: Fixed<64>,
}
pub(super) struct PercentFrame {
    pub(super) commands: [Command; 18],
    pub(super) labels: PercentLabels,
    pub(super) controls: percent::Controls,
    pub(super) layout: utility::Layout,
}
pub(crate) struct Percent {
    pub(super) model: ModelCell<PercentPublication>,
    pub(super) frame: UnsafeCell<PercentFrame>,
    pub(super) result: UnsafeCell<Option<ReaderChildResult>>,
}
impl Percent {
    pub(super) fn input(&self, input: xp_ui::model::Input, edge_buttons: bool) -> percent::Update {
        self.model.with(|model| {
            model.state.update(
                input,
                model.controls.as_ref().map(percent::Controls::as_slice),
                edge_buttons,
            )
        })
    }

    pub(super) fn begin_render(&self) -> percent::State {
        self.model.with(|model| {
            model.state.render_begin();
            model.state
        })
    }

    // The completed controls publish without overwriting input received during paint.
    pub(super) fn publish(&self, controls: percent::Controls) {
        self.model.with(|model| model.controls = Some(controls));
    }

    pub(super) fn try_new(runtime: &'static abi::Runtime, value: i32) -> Option<Box<Self>> {
        unsafe {
            allocate(|out: *mut Self| {
                ptr::addr_of_mut!((*out).model).write(ModelCell::new(
                    PercentPublication {
                        state: percent::State::new(value),
                        controls: None,
                        edge_buttons: false,
                    },
                    runtime,
                ));
                let frame = ptr::addr_of_mut!((*out).frame).cast::<PercentFrame>();
                for index in 0..18 {
                    ptr::addr_of_mut!((*frame).commands)
                        .cast::<Command>()
                        .add(index)
                        .write(Command::default());
                }
                // The backing arrays and lengths are all valid when zeroed.
                ptr::addr_of_mut!((*frame).labels).write_bytes(0, 1);
                ptr::addr_of_mut!((*frame).controls).write(percent::Controls::default());
                ptr::addr_of_mut!((*frame).layout).write(utility::Layout::default());
                ptr::addr_of_mut!((*out).result).write(UnsafeCell::new(None));
            })
        }
    }
}

pub(super) struct QrFrame {
    pub(super) commands: Commands,
    pub(super) layout: utility::Layout,
}
pub(super) struct QrPublication {
    pub(super) state: utility::State,
    pub(super) failed: bool,
    pub(super) suppress_buttons: u32,
    pub(super) suppress_contact: bool,
}
pub(crate) struct Qr {
    pub(super) model: ModelCell<QrPublication>,
    pub(super) frame: UnsafeCell<QrFrame>,
    pub(super) payload: Text,
    pub(super) result: UnsafeCell<Option<ReaderChildResult>>,
}
impl Qr {
    pub(super) fn try_new(runtime: &'static abi::Runtime, payload: Text) -> Option<Box<Self>> {
        unsafe {
            allocate(|out: *mut Self| {
                ptr::addr_of_mut!((*out).model).write(ModelCell::new(
                    QrPublication {
                        state: utility::State {
                            kind: utility::Screen::Qr as u8,
                            ..Default::default()
                        },
                        failed: false,
                        suppress_buttons: 0,
                        suppress_contact: false,
                    },
                    runtime,
                ));
                let frame = ptr::addr_of_mut!((*out).frame).cast::<QrFrame>();
                ptr::addr_of_mut!((*frame).commands).write(Commands::fixed(4));
                ptr::addr_of_mut!((*frame).layout).write(utility::Layout::default());
                ptr::addr_of_mut!((*out).payload).write(payload);
                ptr::addr_of_mut!((*out).result).write(UnsafeCell::new(None));
            })
        }
    }
}

#[derive(Default)]
pub(super) struct ToolbarRow {
    pub(super) label: Text,
    pub(super) value: Text,
    pub(super) toggle: bool,
    pub(super) checked: bool,
}
pub(super) struct ToolbarPublication {
    pub(super) navigation: ListState,
    pub(super) contact: toolbar::Contact,
    pub(super) hits: hits::Layout,
    pub(super) ready: bool,
}
pub(super) struct ToolbarFrame {
    pub(super) commands: Commands,
    pub(super) theme: list::Theme,
    pub(super) rows: [ToolbarRow; 16],
    pub(super) first: u16,
    pub(super) count: u16,
    pub(super) page_indicator: [u8; 16],
    pub(super) next: ListState,
    pub(super) next_hits: hits::Layout,
}
pub(crate) struct Toolbar {
    pub(super) model: ModelCell<ToolbarPublication>,
    pub(super) frame: UnsafeCell<ToolbarFrame>,
}
impl Toolbar {
    pub(super) fn reset_navigation(&self, selected: i32, top: i32) {
        self.model.with(|model| {
            xp_ui::reader::input::toolbar_reset(&mut model.navigation);
            model.navigation.selected = selected;
            model.navigation.top = top;
        });
    }

    pub(super) fn visible_rows(&self) -> i32 {
        self.model
            .with(|model| xp_ui::reader::input::toolbar_page_rows(&model.navigation))
    }

    pub(super) fn scroll(&self, delta: i32, count: i32) -> bool {
        self.model
            .with(|model| xp_ui::reader::input::toolbar_scroll(&mut model.navigation, delta, count))
    }

    pub(super) fn publish(&self, painted: &ListState, hits: hits::Layout) {
        self.model.with(|model| {
            xp_ui::reader::input::toolbar_publish(&mut model.navigation, painted);
            model.hits = hits;
            model.ready = true;
        });
    }

    pub(super) fn try_new(runtime: &'static abi::Runtime) -> Option<Box<Self>> {
        unsafe {
            allocate(|out: *mut Self| {
                ptr::addr_of_mut!((*out).model).write(ModelCell::new(
                    ToolbarPublication {
                        navigation: ListState::new(0),
                        contact: toolbar::Contact::default(),
                        hits: hits::Layout::default(),
                        ready: false,
                    },
                    runtime,
                ));
                let frame = ptr::addr_of_mut!((*out).frame).cast::<ToolbarFrame>();
                ptr::addr_of_mut!((*frame).commands).write(Commands::new(24));
                ptr::addr_of_mut!((*frame).theme).write(list::Theme::default());
                for index in 0..16 {
                    ptr::addr_of_mut!((*frame).rows)
                        .cast::<ToolbarRow>()
                        .add(index)
                        .write(ToolbarRow::default());
                }
                ptr::addr_of_mut!((*frame).first).write(0);
                ptr::addr_of_mut!((*frame).count).write(0);
                ptr::addr_of_mut!((*frame).page_indicator).write([0; 16]);
                ptr::addr_of_mut!((*frame).next).write(ListState::new(0));
                ptr::addr_of_mut!((*frame).next_hits).write(hits::Layout::default());
            })
        }
    }
}
