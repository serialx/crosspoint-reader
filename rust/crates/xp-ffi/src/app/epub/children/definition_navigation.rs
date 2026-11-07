use super::{
    definition_owned::{DefinitionSession, HistoryEntry},
    definition_physical::{SelectionPosition, SelectionRequest},
    owned::Fixed,
};
use crate::app::{
    abi,
    epub::abi::{XpEpubHeap, XpEpubLeaves},
    RenderGuard,
};
use core::{ffi::c_void, fmt::Write, mem, ptr};
use xp_core::i18n::ids::*;

impl DefinitionSession {
    pub(super) fn lock(runtime: &'static abi::Runtime) -> RenderGuard {
        let token = unsafe { (runtime.render_lock)() };
        assert!(!token.is_null());
        RenderGuard { token, runtime }
    }
    pub(super) fn message(&self, id: u16, runtime: &abi::Runtime) {
        let now = unsafe { (runtime.now_ms)() };
        self.model.with(|model| {
            model.state.message = id;
            model.state.message_time = now;
            model.state.message_visible = 1;
        });
        crate::app::request_update();
    }
    pub(super) unsafe fn enter(&self, runtime: &abi::Runtime, leaves: &XpEpubLeaves) {
        unsafe { Self::activity_log(leaves, true) };
        if let Some(view) = unsafe { self.prepare(0, false) } {
            self.publish_payload(view, 0);
        } else {
            self.model.with(|model| model.state.failed = 1);
            self.message(STR_DICT_LOW_MEMORY, runtime);
        }
        crate::app::request_update();
    }
    pub(super) unsafe fn exit(&self, leaves: &XpEpubLeaves) {
        unsafe { Self::activity_log(leaves, false) };
        unsafe { (leaves.fonts_release_caches)() };
    }
    pub(super) unsafe fn selection_failed(&self, runtime: &abi::Runtime) {
        unsafe { (self.physical.selection_release)(self.payload.get()) };
        self.message(STR_DICT_LOW_MEMORY, runtime);
    }
    /// Caller excludes rendering while creating the Page loan. App owns the returned child.
    #[inline(never)]
    pub(super) unsafe fn selection(
        &self,
        runtime: &'static abi::Runtime,
        leaves: &XpEpubLeaves,
        x: i32,
        y: i32,
    ) -> *mut c_void {
        let _guard = Self::lock(runtime);
        let (rejected, page, styled) = self
            .model
            .with(|model| (model.gate.failed(), model.state.page, model.styled));
        if rejected {
            crate::app::request_update();
            return ptr::null_mut();
        }
        let nav = unsafe { &mut *self.navigation.get() };
        if nav.selection.valid == 0 || x >= 0 {
            nav.selection_page = page;
            nav.selection.valid = 0;
        }
        if !styled
            && unsafe { (self.physical.selection_build)(self.payload.get(), nav.selection_page) }
                == 0
        {
            unsafe { Self::log(leaves, 1, b"Cannot allocate plain definition selection") };
            unsafe { self.selection_failed(runtime) };
            return ptr::null_mut();
        }
        let request = SelectionRequest {
            page: nav.selection_page,
            selection: nav.selection,
            x,
            y,
        };
        let child =
            unsafe { (self.physical.selection_child)(self.payload.get(), &request, self.body()) };
        if child.is_null() {
            unsafe { self.selection_failed(runtime) };
        }
        child
    }
    /// Called after child destruction, parent publication and RenderLock release.
    /// The native bridge retains its ActivityResult until this synchronous call returns.
    #[inline(never)]
    pub(super) unsafe fn selection_result(
        &self,
        runtime: &abi::Runtime,
        leaves: &XpEpubLeaves,
        result: &abi::XpAppResultView,
    ) {
        unsafe { (self.physical.selection_release)(self.payload.get()) };
        if result.cancelled != 0 {
            return;
        }
        assert_eq!(result.kind, abi::RESULT_CLIPPING);
        let selected = unsafe { result.value.clipping };
        let nav = unsafe { &mut *self.navigation.get() };
        nav.selection = SelectionPosition {
            start_page: selected.start_page_offset,
            end_page: selected.end_page_offset,
            start_word: selected.start_word,
            end_word: selected.end_word,
            valid: 1,
            reserved: [0; 3],
        };
        let full = self.model.with(|model| {
            model.state.page = nav.selection_page + i32::from(selected.end_page_offset);
            model.state.history_depth == 16
        });
        if full {
            self.message(STR_DICT_HISTORY_FULL, runtime);
            return;
        }
        let mut heap = XpEpubHeap {
            free_bytes: 0,
            largest_block: 0,
        };
        unsafe { (self.physical.heap)(&mut heap) };
        let text = selected.text.bytes;
        // Native admission checks largest first, after releasing temporary selection Pages.
        if (heap.largest_block as usize) < text.len + 1
            || (heap.free_bytes as usize) < text.len + 8192
        {
            unsafe { self.selection_low_memory(runtime, leaves, text.len) };
            return;
        }
        let bytes = if text.len == 0 {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(text.data, text.len) }
        };
        if !nav.pending.assign(bytes) {
            unsafe { self.selection_low_memory(runtime, leaves, text.len) };
        }
    }
    #[inline(never)]
    unsafe fn selection_low_memory(
        &self,
        runtime: &abi::Runtime,
        leaves: &XpEpubLeaves,
        length: usize,
    ) {
        let mut message = Fixed::<80>::new();
        let _ = write!(
            message,
            "Low heap for lookup text ({} bytes)",
            length.wrapping_add(1)
        );
        unsafe { (leaves.log)(1, abi::Text::from_bytes(b"DICT"), message.wire()) };
        self.message(STR_DICT_LOW_MEMORY, runtime);
    }
    unsafe fn activity_log(leaves: &XpEpubLeaves, entering: bool) {
        if unsafe { (leaves.log_enabled)(0) } != 0 {
            let message: &[u8] = if entering {
                b"Entering activity: DictionaryDefinition"
            } else {
                b"Exiting activity: DictionaryDefinition"
            };
            unsafe {
                (leaves.log)(
                    0,
                    abi::Text::from_bytes(b"ACT"),
                    abi::Text::from_bytes(message),
                )
            };
        }
    }
    unsafe fn log(leaves: &XpEpubLeaves, level: u8, bytes: &[u8]) {
        unsafe {
            (leaves.log)(
                level,
                abi::Text::from_bytes(b"DICT"),
                abi::Text::from_bytes(bytes),
            )
        };
    }
    #[inline(never)]
    unsafe fn log_heap(leaves: &XpEpubLeaves) {
        let mut heap = XpEpubHeap {
            free_bytes: 0,
            largest_block: 0,
        };
        unsafe { (leaves.heap_snapshot)(&mut heap) };
        let mut message = Fixed::<96>::new();
        let _ = write!(
            message,
            "Navigation after release: free={} largest={}",
            heap.free_bytes, heap.largest_block
        );
        unsafe { (leaves.log)(0, abi::Text::from_bytes(b"DICT"), message.wire()) };
    }
    unsafe fn find(&self, slot: u8, word: abi::Text) -> Result<(), u8> {
        let mut result = 0;
        if unsafe {
            (self.physical.payload_find)(self.payload.get(), slot, self.lookup, word, &mut result)
        } == 0
        {
            Err(result)
        } else {
            Ok(())
        }
    }
    unsafe fn recover(
        &self,
        back: bool,
        styled: bool,
        error: u16,
        runtime: &abi::Runtime,
        leaves: &XpEpubLeaves,
    ) {
        let nav = unsafe { &mut *self.navigation.get() };
        nav.pending.clear();
        unsafe {
            (self.physical.payload_release)(self.payload.get(), 1, 0);
            (leaves.fonts_release_caches)();
        }
        if back {
            unsafe {
                Self::log(
                    leaves,
                    1,
                    b"Cannot restore previous definition; leaving lookup",
                );
                *self.result.get() = Some(false);
            }
            return;
        }
        let page = self.model.with(|model| model.state.page);
        let restored = unsafe { self.find(0, nav.query.view()) }
            .ok()
            .and_then(|()| unsafe { self.prepare(0, !styled) });
        if let Some(view) =
            restored.filter(|view| (view.styled != 0) == styled && page < view.pages)
        {
            self.publish_payload(view, page);
            self.message(error, runtime);
        } else {
            unsafe {
                Self::log(
                    leaves,
                    1,
                    b"Cannot recover current definition after failed lookup",
                );
                *self.result.get() = Some(false);
            }
        }
    }
    fn cancel_pending_if_rejected(&self, back: bool) -> bool {
        if !back && self.model.with(|model| model.gate.failed()) {
            unsafe {
                (*self.navigation.get()).pending.clear();
                crate::app::request_update();
            }
            true
        } else {
            false
        }
    }
    #[inline(never)]
    pub(super) unsafe fn navigate(
        &self,
        back: bool,
        runtime: &'static abi::Runtime,
        leaves: &XpEpubLeaves,
    ) {
        {
            let _guard = Self::lock(runtime);
            if self.cancel_pending_if_rejected(back) {
                return;
            }
            self.message(STR_DICT_LOOKING_UP, runtime);
        }
        unsafe { (leaves.child_selection_physical.wait_paint)() };
        let _guard = Self::lock(runtime);
        if self.cancel_pending_if_rejected(back) {
            return;
        }
        let (state, styled) = self.model.with(|model| {
            model.state.message_visible = 0;
            (model.state, model.styled)
        });
        unsafe {
            (self.physical.payload_release)(self.payload.get(), 0, 1);
            (leaves.fonts_release_caches)();
            Self::log_heap(leaves);
        }
        let nav = unsafe { &mut *self.navigation.get() };
        let depth = usize::from(state.history_depth);
        let word = if back {
            nav.history[depth - 1].query.view()
        } else {
            nav.pending.view()
        };
        if let Err(result) = unsafe { self.find(1, word) } {
            let error = match result {
                1 => STR_DICT_NOT_FOUND,
                2 => STR_DICT_LOW_MEMORY,
                3 => STR_DICT_DECOMPRESS_ERROR,
                _ => STR_DICT_READ_FAILED,
            };
            unsafe { self.recover(back, styled, error, runtime, leaves) };
            return;
        }
        let plain = back && !nav.history[depth - 1].styled;
        let Some(view) = (unsafe { self.prepare(1, plain) }) else {
            unsafe { self.recover(back, styled, STR_DICT_LOW_MEMORY, runtime, leaves) };
            return;
        };
        let page;
        if back {
            let previous = &mut nav.history[depth - 1];
            if previous.styled != (view.styled != 0) || previous.page >= view.pages {
                unsafe {
                    Self::log(leaves, 1, b"Cannot restore definition layout");
                    self.recover(back, styled, STR_DICT_RESTORE_FAILED, runtime, leaves);
                }
                return;
            }
            nav.query = mem::take(&mut previous.query);
            page = previous.page;
            nav.selection_page = previous.selection_page;
            nav.selection = previous.selection;
        } else {
            nav.history[depth] = HistoryEntry {
                query: mem::take(&mut nav.query),
                page: state.page,
                selection_page: nav.selection_page,
                selection: nav.selection,
                styled,
            };
            nav.query = mem::take(&mut nav.pending);
            nav.selection_page = 0;
            nav.selection = SelectionPosition::default();
            page = 0;
        }
        nav.pending.clear();
        unsafe { (self.physical.payload_commit)(self.payload.get()) };
        self.publish_payload(view, page);
        self.model.with(|model| {
            model.state.history_depth = if back {
                state.history_depth - 1
            } else {
                state.history_depth + 1
            }
        });
        crate::app::request_update();
    }
}
