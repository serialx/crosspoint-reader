//! Six-row settings root; section contents and persistence stay with its children.
extern crate alloc;
mod rows;
mod storage;
use super::{hits, view};
pub use crate::utility::owner::Memory;
use crate::{
    model::{self, Effect, EffectKind, Input, ListState},
    runtime::{Publication, SceneGate},
    scene::{Command, Error, Rect, Refresh, Scene},
};
use alloc::boxed::Box;
use core::{alloc::Layout as Allocation, cell::UnsafeCell, ffi::CStr, ptr};
use rows::Rows;
use storage::Commands;

#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Section {
    General,
    Display,
    Reader,
    Controls,
    Library,
    Network,
}
impl Section {
    pub fn from_byte(value: u8) -> Option<Self> {
        Some(match value {
            0 => Self::General,
            1 => Self::Display,
            2 => Self::Reader,
            3 => Self::Controls,
            4 => Self::Library,
            5 => Self::Network,
            _ => return None,
        })
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Log {
    Enter,
    Exit,
    Arena(usize),
    Build,
    Paint,
    Scroll(i32),
}

/// # Safety
/// Model locks serialize input and publication. Render has one caller; lifecycle
/// changes exclude rendering. Callbacks retain no loans and cannot reenter render.
pub unsafe trait Host {
    type Memory: Memory;
    fn lock(&self);
    fn unlock(&self);
    /// Translations are immutable built-in table strings, including their NUL.
    fn translate(&self, id: u16) -> &'static CStr;
    fn layout(&self, out: &mut view::Layout);
    fn width(&self, text: &CStr, font: i32, style: u8) -> i32;
    fn height(&self, text: &CStr, font: i32, style: u8, lines: u8, width: i16) -> i32;
    fn line(&self, font: i32) -> i32;
    fn paint(&self, scene: &Scene<'_>, resources: &mut impl Resources) -> bool;
    fn text(&self, command: &Command, clip: Rect, text: &CStr);
    fn chrome(&self, command: &Command, clip: Rect, title: &CStr, icon: u8);
    fn footer(&self);
    fn refresh(&self, refresh: Refresh);
    fn request_update(&self);
    fn haptic(&self);
    fn home(&self);
    /// Child factory preserves its allocation-stage diagnostics.
    fn open_section(&self, section: Section);
    fn theme_reload(&self);
    fn log(&self, event: Log);
}
pub trait Resources {
    fn valid(&self, handle: u32, generation: u32) -> bool;
    fn paint(&mut self, command: &Command, clip: Rect);
}
struct Model {
    state: ListState,
    hits: hits::Layout,
    labels: [&'static CStr; 6],
    gate: SceneGate,
    pending_section: Option<Section>,
    effect: Effect,
    ready: bool,
    entered: bool,
    exited: bool,
}
struct Frame {
    snapshot: ListState,
    next: ListState,
    hits: hits::Layout,
    layout: view::Layout,
    labels: [&'static CStr; 6],
}
pub struct Root<H: Host> {
    host: H,
    model: UnsafeCell<Model>,
    frame: UnsafeCell<Frame>,
    commands: UnsafeCell<Commands<H::Memory>>,
}
impl<H: Host> Root<H> {
    /// Consume this Box into the application's StableOwner before exposing a target.
    pub fn try_new(host: H, initial_section: u8) -> Option<Box<Self>> {
        if initial_section > 6 {
            return None;
        }
        let out = H::Memory::owner(Allocation::new::<Self>()).cast::<Self>();
        if out.is_null() {
            return None;
        }
        unsafe {
            ptr::addr_of_mut!((*out).host).write(host);
            ptr::addr_of_mut!((*out).model).write(UnsafeCell::new(Model {
                state: ListState::new(0),
                hits: hits::Layout::default(),
                labels: [c""; 6],
                gate: SceneGate::default(),
                pending_section: Section::from_byte(initial_section),
                effect: Effect::default(),
                ready: false,
                entered: false,
                exited: false,
            }));
            let frame = ptr::addr_of_mut!((*out).frame).cast::<Frame>();
            // Only the POD fields can be zero-initialized; CStr references cannot.
            ptr::addr_of_mut!((*frame).snapshot).write(ListState::new(0));
            ptr::addr_of_mut!((*frame).next).write(ListState::new(0));
            ptr::addr_of_mut!((*frame).hits).write_bytes(0, 1);
            ptr::addr_of_mut!((*frame).layout).write_bytes(0, 1);
            ptr::addr_of_mut!((*frame).labels).write([c""; 6]);
            ptr::addr_of_mut!((*out).commands).write(UnsafeCell::new(Commands::new()));
            Some(Box::from_raw(out))
        }
    }
    fn model<R>(&self, f: impl FnOnce(&mut Model) -> R) -> R {
        struct Guard<'a, H: Host>(&'a H);
        impl<H: Host> Drop for Guard<'_, H> {
            fn drop(&mut self) {
                self.0.unlock();
            }
        }
        self.host.lock();
        let _guard = Guard(&self.host);
        f(unsafe { &mut *self.model.get() })
    }
    pub fn host(&self) -> &H {
        &self.host
    }
    pub fn selected(&self) -> i32 {
        self.model(|m| m.state.selected)
    }
    pub fn ready(&self) -> bool {
        self.model(|m| m.ready)
    }
    // Keep label storage and the model copy out of the Settings entry frame.
    #[inline(never)]
    fn rebuild_rows(&self) {
        let labels = rows::TITLES.map(|id| self.host.translate(id));
        self.model(|m| m.labels = labels);
    }
    pub fn enter(&self) -> bool {
        self.host.log(Log::Enter);
        self.model(|m| {
            m.ready = false;
            m.entered = false;
            m.exited = false;
            m.state = ListState::new(0);
        });
        if !self.reserve(32) {
            self.host.home();
            return false;
        }
        self.host.request_update();
        self.rebuild_rows();
        self.model(|m| m.entered = true);
        true
    }
    fn reserve(&self, count: u16) -> bool {
        let commands = unsafe { &mut *self.commands.get() };
        let success = commands.reserve(count);
        if !success && count <= 128 {
            self.host.log(Log::Arena(usize::from(count) * 32));
        }
        success
    }
    pub fn returned(&self) {
        self.rebuild_rows();
        self.model(|m| {
            m.state.navigation_revision = m.state.navigation_revision.wrapping_add(1);
            m.ready = false;
        });
        self.host.request_update();
    }
    fn open(&self, section: Section) {
        self.model(|m| {
            m.state.selected = i32::from(section as u8);
            m.state.follow = 1;
            m.state.navigation_revision = m.state.navigation_revision.wrapping_add(1);
        });
        self.host.open_section(section);
    }
    fn reject(&self) {
        self.model(|m| {
            m.gate.reject();
            m.ready = false;
            m.effect = Effect::default();
            m.state.active = -1;
            m.state.pending_scroll = 0;
            m.state.last_repeat = 0;
            m.state.navigation_revision = m.state.navigation_revision.wrapping_add(1);
        });
    }
    pub fn input(&self, mut input: Input) {
        if self.model(|m| m.exited) {
            return;
        }
        if let Some(section) = self.model(|m| m.pending_section.take()) {
            self.open(section);
            return;
        }
        let admission = self.model(|m| m.gate.input(&input));
        if admission.retry {
            self.host.request_update();
            if input.released & model::BACK != 0 {
                self.host.home();
            }
        }
        if admission.consume {
            return;
        }
        let effect = self.model(|m| {
            if m.gate.failed() || !m.state.interest(input.now_ms).due(&input) {
                return None;
            }
            if m.ready && input.touch_flags != 0 {
                if let Some(hit) = m.hits.hit(input.touch_x, input.touch_y) {
                    input.action = hit.action;
                    input.value = hit.value;
                }
            }
            m.effect = m.state.update(input, 6, true);
            Some(m.effect)
        });
        let Some(effect) = effect else {
            return;
        };
        if self.model(|m| m.gate.failed()) {
            self.host.request_update();
            return;
        }
        if effect.invalidate != 0 {
            self.host.request_update();
        }
        // Rendering may reject while request_update wakes it; discard that action.
        let effect = self.model(|m| core::mem::take(&mut m.effect));
        match effect.kind {
            x if x == EffectKind::Cancel as u8 => self.host.home(),
            x if x == EffectKind::Activate as u8 => {
                if input.action != 0 && input.touch_flags & model::TOUCH_RELEASED != 0 {
                    self.host.haptic();
                }
                if let Ok(value) = u8::try_from(effect.value) {
                    if let Some(section) = Section::from_byte(value) {
                        self.open(section);
                    }
                }
            }
            x if x == EffectKind::Scroll as u8 => self.host.log(Log::Scroll(effect.value)),
            _ => {}
        }
    }
    #[inline(never)]
    pub fn render(&self) -> bool {
        if !self.model(|m| m.entered && !m.exited) {
            return false;
        }
        let frame = unsafe { &mut *self.frame.get() };
        self.model(|m| {
            frame.snapshot = m.state;
            frame.labels = m.labels;
        });
        self.host.layout(&mut frame.layout);
        let mut rows = Rows {
            host: &self.host,
            labels: &frame.labels,
        };
        let prepared = loop {
            let (present, capacity) = {
                let commands = unsafe { &*self.commands.get() };
                (commands.present(), commands.capacity())
            };
            if !present && !self.reserve(capacity) {
                break Err(Error::Capacity);
            }
            let result = {
                let commands = unsafe { &mut *self.commands.get() };
                view::prepare(
                    &frame.snapshot,
                    &frame.layout,
                    &mut rows,
                    6,
                    commands.slice(),
                    &mut frame.next,
                    &mut frame.hits,
                )
            };
            match result {
                Err(Error::Capacity) => {
                    let Some(next) = crate::runtime::next_capacity(capacity, 32, 16, 128) else {
                        break result;
                    };
                    if !self.reserve(next) {
                        break result;
                    }
                }
                _ => break result,
            }
        };
        let Ok(prepared) = prepared else {
            self.reject();
            self.host.log(Log::Build);
            return false;
        };
        let commands = unsafe { &*self.commands.get() };
        let scene = Scene {
            commands: &commands.read()[..usize::from(prepared.command_count)],
            text: &[],
            interactions: &[],
            generation: 1,
            refresh: prepared.refresh,
        };
        if !self.host.paint(&scene, &mut rows) {
            self.host.log(Log::Paint);
            self.reject();
            return false;
        }
        let current = self.model(|m| {
            let current = m.state.navigation_revision == frame.next.navigation_revision;
            if m.gate.publish(current) == Publication::Current {
                m.state.publish(&frame.next, frame.snapshot.pending_scroll);
                m.hits = frame.hits;
                m.ready = true;
            } else {
                m.ready = false;
            }
            current
        });
        if !current {
            self.host.request_update();
        }
        self.host.refresh(prepared.refresh);
        true
    }
    pub fn exit(&self) {
        let first = self.model(|m| {
            m.ready = false;
            m.entered = false;
            !core::mem::replace(&mut m.exited, true)
        });
        if first {
            unsafe { &mut *self.commands.get() }.clear();
            self.host.log(Log::Exit);
            self.host.theme_reload();
        }
    }
}
