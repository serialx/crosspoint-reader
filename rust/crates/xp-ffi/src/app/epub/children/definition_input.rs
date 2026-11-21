use super::definition_owned::DefinitionSession;
use crate::app::{abi, epub::abi::XpEpubLeaves, RenderGuard};
use core::{ffi::c_void, ptr};
use xp_ui::{
    dictionary::definition::{self, Layout},
    model::{Effect, Input, BACK},
};

impl DefinitionSession {
    // Only the header hit and width are needed once the layout callback returns.
    // Keep its scratch off the definition navigation stack.
    #[inline(never)]
    unsafe fn input_header(&self, x: i32, y: i32) -> (bool, i32) {
        let mut layout = Layout::default();
        let hit = unsafe { (self.physical.layout)(&mut layout, x, y) } != 0;
        (hit, i32::from(layout.screen.w))
    }

    unsafe fn action(
        &self,
        effect: Effect,
        fonts: abi::FontOwner,
        runtime: &'static abi::Runtime,
        leaves: &XpEpubLeaves,
        x: i32,
        y: i32,
    ) -> *mut c_void {
        match effect.kind {
            definition::FINISH | definition::CANCEL => unsafe {
                *self.result.get() = Some(effect.kind == definition::CANCEL)
            },
            definition::BACK => unsafe { self.navigate(true, runtime, leaves, fonts) },
            definition::SELECT => return unsafe { self.selection(runtime, leaves, -1, -1) },
            definition::TOUCH_SELECT => return unsafe { self.selection(runtime, leaves, x, y) },
            _ => {}
        }
        ptr::null_mut()
    }
    /// Loop-task entry. Returned native children are adopted only after this borrow ends.
    #[inline(never)]
    pub(in crate::app) unsafe fn input(
        &self,
        runtime: &'static abi::Runtime,
        leaves: &XpEpubLeaves,
        touch_controls: bool,
        fonts: abi::FontOwner,
    ) -> *mut c_void {
        let (discard, failed, message_time) = self.model.with(|model| {
            (
                core::mem::take(&mut model.discard_pending),
                model.state.failed != 0,
                model.state.message_time,
            )
        });
        if discard {
            unsafe { (*self.navigation.get()).pending.clear() };
        }
        if failed && unsafe { (runtime.now_ms)() }.wrapping_sub(message_time) >= 1500 {
            unsafe { *self.result.get() = Some(false) };
            return ptr::null_mut();
        }
        if self.model.with(|model| model.gate.needs_input()) {
            let mut input = Input::default();
            unsafe { leaves.input_snapshot(&mut input) };
            let (admission, state) = self
                .model
                .with(|model| (model.gate.input(&input), model.state));
            if admission.retry {
                crate::app::request_update();
                if !failed && input.released & BACK != 0 {
                    let kind = if state.lookup != 0 && input.held_ms >= 1000 {
                        definition::CANCEL
                    } else if state.lookup != 0 && state.history_depth > 0 {
                        definition::BACK
                    } else {
                        definition::FINISH
                    };
                    unsafe {
                        self.action(
                            Effect {
                                kind,
                                ..Effect::default()
                            },
                            fonts,
                            runtime,
                            leaves,
                            0,
                            0,
                        )
                    };
                }
            }
            if admission.consume {
                return ptr::null_mut();
            }
        }
        let lookup = !self.lookup.is_null();
        if lookup && !failed && unsafe { !(*self.navigation.get()).pending.is_empty() } {
            unsafe { self.navigate(false, runtime, leaves, fonts) };
            return ptr::null_mut();
        }
        let (mut x, mut y) = (0, 0);
        let long_press =
            !failed && lookup && unsafe { (leaves.input_screen_point)(0, &mut x, &mut y) } != 0;
        // Native samples the recorded header target immediately after consuming the long press.
        let (header_back_hit, width) = unsafe {
            self.input_header(
                if long_press { x } else { -1 },
                if long_press { y } else { -1 },
            )
        };
        let header_long = long_press && header_back_hit;
        let mut input = Input::default();
        unsafe { leaves.input_snapshot(&mut input) };
        let (mut tx, mut ty) = (0, 0);
        let tap = !failed && unsafe { (leaves.input_screen_tap)(&mut tx, &mut ty) } != 0;
        input.touch_x = if tap { tx } else { x } as i16;
        input.touch_y = if tap { ty } else { y } as i16;
        let (mut effect, dismiss) = self.model.with(|model| {
            let visible = model.state.message_visible;
            let effect =
                model
                    .state
                    .update(input, tap, long_press, header_long, width, touch_controls);
            let dismiss = visible != 0 && model.state.message_visible == 0;
            // Dismissal is published only after the current render loan has ended.
            if dismiss {
                model.state.message_visible = visible;
            }
            (effect, dismiss)
        });
        if effect.value != 0 {
            unsafe { (*self.navigation.get()).selection.valid = 0 };
        }
        if dismiss {
            let token = unsafe { (runtime.render_try_lock)() };
            if token.is_null() {
                effect.invalidate = u8::from(effect.value != 0);
            } else {
                let _guard = RenderGuard { token, runtime };
                self.model.with(|model| model.state.message_visible = 0);
            }
        }
        if effect.invalidate != 0 {
            crate::app::request_update();
        }
        if tap && effect.value != 0 {
            unsafe { (runtime.touch_feedback)(0) };
        }
        unsafe { self.action(effect, fonts, runtime, leaves, x, y) }
    }
}
