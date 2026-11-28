//! Visit ownership for File Transfer; physical services retain native drawing.
extern crate alloc;
use crate::{
    model::{self, Effect, EffectKind, Input, ListState},
    runtime::{Publication, SceneGate},
    scene::{Command, Refresh, Scene},
    settings::{
        hits, settings_ffi,
        view::{Layout, Resources},
    },
};
use alloc::boxed::Box;
use core::{alloc::Layout as Allocation, cell::UnsafeCell, ptr};

/// # Safety
/// Model locks serialize input/publication. The app serializes render and excludes
/// it during entry, exit and destruction. Callbacks retain no loans or render again.
pub unsafe trait Host {
    /// Zero-filled, aligned allocation compatible with the Rust global allocator.
    fn allocate_owner(layout: Allocation) -> *mut u8 {
        unsafe { alloc::alloc::alloc_zeroed(layout) }
    }
    fn lock(&self);
    fn unlock(&self);
    fn enter(&self);
    fn exit(&self);
    fn count(&self) -> u16;
    fn layout(&self) -> Layout;
    fn with_resources<R>(&self, consume: impl FnOnce(&mut dyn Resources) -> R) -> R;
    fn paint(&self, scene: &Scene<'_>) -> bool;
    fn refresh(&self, refresh: Refresh);
    fn request_update(&self);
    fn haptic(&self);
    fn complete(&self, cancelled: bool, mode: u8);
    fn scroll_diagnostic(&self, delta: i32);
    fn scene_diagnostic(&self, invalid: bool);
}
struct Guard<'a, H: Host>(&'a H);
impl<H: Host> Drop for Guard<'_, H> {
    fn drop(&mut self) {
        self.0.unlock();
    }
}
#[repr(C)]
struct Model {
    state: ListState,
    hits: hits::Layout,
    gate: SceneGate,
    ready: bool,
    touch_activated: bool,
    exited: bool,
    reserved: u8,
}
#[repr(C)]
struct Frame {
    next: ListState,
    hits: hits::Layout,
    commands: [Command; 24],
    scroll: i32,
}
#[repr(C)]
pub struct Modes<H: Host> {
    host: H,
    model: UnsafeCell<Model>,
    frame: UnsafeCell<Frame>,
}
impl<H: Host> Modes<H> {
    /// Consume the Box into a stable owner before retaining any raw target.
    pub fn try_new(host: H) -> Option<Box<Self>> {
        let out = H::allocate_owner(Allocation::new::<Self>()).cast::<Self>();
        if out.is_null() {
            return None;
        }
        // Every frame/model field is zero-valid POD, written in its final storage.
        unsafe {
            ptr::addr_of_mut!((*out).model).write_bytes(0, 1);
            ptr::addr_of_mut!((*out).frame).write_bytes(0, 1);
            ptr::addr_of_mut!((*out).host).write(host);
            Some(Box::from_raw(out))
        }
    }
    pub fn host(&self) -> &H {
        &self.host
    }
    fn model<R>(&self, f: impl FnOnce(&mut Model) -> R) -> R {
        self.host.lock();
        let _guard = Guard(&self.host);
        f(unsafe { &mut *self.model.get() })
    }
    pub fn enter(&self) {
        self.host.enter();
        self.model(|m| {
            m.state = ListState::new(0);
            m.ready = false;
            m.exited = false;
        });
        self.host.request_update();
    }
    pub fn exit(&self) {
        self.host.exit();
        self.model(|m| {
            m.exited = true;
            m.ready = false;
        });
    }
    fn reject(&self) {
        self.model(|m| {
            m.gate.reject();
            m.ready = false;
            m.state.active = -1;
            m.state.last_repeat = 0;
            m.state.pending_scroll = 0;
            m.state.navigation_revision = m.state.navigation_revision.wrapping_add(1);
            m.touch_activated = false;
        });
    }
    pub fn input(&self, input: Input) {
        let admission = self.model(|m| (!m.exited).then(|| m.gate.input(&input)));
        let Some(admission) = admission else {
            return;
        };
        if admission.retry {
            self.host.request_update();
            if input.released & model::BACK != 0 {
                self.host.complete(true, 0);
            }
        }
        if admission.consume {
            return;
        }
        let count = self.host.count();
        let effect = self.model(|m| {
            if m.gate.failed() || !m.state.interest(input.now_ms).due(&input) {
                return None;
            }
            m.touch_activated = false;
            // The native adapter's scene has no interactions; the mode hit table
            // resolves a local input copy, leaving outer action/haptics unchanged.
            let mut resolved = input;
            if m.ready && resolved.touch_flags != 0 {
                if let Some(hit) = m.hits.hit(resolved.touch_x, resolved.touch_y) {
                    resolved.action = hit.action;
                    resolved.value = hit.value;
                }
            }
            let effect = unsafe { settings_ffi::update(&mut m.state, &resolved, count) };
            m.touch_activated = resolved.action != 0
                && resolved.touch_flags & model::TOUCH_RELEASED != 0
                && effect.kind == EffectKind::Activate as u8;
            Some(effect)
        });
        let Some(effect) = effect else {
            return;
        };
        if self.model(|m| m.gate.failed()) {
            self.host.request_update();
            return;
        }
        if input.action != 0
            && input.touch_flags & model::TOUCH_RELEASED != 0
            && effect.kind == EffectKind::Activate as u8
        {
            self.host.haptic();
        }
        if effect.invalidate != 0 {
            self.host.request_update();
        }
        self.apply(effect);
    }
    fn apply(&self, effect: Effect) {
        match effect.kind {
            k if k == EffectKind::Cancel as u8 => self.host.complete(true, 0),
            k if k == EffectKind::Activate as u8 => {
                if self.model(|m| m.touch_activated) {
                    self.host.haptic();
                }
                self.host.complete(false, effect.value as u8);
            }
            k if k == EffectKind::Scroll as u8 => self.host.scroll_diagnostic(effect.value),
            _ => {}
        }
    }
    #[inline(never)]
    pub fn render(&self) -> bool {
        let state = self.model(|m| m.state);
        // App's render lock guarantees one writer; input touches only model.
        let frame = unsafe { &mut *self.frame.get() };
        frame.scroll = state.pending_scroll;
        let layout = self.host.layout();
        let count = self.host.count();
        let built = self.host.with_resources(|resources| {
            super::modes::build(
                &mut frame.commands,
                &mut frame.hits,
                state,
                layout,
                resources,
                count,
            )
        });
        let Ok((next, command_count)) = built else {
            self.reject();
            self.host.scene_diagnostic(false);
            return false;
        };
        frame.next = next;
        let scene = Scene {
            commands: &frame.commands[..command_count],
            text: &[],
            interactions: &[],
            refresh: Refresh::Fast,
            generation: 1,
        };
        if !self.host.paint(&scene) {
            self.host.scene_diagnostic(true);
            self.reject();
            return false;
        }
        let current = self.model(|m| {
            let current = m.state.navigation_revision == frame.next.navigation_revision;
            m.ready = m.gate.publish(current) == Publication::Current;
            if m.ready {
                m.state.publish(&frame.next, frame.scroll);
                m.hits = frame.hits;
            }
            m.ready
        });
        if !current {
            self.host.request_update();
        }
        self.host.refresh(scene.refresh);
        true
    }
}

#[cfg(test)]
mod tests;
