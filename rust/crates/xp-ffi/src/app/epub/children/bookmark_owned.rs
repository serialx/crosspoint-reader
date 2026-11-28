use super::{
    bookmark_window::Window,
    owned::{allocate, ListFrame, ListPublication},
};
use crate::app::{abi, bookmarks::Record, resources::Text, sync::ModelCell};
use core::{
    cell::UnsafeCell,
    ffi::c_void,
    ptr::{self, NonNull},
};
extern crate alloc;
use alloc::boxed::Box;

pub(crate) struct Position {
    pub(crate) xpath: Text,
    pub(crate) percentage: f32,
    pub(crate) spine: i32,
    pub(crate) page: i32,
    pub(crate) total_pages: i32,
    pub(crate) visible_offset: Option<u32>,
}
pub(super) struct Frame {
    pub(super) list: ListFrame,
    pub(super) window: Window,
}
#[derive(Clone, Copy)]
pub(super) enum OptionKind {
    Actions,
    Delete,
}
pub(super) struct Popup {
    pub(super) handle: Option<NonNull<c_void>>,
    pub(super) parent: *const c_void,
    pub(super) option: OptionKind,
    pub(super) choice: Option<i32>,
    pub(super) confirming_delete: bool,
    pub(super) rename_index: Option<usize>,
}
pub(crate) struct Bookmarks {
    pub(super) model: ModelCell<ListPublication>,
    pub(super) frame: UnsafeCell<Frame>,
    pub(super) popup: UnsafeCell<Popup>,
    result: UnsafeCell<Option<Option<Position>>>,
}
impl Bookmarks {
    pub(super) fn try_new(runtime: &'static abi::Runtime) -> Option<Box<Self>> {
        unsafe {
            allocate(|out: *mut Self| {
                ModelCell::initialize(ptr::addr_of_mut!((*out).model), runtime, |model| {
                    ListPublication::initialize(model, 0);
                });
                let frame = ptr::addr_of_mut!((*out).frame).cast::<Frame>();
                ListFrame::initialize(ptr::addr_of_mut!((*frame).list), 40);
                Window::initialize(ptr::addr_of_mut!((*frame).window));
                ptr::addr_of_mut!((*out).popup).write(UnsafeCell::new(Popup {
                    handle: None,
                    parent: ptr::null(),
                    option: OptionKind::Actions,
                    choice: None,
                    confirming_delete: false,
                    rename_index: None,
                }));
                ptr::addr_of_mut!((*out).result).write(UnsafeCell::new(None));
            })
        }
    }
    pub(super) fn selected(&self) -> Option<usize> {
        self.model
            .with(|model| usize::try_from(model.state.list.selected).ok())
    }
    pub(super) fn select(&self, index: i32, count: usize) {
        if index >= 0 && (index as usize) < count {
            self.model.with(|model| model.state.list.select(index));
        }
    }
    // Records are lent by the retained reader under its resource guard.
    #[inline(never)]
    pub(super) fn selection<N>(&self, records: &[Record<N>], spine_count: i32) -> Option<Position> {
        let row = records.get(self.selected()?)?;
        let valid =
            row.page_count > 0 && row.page < row.page_count && i32::from(row.spine) < spine_count;
        Some(Position {
            xpath: Text::copy(row.xpath.bytes()).ok()?,
            percentage: row.percentage,
            spine: i32::from(row.spine),
            page: if valid { i32::from(row.page) } else { 0 },
            total_pages: if valid { i32::from(row.page_count) } else { 0 },
            visible_offset: row.visible_offset,
        })
    }
    /// The loop owns the result; Application extracts it while excluding render.
    pub(super) unsafe fn finish(&self, result: Option<Position>) {
        unsafe { *self.result.get() = Some(result) };
    }
    pub(super) unsafe fn finished(&self) -> bool {
        unsafe { (*self.result.get()).is_some() }
    }
    /// Called once before child destruction under the render lifetime lock.
    pub(super) unsafe fn take_result(&self) -> Option<Option<Position>> {
        unsafe { (*self.result.get()).take() }
    }
    /// The resource guard excludes render while records and their rows change.
    pub(super) unsafe fn invalidate(&self) {
        unsafe { (*self.frame.get()).window.invalidate() };
    }
    /// Deletion stays committed in memory even if its file write fails.
    pub(super) unsafe fn after_delete(&self, count: usize) {
        unsafe { self.invalidate() };
        if count == 0 {
            unsafe { self.finish(None) };
        } else {
            self.model.with(|model| {
                let selected = model.state.list.selected.min(count as i32 - 1);
                model.state.list.select(selected);
            });
        }
    }
}

#[cfg(target_pointer_width = "32")]
const _: () = {
    assert!(core::mem::size_of::<Bookmarks>() == 936);
    assert!(core::mem::size_of::<Position>() == 36);
};
