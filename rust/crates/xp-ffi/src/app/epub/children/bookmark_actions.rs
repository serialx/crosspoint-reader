use super::{
    bookmark_activity::error,
    bookmark_owned::{Bookmarks, OptionKind},
    owned::{ChildTarget, Fixed},
};
use crate::app::{
    abi,
    bookmarks::mutations,
    epub::{bookmark_storage::Name, session_book, EpubSession},
    keyboard,
};
use core::{ffi::c_void, fmt::Write, mem::MaybeUninit, ptr::NonNull};
use xp_core::i18n::ids;
use xp_ui::{
    model::{EffectKind, Input, CONFIRM, SWIPE_LEFT},
    reader::input::ButtonQuery,
    reader_chrome::{list::Screen, ACTIONS, DELETE},
};

unsafe extern "C" fn label(
    context: *mut c_void,
    index: u32,
    sink: abi::TextSink,
    sink_context: *mut c_void,
) -> u8 {
    let owner = unsafe { &*context.cast::<Bookmarks>() };
    let popup = unsafe { &*owner.popup.get() };
    let parent = unsafe { &*popup.parent.cast::<EpubSession>() };
    let keys: &[u16] = match popup.option {
        OptionKind::Actions => &[ids::STR_OPEN, ids::STR_RENAME, ids::STR_DELETE],
        OptionKind::Delete => &[ids::STR_CANCEL, ids::STR_DELETE],
    };
    keys.get(index as usize).map_or(0, |key| unsafe {
        sink(sink_context, (parent.resources.translate)(*key))
    })
}
unsafe extern "C" fn choose(context: *mut c_void, index: i32) {
    unsafe { (*(*context.cast::<Bookmarks>()).popup.get()).choice = Some(index) };
}
impl Bookmarks {
    #[inline(never)]
    unsafe fn show_options(&self, parent: &EpubSession, option: OptionKind) {
        let access = parent.lock_resources();
        if parent.with_bookmarks(&access, |rows| rows.is_empty()) {
            return;
        }
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
            OptionKind::Actions => (ids::STR_BOOKMARKS, 3),
            OptionKind::Delete => {
                popup.confirming_delete = true;
                (ids::STR_CONFIRM_DELETE_BOOKMARK, 2)
            }
        };
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
    unsafe fn open_selected(&self, parent: &EpubSession) {
        let access = parent.lock_resources();
        let Some(book) = parent.book(&access) else {
            return;
        };
        let count = unsafe { session_book::spine_count(book.as_ptr()) };
        let position = parent.with_bookmarks(&access, |rows| self.selection(rows, count));
        if let Some(position) = position {
            unsafe { self.finish(Some(position)) };
        }
    }

    #[inline(never)]
    unsafe fn delete_selected(&self, parent: &EpubSession) {
        let Some(index) = self.selected() else { return };
        let access = parent.lock_resources();
        let count = parent.mutate_bookmarks(&access, |rows| {
            mutations::remove(rows, index)?;
            Some(rows.len())
        });
        let Some(count) = count else { return };
        unsafe { self.invalidate() };
        if !parent.write_bookmarks(&access) {
            error(parent, b"Failed to save bookmarks after delete");
        }
        unsafe { self.after_delete(count) };
        if count != 0 {
            if let Some(app) = crate::app::app() {
                app.request(true);
            } else {
                parent.request_update();
            }
        }
    }

    #[inline(never)]
    unsafe fn start_rename(&self, parent: &EpubSession) {
        let Some(index) = self.selected() else { return };
        let access = parent.lock_resources();
        let launched = parent.with_bookmarks(&access, |rows| {
            let row = rows.get(index)?;
            Some(keyboard::launch(&keyboard::Launch {
                title: unsafe { (parent.resources.translate)(ids::STR_RENAME) },
                initial: abi::Text::from_terminated(row.name.terminated()),
                maximum: 128,
                input_type: 0,
            }))
        });
        match launched {
            Some(true) => unsafe { (*self.popup.get()).rename_index = Some(index) },
            Some(false) => error(parent, b"OOM: bookmark rename keyboard"),
            None => {}
        }
    }

    #[inline(never)]
    unsafe fn renamed(&self, parent: &EpubSession, result: Option<keyboard::Result>) {
        let Some(index) = (unsafe { (*self.popup.get()).rename_index.take() }) else {
            return;
        };
        let Some(name) = result.and_then(Name::from_result) else {
            return;
        };
        let access = parent.lock_resources();
        let old =
            parent.mutate_bookmarks(&access, |rows| mutations::replace_name(rows, index, name));
        let Ok(old) = old else { return };
        unsafe { self.invalidate() };
        if !parent.write_bookmarks(&access) {
            error(parent, b"Failed to save bookmarks after rename");
            let _ =
                parent.mutate_bookmarks(&access, |rows| mutations::replace_name(rows, index, old));
            unsafe { self.invalidate() };
        }
        parent.request_update();
    }

    #[inline(never)]
    unsafe fn choose_option(&self, parent: &EpubSession, choice: i32) {
        match unsafe { (*self.popup.get()).option } {
            OptionKind::Actions => match choice {
                0 => unsafe { self.open_selected(parent) },
                1 => unsafe { self.start_rename(parent) },
                2 => unsafe { self.show_options(parent, OptionKind::Delete) },
                _ => {}
            },
            OptionKind::Delete => {
                unsafe { (*self.popup.get()).confirming_delete = false };
                if choice == 1 {
                    unsafe { self.delete_selected(parent) };
                }
                parent.request_update();
            }
        }
    }

    #[inline(never)]
    pub(super) unsafe fn input_bookmarks(
        &self,
        parent: &EpubSession,
        input: Input,
        home: bool,
    ) -> bool {
        if home {
            unsafe { self.finish(None) };
            return true;
        }
        if let Some(handle) = unsafe { (*self.popup.get()).handle } {
            let mut response = MaybeUninit::uninit();
            unsafe { (parent.leaves.popup_input)(handle.as_ptr(), response.as_mut_ptr()) };
            let response = unsafe { response.assume_init() };
            let choice = unsafe { (*self.popup.get()).choice.take() };
            if let Some(choice) = choice {
                unsafe { self.choose_option(parent, choice) };
            }
            if response.changed != 0 {
                parent.request_update();
            }
            if response.consumed != 0 {
                return unsafe { self.finished() };
            }
        }
        if unsafe { (*self.popup.get()).confirming_delete } {
            unsafe { (*self.popup.get()).confirming_delete = false };
            parent.request_update();
            return false;
        }
        let count = parent.loop_bookmark_count();
        let Ok(count) = u16::try_from(count) else {
            return false;
        };
        let suppressed = self.model.with(|model| model.gate.needs_input());
        let mut long_press = false;
        let (mut long_touch, mut swipe_touch) = (None, None);
        if !suppressed {
            let services = unsafe { &*(parent.leaves.input_services)() };
            long_press = services.button.is_some_and(|button| unsafe {
                button(services.context, CONFIRM, ButtonQuery::Long as u8, 700) != 0
            });
            if self.model.with(|model| model.ready) {
                let (mut x, mut y) = (0, 0);
                if input.swipe == SWIPE_LEFT
                    && unsafe { (parent.leaves.input_screen_point)(1, &mut x, &mut y) } != 0
                {
                    swipe_touch = i16::try_from(x).ok().zip(i16::try_from(y).ok());
                }
                if unsafe { (parent.leaves.input_screen_point)(0, &mut x, &mut y) } != 0 {
                    long_touch = i16::try_from(x).ok().zip(i16::try_from(y).ok());
                    // The native list shares these coordinates; long touch wins when both are present.
                    if input.swipe == SWIPE_LEFT {
                        swipe_touch = long_touch;
                    }
                }
            }
        }
        let due = suppressed
            || long_press
            || long_touch.is_some()
            || self.model.with(|model| {
                model
                    .state
                    .interest(Screen::Bookmarks, input.now_ms)
                    .due(&input)
            });
        if !due {
            return false;
        }
        let update = self
            .model
            .with(|model| model.bookmark_input(input, count, long_press, long_touch, swipe_touch));
        if update.retry || update.effect.invalidate != 0 {
            parent.request_update();
        }
        if update.haptic || update.long_haptic {
            unsafe { (parent.runtime.touch_feedback)(u8::from(update.long_haptic)) };
        }
        match update.effect.kind {
            value if value == EffectKind::Cancel as u8 => unsafe { self.finish(None) },
            value if value == EffectKind::Activate as u8 || value == ACTIONS || value == DELETE => {
                let index = update.effect.value;
                if index >= 0 && index < i32::from(count) {
                    self.select(index, usize::from(count));
                    match value {
                        ACTIONS => unsafe { self.show_options(parent, OptionKind::Actions) },
                        DELETE => unsafe { self.show_options(parent, OptionKind::Delete) },
                        _ => unsafe { self.open_selected(parent) },
                    }
                }
            }
            value
                if value == EffectKind::Scroll as u8
                    && unsafe { (parent.leaves.log_enabled)(0) } != 0 =>
            {
                let mut message = Fixed::<80>::new();
                let _ = write!(
                    message,
                    "EpubReaderBookmarks swipe delta={} count={count}",
                    update.effect.value
                );
                unsafe { (parent.leaves.log)(0, abi::Text::from_bytes(b"LIST"), message.wire()) };
            }
            _ => {}
        }
        unsafe { self.finished() }
    }
}

impl ChildTarget {
    #[inline(never)]
    pub(in crate::app) unsafe fn keyboard_returned(
        self,
        parent: &EpubSession,
        result: Option<keyboard::Result>,
    ) {
        if let Self::Bookmarks(owner) = self {
            unsafe { owner.as_ref().renamed(parent, result) };
        }
    }
}
