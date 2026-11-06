use super::{
    child_result::ReaderChildResult,
    owned::allocate,
    selection_physical::{LookupStorage, Physical},
};
use crate::app::{
    abi,
    epub::page_owner::Page,
    resources::{Buffer, Text},
    stable_owner::StableOwner,
    sync::ModelCell,
};
use core::{
    cell::UnsafeCell,
    ptr::{self, NonNull},
};
use xp_ui::{
    dictionary::popup,
    reader_chrome::FootnoteState,
    scene::{Command, Rect},
};

#[repr(C)]
pub(super) struct FootnotePublication {
    pub(super) state: FootnoteState,
    pub(super) ready: bool,
    pub(super) gate: xp_ui::runtime::SceneGate,
}
#[repr(C)]
pub(super) struct FootnoteFrame {
    pub(super) commands: [Command; 2],
    pub(super) snapshot: Option<Buffer>,
    pub(super) snapshot_rect: Rect,
    pub(super) snapshot_index: i32,
    pub(super) rendering_selected: i32,
}
#[repr(C)]
pub(crate) struct Footnote {
    pub(super) model: ModelCell<FootnotePublication>,
    pub(super) frame: UnsafeCell<FootnoteFrame>,
    pub(super) page: Page,
    pub(super) margins: [i32; 2],
    pub(super) links: [[u16; 2]; 16],
    pub(super) count: u16,
    pub(super) result: UnsafeCell<Option<ReaderChildResult>>,
}

pub(super) fn terminated_bytes(bytes: &[u8]) -> &[u8] {
    &bytes[..bytes.iter().position(|b| *b == 0).unwrap_or(bytes.len())]
}

impl Footnote {
    pub(super) fn try_new(
        runtime: &'static abi::Runtime,
        page: Page,
        margins: [i32; 2],
    ) -> Option<StableOwner<Self>> {
        // The index map borrows metadata from the one Page; no href or text copy.
        unsafe {
            allocate(|out: *mut Self| {
                ptr::addr_of_mut!((*out).model).write(ModelCell::new(
                    FootnotePublication {
                        state: FootnoteState::default(),
                        ready: false,
                        gate: xp_ui::runtime::SceneGate::default(),
                    },
                    runtime,
                ));
                ptr::addr_of_mut!((*out).frame).write(UnsafeCell::new(FootnoteFrame {
                    commands: [Command::default(); 2],
                    snapshot: None,
                    snapshot_rect: Rect::default(),
                    snapshot_index: -1,
                    rendering_selected: 0,
                }));
                ptr::addr_of_mut!((*out).margins).write(margins);
                ptr::addr_of_mut!((*out).links).write([[0; 2]; 16]);
                let view = page.view();
                let mut count = 0;
                for i in 0..view.footnote_count() {
                    let (_, href) = view.footnote(i).expect("validated footnote");
                    for j in 0..view.link_count() {
                        let link = view.link(j).expect("validated link");
                        if terminated_bytes(link.href) == terminated_bytes(href) {
                            (*out).links[count] = [j, i];
                            count += 1;
                            break;
                        }
                    }
                }
                ptr::addr_of_mut!((*out).count).write(count as u16);
                ptr::addr_of_mut!((*out).page).write(page);
                ptr::addr_of_mut!((*out).result).write(UnsafeCell::new(None));
            })
            .map(StableOwner::from)
        }
    }

    pub(super) fn href(&self, selected: usize) -> Option<&[u8]> {
        if selected >= usize::from(self.count) {
            return None;
        }
        let (_, href) = self.page.view().footnote(self.links[selected][1]).ok()?;
        Some(terminated_bytes(href))
    }

    pub(super) fn selection(&self, selected: usize) -> Option<Text> {
        Text::copy(self.href(selected)?).ok()
    }
}

#[repr(C)]
pub(super) struct LookupPublication {
    pub(super) state: popup::Lookup,
    pub(super) failed: bool,
    pub(super) ready: bool,
    pub(super) open_attempted: bool,
    pub(super) open_ok: bool,
    pub(super) needs_index: bool,
    pub(super) awaiting_detail: bool,
}
#[repr(C)]
pub(super) struct LookupFrame {
    pub(super) commands: [Command; 3],
    pub(super) popup: popup::Layout,
    pub(super) prewarm: [usize; 2],
}
#[repr(C)]
pub(crate) struct LookupData {
    pub(super) model: ModelCell<LookupPublication>,
    pub(super) frame: UnsafeCell<LookupFrame>,
    pub(super) page: UnsafeCell<Option<Page>>,
    pub(super) margins: [i32; 2],
    pub(super) font: i32,
    pub(super) native: UnsafeCell<LookupStorage>,
    destroy: UnsafeCell<Option<unsafe extern "C" fn(*mut LookupStorage)>>,
    pub(super) result: UnsafeCell<Option<ReaderChildResult>>,
}
pub(crate) struct Lookup(StableOwner<LookupData>);
impl core::ops::Deref for Lookup {
    type Target = LookupData;
    fn deref(&self) -> &LookupData {
        &self.0
    }
}
impl Lookup {
    pub(super) fn pointer(&self) -> NonNull<LookupData> {
        self.0.pointer()
    }

    pub(super) fn try_new(
        runtime: &'static abi::Runtime,
        page: Page,
        margins: [i32; 2],
        font: i32,
        physical: &Physical,
        query: abi::Text,
    ) -> Option<Self> {
        let owner = unsafe {
            allocate(|out: *mut LookupData| {
                ptr::addr_of_mut!((*out).model).write(ModelCell::new(
                    LookupPublication {
                        state: popup::Lookup::new(),
                        failed: false,
                        ready: false,
                        open_attempted: false,
                        open_ok: false,
                        needs_index: false,
                        awaiting_detail: false,
                    },
                    runtime,
                ));
                ptr::addr_of_mut!((*out).frame).write(UnsafeCell::new(LookupFrame {
                    commands: [Command::default(); 3],
                    popup: popup::Layout::default(),
                    prewarm: [0; 2],
                }));
                ptr::addr_of_mut!((*out).page).write(UnsafeCell::new(Some(page)));
                ptr::addr_of_mut!((*out).margins).write(margins);
                ptr::addr_of_mut!((*out).font).write(font);
                ptr::addr_of_mut!((*out).native).write_bytes(0, 1);
                ptr::addr_of_mut!((*out).destroy).write(UnsafeCell::new(None));
                ptr::addr_of_mut!((*out).result).write(UnsafeCell::new(None));
            })
            .map(StableOwner::from)
            .map(Self)?
        };
        // Publish native interior pointers only after Box has become StableOwner.
        unsafe {
            (physical.lookup_init)(owner.native.get(), query);
            *owner.destroy.get() = Some(physical.lookup_drop);
        }
        Some(owner)
    }
}
impl LookupData {
    /// Caller holds the render lifetime lock. No paint loan may span this call.
    pub(super) unsafe fn release_page(&self) {
        unsafe { drop((*self.page.get()).take()) };
    }
}
impl Drop for Lookup {
    fn drop(&mut self) {
        // Native strings may point inside this allocation; destroy before Box retags it.
        if let Some(destroy) = unsafe { (*self.destroy.get()).take() } {
            unsafe { destroy(self.native.get()) };
        }
    }
}
#[cfg(target_pointer_width = "32")]
const _: () = {
    assert!(core::mem::size_of::<Footnote>() == 212);
    assert!(core::mem::size_of::<LookupData>() == 552);
};
