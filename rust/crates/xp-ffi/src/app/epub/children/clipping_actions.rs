use super::{
    child_result::{ClippingPosition, ReaderChildResult},
    clipping_activity::log_index,
    owned::{ClippingOption, Clippings, Fixed},
};
use crate::app::{abi, clippings_facade::NativeStore, epub::EpubSession};
use core::{ffi::c_void, fmt::Write, mem::MaybeUninit, ptr::NonNull};
use xp_core::i18n::ids;
use xp_ui::{
    model::{EffectKind, Input, CONFIRM},
    reader::input::ButtonQuery,
    reader_chrome::{list::Screen, ACTIONS},
};

unsafe extern "C" fn label(
    context: *mut c_void,
    index: u32,
    sink: abi::TextSink,
    sink_context: *mut c_void,
) -> u8 {
    let owner = unsafe { &*context.cast::<Clippings>() };
    let popup = unsafe { &*owner.popup.get() };
    let parent = unsafe { &*popup.parent.cast::<EpubSession>() };
    let keys = match popup.option {
        ClippingOption::Open => [ids::STR_OPEN, ids::STR_GO_TO_PASSAGE],
        ClippingOption::Delete => [ids::STR_CANCEL, ids::STR_DELETE],
    };
    keys.get(index as usize).map_or(0, |key| unsafe {
        sink(sink_context, (parent.resources.translate)(*key))
    })
}
unsafe extern "C" fn choose(context: *mut c_void, index: i32) {
    // Consume the choice after the native popup releases its synchronous callback.
    unsafe { (*(*context.cast::<Clippings>()).popup.get()).choice = Some(index) };
}
struct DetailRead<'a> {
    store: &'a NativeStore,
    index: usize,
    read_ok: bool,
    fill_attempted: bool,
}
unsafe extern "C" fn fill_detail(context: *mut c_void, output: *mut u8, length: u32) -> u8 {
    let source = unsafe { &mut *context.cast::<DetailRead<'_>>() };
    source.fill_attempted = true;
    let bytes = if length == 0 {
        &mut []
    } else {
        unsafe { core::slice::from_raw_parts_mut(output, length as usize) }
    };
    source.read_ok = source.store.read_into(source.index, bytes) == Some(length as usize);
    u8::from(source.read_ok)
}
impl Clippings {
    fn selected(&self) -> Option<usize> {
        self.model
            .with(|model| usize::try_from(model.state.list.selected).ok())
    }
    unsafe fn show_clipping_options(&self, parent: &EpubSession, option: ClippingOption) {
        let Some(app) = crate::app::app() else { return };
        let Some(index) = self.selected() else { return };
        let Some(anchored) = app
            .clippings
            .read(|store| {
                store
                    .row(index)
                    .map(|row| row.start_offset != u32::MAX && row.end_offset != u32::MAX)
            })
            .flatten()
        else {
            return;
        };
        let _access = parent.lock_resources();
        let popup = unsafe { &mut *self.popup.get() };
        if let Some(handle) = popup.handle {
            if unsafe { (parent.leaves.popup_active)(handle.as_ptr()) } != 0 {
                return;
            }
        } else {
            popup.handle = NonNull::new(unsafe { (parent.leaves.popup_open)() });
        }
        let Some(handle) = popup.handle else {
            parent.request_update();
            return;
        };
        popup.parent = (parent as *const EpubSession).cast();
        popup.option = option;
        popup.choice = None;
        let (title, count) = match option {
            ClippingOption::Open => (ids::STR_CLIPPINGS, if anchored { 2 } else { 1 }),
            ClippingOption::Delete => {
                popup.confirming_delete = true;
                (ids::STR_DELETE_CLIPPING_CONFIRM, 2)
            }
        };
        // End the mutable popup borrow before synchronous label callbacks.
        unsafe {
            (parent.leaves.popup_show)(
                handle.as_ptr(),
                (parent.resources.translate)(title),
                abi::Text::from_terminated(b"\0"),
                count,
                0,
                label,
                (self as *const Self).cast_mut().cast(),
                choose,
                (self as *const Self).cast_mut().cast(),
            );
        }
        parent.request_update();
    }

    #[inline(never)]
    unsafe fn delete_clipping(&self, parent: &EpubSession) {
        let Some(index) = self.selected() else { return };
        let Some(app) = crate::app::app() else { return };
        let Some(mut operation) = app.clippings.operation() else {
            parent.request_update();
            return;
        };
        if !operation.read(|store| store.queue_remove(index)) {
            log_index(parent, "Failed to delete clipping ", index as i32);
            return;
        }
        let pending = {
            let _access = parent.lock_resources();
            operation.update(|store| store.prepare_remove(index))
        };
        let written =
            pending.and_then(|pending| operation.read(|store| store.persist(pending, None)));
        let Ok(written) = written else {
            log_index(parent, "Failed to delete clipping ", index as i32);
            return;
        };
        let _access = parent.lock_resources();
        let count = operation.update(|store| {
            store.publish_clean(written);
            store.count()
        });
        unsafe { &mut *self.frame.get() }.window.invalidate();
        if count == 0 {
            unsafe { *self.result.get() = Some(ReaderChildResult::Clippings(None)) };
        } else {
            self.model.with(|model| {
                let selected = model.state.list.selected.min(count as i32 - 1);
                model.state.list.select(selected);
            });
        }
    }

    #[inline(never)]
    unsafe fn open_clipping_detail(&self, parent: &EpubSession) {
        let Some(index) = self.selected() else { return };
        let Some(app) = crate::app::app() else { return };
        let Some(operation) = app.clippings.operation() else {
            parent.request_update();
            return;
        };
        operation.read(|store| {
            let row = store.row(index)?;
            let title = if row.chapter[0] == 0 {
                unsafe { (parent.resources.translate)(ids::STR_CLIPPINGS) }
            } else {
                abi::Text::from_terminated(
                    &row.chapter[..=row.chapter.iter().position(|b| *b == 0)?],
                )
            };
            let mut source = DetailRead {
                store,
                index,
                read_ok: false,
                fill_attempted: false,
            };
            let launched = unsafe {
                parent.launch_plain_definition(
                    title,
                    u32::from(row.text_length),
                    fill_detail,
                    (&mut source as *mut DetailRead<'_>).cast(),
                )
            };
            if !launched && (!source.fill_attempted || source.read_ok) {
                unsafe {
                    (parent.leaves.log)(
                        1,
                        abi::Text::from_bytes(b"CLIP"),
                        abi::Text::from_bytes(b"Failed to allocate clipping detail activity"),
                    )
                };
            }
            Some(())
        });
    }

    unsafe fn choose_clipping(&self, parent: &EpubSession, choice: i32) {
        match unsafe { (*self.popup.get()).option } {
            ClippingOption::Delete => {
                unsafe { (*self.popup.get()).confirming_delete = false };
                if choice == 1 {
                    unsafe { self.delete_clipping(parent) };
                }
                parent.request_update();
            }
            ClippingOption::Open if choice == 0 => unsafe { self.open_clipping_detail(parent) },
            ClippingOption::Open => {
                let Some(index) = self.selected() else { return };
                let Some(app) = crate::app::app() else { return };
                if let Some(position) = app
                    .clippings
                    .read(|store| {
                        store.row(index).map(|row| ClippingPosition {
                            spine: i32::from(row.spine),
                            page: i32::from(row.start_page),
                            offset: row.start_offset,
                        })
                    })
                    .flatten()
                {
                    unsafe {
                        *self.result.get() = Some(ReaderChildResult::Clippings(Some(position)))
                    };
                }
            }
        }
    }

    #[inline(never)]
    pub(super) unsafe fn input_clippings(
        &self,
        parent: &EpubSession,
        input: Input,
        home: bool,
    ) -> bool {
        if home {
            unsafe { *self.result.get() = Some(ReaderChildResult::Clippings(None)) };
            return true;
        }
        if let Some(handle) = unsafe { (*self.popup.get()).handle } {
            let mut response = MaybeUninit::uninit();
            unsafe { (parent.leaves.popup_input)(handle.as_ptr(), response.as_mut_ptr()) };
            let response = unsafe { response.assume_init() };
            let choice = unsafe { (*self.popup.get()).choice.take() };
            if let Some(choice) = choice {
                unsafe { self.choose_clipping(parent, choice) };
            }
            if response.changed != 0 {
                parent.request_update();
            }
            if response.consumed != 0 {
                return unsafe { (*self.result.get()).is_some() };
            }
        }
        if unsafe { (*self.popup.get()).confirming_delete } {
            unsafe { (*self.popup.get()).confirming_delete = false };
            parent.request_update();
            return false;
        }
        let Some(app) = crate::app::app() else {
            return false;
        };
        let Some(count) = app.clippings.read(|store| store.count() as u16) else {
            parent.request_update();
            return false;
        };
        let suppressed = self.model.with(|model| model.gate.needs_input());
        let mut long_press = false;
        let mut long_touch = None;
        if !suppressed {
            let services = unsafe { &*(parent.leaves.input_services)() };
            long_press = services.button.is_some_and(|button| unsafe {
                button(services.context, CONFIRM, ButtonQuery::Long as u8, 700) != 0
            });
            if self.model.with(|model| model.ready) {
                let (mut x, mut y) = (0, 0);
                if unsafe { (parent.leaves.input_screen_long)(&mut x, &mut y) } != 0 {
                    long_touch = i16::try_from(x).ok().zip(i16::try_from(y).ok());
                }
            }
        }
        let due = suppressed
            || long_press
            || long_touch.is_some()
            || self.model.with(|model| {
                model
                    .state
                    .interest(Screen::Clippings, input.now_ms)
                    .due(&input)
            });
        if !due {
            return false;
        }
        let update = self
            .model
            .with(|model| model.clipping_input(input, count, long_press, long_touch));
        if update.retry || update.effect.invalidate != 0 {
            parent.request_update();
        }
        if update.haptic || update.long_haptic {
            unsafe { (parent.runtime.touch_feedback)(u8::from(update.long_haptic)) };
        }
        match update.effect.kind {
            value if value == EffectKind::Cancel as u8 => {
                unsafe { *self.result.get() = Some(ReaderChildResult::Clippings(None)) };
                true
            }
            value if value == EffectKind::Activate as u8 || value == ACTIONS => {
                let index = update.effect.value;
                if index >= 0 && index < i32::from(count) {
                    self.model.with(|model| model.state.list.select(index));
                    unsafe {
                        self.show_clipping_options(
                            parent,
                            if value == ACTIONS {
                                ClippingOption::Delete
                            } else {
                                ClippingOption::Open
                            },
                        )
                    };
                }
                false
            }
            value if value == EffectKind::Scroll as u8 => {
                if unsafe { (parent.leaves.log_enabled)(0) } != 0 {
                    let mut message = Fixed::<80>::new();
                    let _ = write!(
                        message,
                        "EpubReaderClippings swipe delta={} count={count}",
                        update.effect.value
                    );
                    unsafe {
                        (parent.leaves.log)(0, abi::Text::from_bytes(b"LIST"), message.wire())
                    };
                }
                false
            }
            _ => false,
        }
    }
}
