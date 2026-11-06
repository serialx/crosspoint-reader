use super::{
    clipping_view::Details,
    owned::{Clippings, Fixed},
    physical::Physical,
};
use crate::app::{abi, epub::EpubSession};
use core::fmt::Write;

pub(super) fn log_index(parent: &EpubSession, prefix: &str, index: i32) {
    let mut message = Fixed::<64>::new();
    let _ = write!(message, "{prefix}{index}");
    unsafe { (parent.leaves.log)(1, abi::Text::from_bytes(b"CLIP"), message.wire()) };
}
impl Clippings {
    /// The child's entry owns the parent resource guard.
    pub(super) unsafe fn enter_clippings(&self, parent: &EpubSession) -> bool {
        let frame = unsafe { &mut *self.frame.get() };
        frame.initial = true;
        frame.window.invalidate();
        frame.list.commands.recover(parent.resources)
    }

    /// Application retains the render lifetime lock through exit and destruction.
    pub(super) unsafe fn exit_clippings(&self, parent: &EpubSession) {
        self.model.with(|model| model.ready = false);
        unsafe { &mut *self.frame.get() }.list.commands.clear();
        if let Some(handle) = unsafe { &mut *self.popup.get() }.handle.take() {
            unsafe { (parent.leaves.popup_close)(handle.as_ptr()) };
        }
    }

    /// Application holds the render lifetime lock; busy storage retains the last publication.
    #[inline(never)]
    pub(super) unsafe fn render_clippings(
        &self,
        parent: &EpubSession,
        physical: &Physical,
    ) -> bool {
        if let Some(handle) = unsafe { (*self.popup.get()).handle } {
            if unsafe { (parent.leaves.popup_active)(handle.as_ptr()) } != 0 {
                unsafe { (parent.leaves.popup_process_render)(handle.as_ptr()) };
                return true;
            }
        }
        let Some(app) = crate::app::app() else {
            return false;
        };
        let Some(operation) = app.clippings.operation() else {
            parent.request_update();
            return true;
        };
        operation.read(|store| unsafe {
            self.render(
                parent.resources,
                physical,
                &Details {
                    help: parent.leaves.child_list_help,
                    bookmark: parent.leaves.child_list_bookmark_icon,
                },
                store.count() as u16,
                &mut |index, output| {
                    if let Some(row) = store.row(index as usize) {
                        output.chapter.copy_from_slice(&row.chapter);
                    }
                    if !store.preview(
                        index as usize,
                        &mut output.preview,
                        &mut output.preview_length,
                    ) {
                        log_index(parent, "Failed to read clipping ", index);
                    }
                },
            )
            .is_ok()
        })
    }
}
