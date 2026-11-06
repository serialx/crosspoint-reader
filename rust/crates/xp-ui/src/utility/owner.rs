//! Dormant dialog owners. Native launch/result installation is a later checkpoint.
extern crate alloc;
mod labels;
mod message;
mod storage;
use super::{Feedback, Layout, State};
use crate::{
    model::{self, EffectKind, Input, Metrics},
    runtime::{Publication, SceneGate},
    scene::{Arena, Command, Error, Rect, Refresh, Scene},
};
use alloc::boxed::Box;
use core::{alloc::Layout as Allocation, cell::UnsafeCell, ptr};
pub use labels::{Confirmation, ConfirmationResult, Interval, IntervalResult, Label, Spec};
pub use message::{Message, MessageResult};
pub use storage::Memory;
use storage::Storage;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Log {
    Enter,
    Exit,
    Arena(usize),
    Build,
    Paint,
    MessageBuild,
}
/// Physical leaves only. Text loans exclude their terminating NUL.
/// # Safety
/// Model locks serialize input/publication. Render has one caller and never
/// reenters render; entry/exit/drop exclude input and render via the app lock.
/// Callbacks retain no loans. No callback may mutate an outstanding owner loan.
pub unsafe trait Host {
    type Memory: Memory;
    fn lock(&self);
    fn unlock(&self);
    fn layout(&self) -> Layout;
    fn with_translation<R>(&self, id: u16, consume: impl FnOnce(&[u8]) -> R) -> R;
    /// Same bounded snprintf semantics as native, including byte truncation/NUL.
    fn format_number(&self, id: u16, value: i32, unsigned: bool, out: &mut [u8]);
    fn with_metrics<R>(&self, consume: impl FnOnce(&mut dyn Metrics) -> R) -> R;
    fn text_height(&self, text: &[u8], font: i32, width: i16, style: u8, lines: u8) -> i16;
    fn paint(&self, scene: &Scene<'_>, resources: &mut impl Resources) -> bool;
    fn text(&self, command: &Command, clip: Rect, text: &[u8]);
    /// Map these labels and preserve native footer clip/orientation handling.
    fn footer(&self, command: &Command, clip: Rect, labels: [Label; 4]);
    fn refresh(&self, refresh: Refresh);
    fn request_update(&self, immediate: bool);
    fn haptic(&self);
    fn finish(&self);
    /// Preserve native ACT/RUI strings and levels; host supplies the activity name.
    fn log(&self, event: Log);
}
pub trait Resources {
    fn valid(&mut self, resource: u32, generation: u32) -> bool;
    fn paint(&mut self, command: &Command, clip: Rect);
}
struct Guard<'a, H: Host>(&'a H);
impl<H: Host> Drop for Guard<'_, H> {
    fn drop(&mut self) {
        self.0.unlock();
    }
}
struct Model<R> {
    state: State,
    feedback: Feedback,
    gate: SceneGate,
    ready: bool,
    bank: usize,
    count: usize,
    result: Option<R>,
    pending: model::Effect,
    exited: bool,
}
struct Frame<B> {
    state: State,
    feedback: Feedback,
    layout: Layout,
    labels: B,
}
pub struct Dialog<H: Host, S: Spec> {
    host: H,
    spec: S,
    model: UnsafeCell<Model<S::Result>>,
    frame: UnsafeCell<Frame<S::Buffers>>,
    storage: UnsafeCell<Storage<H::Memory>>,
}
impl<H: Host> Dialog<H, Confirmation<H::Memory>> {
    pub fn confirmation(
        host: H,
        heading: &[u8],
        body: &[u8],
        cancel: &[u8],
        confirm: &[u8],
    ) -> Option<Box<Self>> {
        Self::create(host, || Confirmation::copy(heading, body, cancel, confirm))
    }
}
impl<H: Host> Dialog<H, Interval> {
    pub fn interval(host: H, spec: Interval) -> Option<Box<Self>> {
        Self::create(host, || Some(spec))
    }
}
impl<H: Host, S: Spec> Dialog<H, S> {
    /// No target or address borrowed from this Box may escape an owning move.
    fn create(host: H, make_spec: impl FnOnce() -> Option<S>) -> Option<Box<Self>> {
        let out = H::Memory::owner(Allocation::new::<Self>()).cast::<Self>();
        if out.is_null() {
            return None;
        }
        let Some(spec) = make_spec() else {
            unsafe { alloc::alloc::dealloc(out.cast(), Allocation::new::<Self>()) };
            return None;
        };
        unsafe {
            ptr::addr_of_mut!((*out).model).write(UnsafeCell::new(Model {
                state: spec.state(),
                feedback: Feedback::default(),
                gate: SceneGate::default(),
                ready: false,
                bank: 1,
                count: 0,
                result: None,
                pending: model::Effect::default(),
                exited: false,
            }));
            // Frame fields are all zero-valid POD, initialized in final storage.
            ptr::addr_of_mut!((*out).frame).write_bytes(0, 1);
            ptr::addr_of_mut!((*out).storage)
                .write(UnsafeCell::new(Storage::new(S::COMMANDS, S::HITS)));
            ptr::addr_of_mut!((*out).host).write(host);
            ptr::addr_of_mut!((*out).spec).write(spec);
            Some(Box::from_raw(out))
        }
    }
    fn model<R>(&self, f: impl FnOnce(&mut Model<S::Result>) -> R) -> R {
        self.host.lock();
        let _guard = Guard(&self.host);
        f(unsafe { &mut *self.model.get() })
    }
    pub fn ready(&self) -> bool {
        self.model(|m| m.ready)
    }
    pub fn is_reader(&self) -> bool {
        self.spec.reader()
    }
    pub fn enter(&self) -> bool {
        self.host.log(Log::Enter);
        self.model(|m| {
            m.ready = false;
            m.exited = false;
        });
        let storage = unsafe { &mut *self.storage.get() };
        if !storage.allocate() || !self.model(|m| m.state.init()) {
            self.host.log(Log::Arena(storage.bytes()));
            self.complete(true, 0);
            return false;
        }
        self.host.request_update(S::IMMEDIATE);
        true
    }
    fn complete(&self, cancelled: bool, value: i32) {
        self.model(|m| m.result = Some(S::result(cancelled, value)));
        self.host.finish();
    }
    fn reject(&self) {
        self.model(|m| {
            m.ready = false;
            m.pending = model::Effect::default();
            m.gate.reject();
            m.state.active_action = 0;
            m.state.contact_held = 0;
            m.state.dragging = 0;
            m.state.flash_pending = 0;
            m.state.last_drag_x = -1;
            m.state.last_repeat = 0;
        });
    }
    pub fn input(&self, mut input: Input) {
        let admission = self.model(|m| {
            if m.exited {
                None
            } else {
                Some(m.gate.input(&input))
            }
        });
        let Some(admission) = admission else {
            return;
        };
        if admission.retry {
            self.host.request_update(false);
            if input.released & model::BACK != 0 {
                self.complete(true, 0);
            }
        }
        if admission.consume {
            return;
        }
        let effect = self.model(|m| {
            if m.gate.failed() || !m.state.interest(input.now_ms).due(&input) {
                return None;
            }
            let storage = unsafe { &*self.storage.get() };
            let hits = if m.ready {
                Some(unsafe { storage.hits(m.bank, m.count) })
            } else {
                None
            };
            if let Some(hits) = hits.filter(|_| {
                input.touch_flags
                    & (model::TOUCH_PRESSED | model::TOUCH_RELEASED | model::TOUCH_HELD)
                    != 0
            }) {
                if let Some(h) = hits.iter().rev().find(|h| {
                    h.generation == 1
                        && h.flags & 1 == 0
                        && h.inputs & 1 != 0
                        && h.rect.contains(input.touch_x, input.touch_y)
                }) {
                    input.action = h.action;
                    input.value = h.value;
                }
            }
            m.pending = m.state.update(&input, hits, &m.feedback).navigation;
            Some(m.pending)
        });
        let Some(effect) = effect else {
            return;
        };
        if self.model(|m| m.gate.failed()) {
            self.host.request_update(false);
            return;
        }
        if input.action != 0
            && input.touch_flags & model::TOUCH_RELEASED != 0
            && effect.kind == EffectKind::Activate as u8
        {
            self.host.haptic();
        }
        if effect.invalidate != 0 {
            self.host.request_update(false);
        }
        // Rejection can clear the queued effect while request_update wakes render.
        let effect = self.model(|m| core::mem::take(&mut m.pending));
        if effect.reserved[0] != 0 && effect.kind != EffectKind::Activate as u8 {
            self.host.haptic();
        }
        match effect.kind {
            x if x == EffectKind::Cancel as u8 => self.complete(true, 0),
            x if x == EffectKind::Activate as u8 => self.complete(false, effect.value),
            _ => {}
        }
    }
    pub fn render(&self) -> bool {
        let storage = unsafe { &*self.storage.get() };
        if !storage.present() || self.model(|m| m.exited) {
            return false;
        }
        let frame = unsafe { &mut *self.frame.get() };
        let bank = self.model(|m| {
            frame.state = m.state;
            frame.feedback = m.feedback;
            1 - m.bank
        });
        self.spec
            .prepare(&self.host, &frame.state, &mut frame.labels);
        frame.layout = self.host.layout();
        let resources = labels::View {
            host: &self.host,
            spec: &self.spec,
            buffers: &frame.labels,
        };
        let built = (|| {
            let (commands, hits) = storage.build(bank);
            let commands =
                unsafe { core::slice::from_raw_parts_mut(commands, S::COMMANDS as usize) };
            let hits = unsafe { core::slice::from_raw_parts_mut(hits, S::HITS as usize) };
            let mut arena = Arena::new(commands, &mut [], hits)?;
            let (feedback, refresh) = self.host.with_metrics(|metrics| {
                super::view(
                    &mut arena,
                    &frame.state,
                    &frame.layout,
                    metrics,
                    &resources,
                    frame.feedback,
                )
            })?;
            let scene = arena.finish(refresh, 1)?;
            Ok::<_, Error>((scene, feedback))
        })();
        let Ok((scene, feedback)) = built else {
            self.reject();
            self.host.log(Log::Build);
            return false;
        };
        let mut resources = resources;
        if !self.host.paint(&scene, &mut resources) {
            self.host.log(Log::Paint);
            self.reject();
            return false;
        }
        self.model(|m| {
            if m.gate.publish(true) == Publication::Current {
                m.state.publish(&frame.state);
                m.feedback = feedback;
                m.bank = bank;
                m.count = scene.interactions.len();
                m.ready = true;
            }
        });
        self.host.refresh(scene.refresh);
        true
    }
    pub fn take_result(&self) -> Option<S::Result> {
        self.model(|m| m.result.take())
    }
    pub fn exit(&self) {
        let first = self.model(|m| {
            let first = !m.exited;
            m.exited = true;
            m.ready = false;
            first
        });
        if first {
            unsafe { &mut *self.storage.get() }.clear();
            self.host.log(Log::Exit);
        }
    }
}
