use super::{
    chapter_book::GuardedBook,
    chapter_view::ChapterBook,
    child_result::{ChapterSelection, ReaderChildResult},
    menu_view::Toggles,
    owned::ChildTarget,
    physical::Physical,
};
use crate::app::{
    epub::{abi, session_book, EpubSession},
    resources::Text,
};
use core::ffi::c_void;
use xp_ui::{
    model::{EffectKind, Input},
    reader::percent::Outcome,
    reader_chrome::list::Screen,
};

impl ChildTarget {
    fn activity_log(self, parent: &EpubSession, entering: bool) {
        if unsafe { (parent.leaves.log_enabled)(0) } == 0 {
            return;
        }
        let message: &[u8] = match (self, entering) {
            (Self::Menu(_), true) => b"Entering activity: EpubReaderMenu",
            (Self::Menu(_), false) => b"Exiting activity: EpubReaderMenu",
            (Self::Chapters(_), true) => b"Entering activity: EpubReaderChapterSelection",
            (Self::Chapters(_), false) => b"Exiting activity: EpubReaderChapterSelection",
            (Self::Percent(_), true) => b"Entering activity: EpubReaderPercentSelection",
            (Self::Percent(_), false) => b"Exiting activity: EpubReaderPercentSelection",
            (Self::Qr(_), true) => b"Entering activity: QrDisplay",
            (Self::Qr(_), false) => b"Exiting activity: QrDisplay",
        };
        unsafe {
            (parent.leaves.log)(
                0,
                crate::app::abi::Text::from_bytes(b"ACT"),
                crate::app::abi::Text::from_bytes(message),
            )
        };
    }

    pub(in crate::app) unsafe fn facts(self) -> crate::app::abi::LegacyFacts {
        crate::app::abi::LegacyFacts {
            kind: 255,
            remote_input: u8::from(match self {
                Self::Menu(owner) => unsafe { owner.as_ref() }
                    .model
                    .with(|model| model.list.ready),
                _ => false,
            }),
            reader: 0,
            prevent_sleep: 0,
            exclusive_storage: 0,
            skip_delay: 0,
            input_over_page: 0,
        }
    }

    /// Entry runs after publication; acquire the parent guard before touching scratch.
    pub(in crate::app) unsafe fn enter(self, parent: &EpubSession, physical: &Physical) -> bool {
        self.activity_log(parent, true);
        let _access = parent.lock_resources();
        let ready = match self {
            Self::Menu(owner) => unsafe { &mut *owner.as_ref().frame.get() }
                .list
                .commands
                .recover(parent.resources),
            Self::Chapters(owner) => unsafe { owner.as_ref().enter(parent.resources, physical) },
            Self::Percent(owner) => {
                let owner = unsafe { owner.as_ref() };
                let frame = unsafe { &mut *owner.frame.get() };
                let edge = unsafe { (physical.utility_layout)(&mut frame.layout) } != 0;
                owner.model.with(|model| model.edge_buttons = edge);
                true
            }
            Self::Qr(owner) => unsafe { &mut *owner.as_ref().frame.get() }
                .commands
                .recover(parent.resources),
        };
        if !ready {
            unsafe { self.cancel() };
        }
        parent.request_update();
        ready
    }

    /// App holds the render lifetime lock through exit and destruction.
    pub(in crate::app) unsafe fn exit(self, parent: &EpubSession) {
        let _access = unsafe { parent.borrowed_resources() };
        match self {
            Self::Menu(owner) => {
                let owner = unsafe { owner.as_ref() };
                owner.model.with(|model| model.list.ready = false);
                unsafe { &mut *owner.frame.get() }.list.commands.clear();
                if let Some(popup) = unsafe { &mut *owner.popup.get() }.handle.take() {
                    unsafe {
                        (parent.leaves.popup_close)(popup.as_ptr());
                    }
                }
            }
            Self::Chapters(owner) => {
                let owner = unsafe { owner.as_ref() };
                owner.model.with(|model| model.ready = false);
                unsafe { &mut *owner.frame.get() }.list.commands.clear();
            }
            Self::Percent(owner) => unsafe { owner.as_ref() }
                .model
                .with(|model| model.controls = None),
            Self::Qr(owner) => unsafe { &mut *owner.as_ref().frame.get() }.commands.clear(),
        }
        self.activity_log(parent, false);
    }

    /// App's native render token protects the child and retained parent.
    #[inline(never)]
    pub(in crate::app) unsafe fn render(self, parent: &EpubSession, physical: &Physical) -> bool {
        let access = unsafe { parent.borrowed_resources() };
        let painted = match self {
            Self::Menu(owner) => {
                let owner = unsafe { owner.as_ref() };
                if let Some(handle) = unsafe { (*owner.popup.get()).handle } {
                    if unsafe { (parent.leaves.popup_active)(handle.as_ptr()) } != 0 {
                        unsafe {
                            (parent.leaves.popup_process_render)(handle.as_ptr());
                        }
                        return true;
                    }
                }
                let settings = parent.settings(false);
                unsafe {
                    owner.render(
                        parent.resources,
                        physical,
                        Toggles {
                            night: settings.screen_inverted != 0,
                            bluetooth: settings.ble_enabled != 0,
                            frontlight: (parent.runtime.frontlight_state)() != 0,
                        },
                    )
                }
                .is_ok()
            }
            Self::Chapters(owner) => unsafe {
                owner
                    .as_ref()
                    .render(parent.resources, physical, &mut GuardedBook(&access))
            }
            .is_ok(),
            Self::Percent(owner) => unsafe {
                owner
                    .as_ref()
                    .render(parent.resources, parent.leaves.log, |layout| {
                        (physical.utility_layout)(layout);
                    })
            }
            .is_ok(),
            Self::Qr(owner) => unsafe {
                owner.as_ref().render(parent.resources, physical, |layout| {
                    (physical.utility_layout)(layout);
                })
            }
            .is_ok(),
        };
        painted
    }

    pub(in crate::app) unsafe fn cancel(self) {
        match self {
            Self::Menu(owner) => {
                let owner = unsafe { owner.as_ref() };
                let state = owner.model.with(|model| model.menu);
                unsafe {
                    *owner.result.get() = Some(ReaderChildResult::Menu {
                        action: None,
                        orientation: state.orientation,
                        page_turn: state.page_turn,
                    });
                }
            }
            Self::Chapters(owner) => unsafe {
                *owner.as_ref().result.get() = Some(ReaderChildResult::Chapter(None));
            },
            Self::Percent(owner) => unsafe {
                *owner.as_ref().result.get() = Some(ReaderChildResult::Percent(None));
            },
            Self::Qr(owner) => unsafe {
                *owner.as_ref().result.get() = Some(ReaderChildResult::QrClosed);
            },
        }
    }

    pub(in crate::app) unsafe fn input(
        self,
        parent: &EpubSession,
        input: Input,
        home_gesture: bool,
    ) -> bool {
        match self {
            Self::Menu(owner) => unsafe { owner.as_ref().input(parent, input, home_gesture) },
            Self::Chapters(owner) => {
                let owner = unsafe { owner.as_ref() };
                // Poll short holds through release instead of waiting for a display
                // refresh and turning the stale held state into a page repeat.
                let due = owner.model.with(|model| {
                    model.failed
                        || model.suppress_buttons != 0
                        || model.suppress_contact
                        || model
                            .state
                            .interest(Screen::Chapters, input.now_ms)
                            .due(&input)
                });
                if !due {
                    return false;
                }
                let total = {
                    let access = parent.lock_resources();
                    GuardedBook(&access).count()
                };
                let count = u16::try_from(total.unwrap_or(0)).unwrap_or(0);
                let update = owner
                    .model
                    .with(|model| model.input(input, count, total.is_some(), Screen::Chapters));
                if update.retry || update.effect.invalidate != 0 {
                    parent.request_update();
                }
                if update.haptic {
                    unsafe {
                        (parent.runtime.touch_feedback)(0);
                    }
                }
                match update.effect.kind {
                    value if value == EffectKind::Cancel as u8 => {
                        unsafe {
                            self.cancel();
                        }
                        true
                    }
                    value if value == EffectKind::Activate as u8 => {
                        let index = update.effect.value;
                        if index < 0 || index >= i32::from(count) {
                            return false;
                        }
                        owner.model.with(|model| {
                            model.state.list.select(index);
                        });
                        let mut result = None;
                        let access = parent.lock_resources();
                        if let Some(book) = parent.book(&access) {
                            unsafe {
                                session_book::toc(
                                    book.as_ptr(),
                                    index,
                                    chapter_selected,
                                    (&mut result as *mut Option<ChapterSelection>).cast(),
                                );
                            }
                        }
                        unsafe {
                            *owner.result.get() = Some(ReaderChildResult::Chapter(result));
                        }
                        true
                    }
                    _ => false,
                }
            }
            Self::Percent(owner) => {
                let owner = unsafe { owner.as_ref() };
                let edge = owner.model.with(|model| model.edge_buttons);
                let update = owner.input(input, edge);
                if update.feedback {
                    unsafe {
                        (parent.runtime.touch_feedback)(0);
                    }
                }
                match update.outcome {
                    Outcome::None => false,
                    Outcome::Redraw => {
                        parent.request_update();
                        false
                    }
                    Outcome::Cancel => {
                        unsafe {
                            self.cancel();
                        }
                        true
                    }
                    Outcome::Confirm(value) => {
                        unsafe {
                            *owner.result.get() =
                                Some(ReaderChildResult::Percent(Some(value as u8)));
                        }
                        true
                    }
                }
            }
            Self::Qr(owner) => {
                let update = unsafe { owner.as_ref() }.input(input);
                if update.retry {
                    parent.request_update();
                }
                if update.effect.kind == EffectKind::Cancel as u8 {
                    unsafe {
                        self.cancel();
                    }
                    true
                } else {
                    false
                }
            }
        }
    }
}

unsafe extern "C" fn chapter_selected(context: *mut c_void, toc: *const abi::XpEpubToc) -> u8 {
    let toc = unsafe { &*toc };
    if toc.spine == -1 {
        return 1;
    }
    let Some(bytes) = (unsafe { toc.anchor.bytes() }) else {
        return 0;
    };
    let Ok(anchor) = Text::copy(bytes) else {
        return 0;
    };
    unsafe {
        *context.cast::<Option<ChapterSelection>>() = Some(ChapterSelection {
            spine: toc.spine,
            anchor,
        });
    }
    1
}
