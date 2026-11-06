//! Dormant keyboard lifetime. Physical leaves lend tables and pixels synchronously.
extern crate alloc;
pub mod storage;
use crate::{
    keyboard::{self, Effect, Layouts, State},
    keyboard_view::{self as view, Layout, Metrics, PaintKey, PlacedKey, TextSpan},
    model::{self, Input, Interest},
    runtime::SceneGate,
    scene::{Arena, Command, Rect, Refresh, Scene},
};
use alloc::boxed::Box;
use core::{alloc::Layout as Allocation, cell::UnsafeCell, ptr};
use storage::{Block, Memory};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Lock {
    Text,
    Model,
}

/// # Safety
/// Locks serialize Text and Model access with all other users of this owner.
/// Loop and render each have one caller; exit/drop exclude both through RenderLock.
/// Callbacks may not reenter the same lane or retain a loan. Layout/metric callbacks
/// supply valid immutable descriptors for the enclosing call. No model-lock I/O.
pub unsafe trait Host {
    type Memory: Memory;
    fn lock(&self, kind: Lock);
    fn unlock(&self, kind: Lock);
    fn settings(&self) -> (u16, u8);
    fn layout(&self) -> Layout;
    fn with_layouts<R>(&self, consume: impl FnOnce(&Layouts) -> R) -> R;
    fn with_metrics<R>(&self, consume: impl FnOnce(&Metrics) -> R) -> R;
    fn paint(&self, scene: &Scene<'_>, resources: &mut impl Resources) -> bool;
    fn chrome(&self, command: &Command, clip: Rect, title: &[u8]);
    fn key(&self, command: &Command, key: &PaintKey, layout: u8, symbols: bool, mode_abc: bool);
    fn text(&self, command: &Command, clip: Rect, terminated: &[u8]);
    fn refresh(&self);
    fn request_update(&self);
    fn haptic(&self, held: bool);
    fn error(&self, message: &'static str, bytes: u32);
}

pub trait Resources {
    fn valid(&mut self, resource: u32, generation: u32) -> bool;
    fn paint(&mut self, command: &Command, clip: Rect);
}
struct Guard<'a, H: Host>(&'a H, Lock);
impl<'a, H: Host> Guard<'a, H> {
    fn new(host: &'a H, lock: Lock) -> Self {
        host.lock(lock);
        Self(host, lock)
    }
}
impl<H: Host> Drop for Guard<'_, H> {
    fn drop(&mut self) {
        self.0.unlock(self.1);
    }
}

struct Edit<M: Memory> {
    text: Block<u8, M>,
    state: State,
    before: State,
    pending: Effect,
    input: Input,
    tapped_cursor: u32,
    result: u8,
}
struct Published {
    gate: SceneGate,
    ready: bool,
    pending: bool,
    reset: bool,
    interest: Interest,
    previous: State,
    state: State,
    layout: Layout,
}
struct Render<M: Memory> {
    commands: Block<Command, M>,
    spans: Block<TextSpan, M>,
    scene_text: Block<u8, M>,
    snapshot: State,
    raw: Block<u8, M>,
    display: Block<u8, M>,
    paint_text: Block<u8, M>,
    row_keys: [PlacedKey; 16],
}
impl<M: Memory> Render<M> {
    fn clear(&mut self) {
        self.paint_text.clear();
        self.display.clear();
        self.raw.clear();
        self.scene_text.clear();
        self.spans.clear();
        self.commands.clear();
    }
    fn masked(&self) -> bool {
        self.snapshot.input_type == 1 && self.snapshot.password_visible == 0
    }
    fn build(
        &mut self,
        layout: &Layout,
        layouts: &Layouts,
        metrics: &Metrics,
    ) -> Option<(usize, usize)> {
        let masked = self.masked();
        if masked {
            if !self.display.grow(self.snapshot.length + 1, 0) {
                return None;
            }
            if !self
                .snapshot
                .mask(self.raw.as_slice(), self.display.as_mut_slice())
            {
                return None;
            }
        } else {
            self.display.clear();
        }
        if self.commands.capacity() == 0 && !unsafe { self.commands.replace(24, 0) } {
            return None;
        }
        if self.spans.capacity() == 0 && !unsafe { self.spans.replace(8, 0) } {
            return None;
        }
        if !self.scene_text.grow(512, 0) {
            return None;
        }
        loop {
            for span in self.spans.as_mut_slice() {
                span.raw = 255;
            }
            let mut arena = Arena::new(
                self.commands.as_mut_slice(),
                self.scene_text.as_mut_slice(),
                &mut [],
            )
            .ok()?;
            let length = self.snapshot.length as usize;
            let display = if masked {
                self.display.as_slice()
            } else {
                self.raw.as_slice()
            };
            if view::view(
                &mut arena,
                &self.snapshot,
                (&self.raw.as_slice()[..length], &display[..length]),
                layout,
                metrics,
                layouts,
                self.spans.as_mut_slice(),
            )
            .is_ok()
            {
                let scene = arena.finish(Refresh::Fast, 1).ok()?;
                let counts = (scene.commands.len(), scene.text.len());
                let longest = self
                    .spans
                    .as_slice()
                    .iter()
                    .filter(|s| s.raw <= 1)
                    .map(|s| s.length)
                    .max()
                    .unwrap_or(0);
                return self.paint_text.grow(longest + 1, 0).then_some(counts);
            }
            if self.commands.capacity() == 256
                && self.spans.capacity() == 64
                && self.scene_text.capacity() == 4096
            {
                return None;
            }
            let capacity = (self.commands.capacity() * 2).min(256);
            if capacity != self.commands.capacity()
                && !unsafe { self.commands.replace(capacity, 0) }
            {
                return None;
            }
            if self.spans.capacity() < 64 {
                if !unsafe { self.spans.replace((self.spans.capacity() * 2).min(64), 0) } {
                    return None;
                }
            } else if !self
                .scene_text
                .grow((self.scene_text.capacity() * 2).min(4096), 0)
            {
                return None;
            }
        }
    }
}

pub enum Result<M: Memory> {
    Cancelled,
    Text { bytes: Block<u8, M>, length: u32 },
}
impl<M: Memory> Result<M> {
    pub fn is_cancelled(&self) -> bool {
        matches!(self, Self::Cancelled)
    }
    pub fn text(&self) -> Option<&[u8]> {
        match self {
            Self::Cancelled => None,
            Self::Text { bytes, length } => Some(&bytes.as_slice()[..*length as usize]),
        }
    }
}

pub struct Keyboard<H: Host> {
    host: H,
    title: Block<u8, H::Memory>,
    layout: Layout,
    edit: UnsafeCell<Edit<H::Memory>>,
    published: UnsafeCell<Published>,
    render: UnsafeCell<Render<H::Memory>>,
}
impl<H: Host> Keyboard<H> {
    /// No address borrowed from this Box may be retained across an owning move.
    pub fn try_new(
        host: H,
        title: &[u8],
        initial: &[u8],
        maximum: usize,
        input_type: u8,
    ) -> Option<Box<Self>> {
        let pointer = H::Memory::owner(Allocation::new::<Self>()).cast::<Self>();
        if pointer.is_null() {
            host.error("Cannot allocate keyboard scene", 0);
            return None;
        }
        // Every field other than H is zero-valid POD or an empty Block. Initialize
        // H in its final allocation; no large owner temporary or raw target escapes.
        unsafe {
            ptr::addr_of_mut!((*pointer).host).write(host);
        }
        let mut owner = unsafe { Box::from_raw(pointer) };
        if title.len() >= u32::MAX as usize
            || initial.len() >= u32::MAX as usize
            || maximum > u32::MAX as usize
            || !owner.title.grow(title.len() as u32 + 1, 0)
        {
            owner.host.error("Cannot allocate keyboard scene", 0);
            return None;
        }
        owner.title.as_mut_slice()[..title.len()].copy_from_slice(title);
        let edit = owner.edit.get_mut();
        if !edit.text.grow(initial.len() as u32 + 1, 0) {
            owner.host.error("Cannot allocate keyboard scene", 0);
            return None;
        }
        edit.text.as_mut_slice()[..initial.len()].copy_from_slice(initial);
        let (configured, language) = owner.host.settings();
        edit.state = keyboard::init(
            initial.len() as u32,
            maximum as u32,
            input_type,
            keyboard::enabled(configured, language),
            language,
        );
        owner.published.get_mut().interest = keyboard::interest(&edit.state);
        owner.layout = owner.host.layout();
        owner.host.request_update();
        Some(owner)
    }
    fn model<R>(&self, consume: impl FnOnce(&mut Published) -> R) -> R {
        let _guard = Guard::new(&self.host, Lock::Model);
        consume(unsafe { &mut *self.published.get() })
    }
    pub fn ready(&self) -> bool {
        self.model(|p| p.ready)
    }
    fn reject(&self) {
        self.model(|p| {
            p.gate.reject();
            p.ready = false;
            p.reset = true;
        });
    }
    fn reconcile(&self) {
        let _guard = Guard::new(&self.host, Lock::Text);
        let edit = unsafe { &mut *self.edit.get() };
        self.host.with_layouts(|layouts| {
            self.model(|p| {
                if p.pending {
                    keyboard::publish(&mut edit.state, &p.previous, &p.state, layouts);
                    p.pending = false;
                }
                if p.reset {
                    edit.pending = Effect::default();
                    edit.input = Input::default();
                    edit.tapped_cursor = u32::MAX;
                    let s = &mut edit.state;
                    s.confirm_held = 0;
                    s.confirm_long = 0;
                    s.up_held = 0;
                    s.up_long = 0;
                    s.down_held = 0;
                    s.down_long = 0;
                    s.right_held = 0;
                    s.right_long = 0;
                    s.touch_held = 0;
                    s.touch_long = 0;
                    s.active_key = i16::MIN;
                    s.active_index = -1;
                    s.last_repeat = 0;
                    s.touch_start = 0;
                    p.reset = false;
                }
                p.interest = keyboard::interest(&edit.state);
            })
        });
    }
    pub fn input(&self, mut input: Input, tap: Option<(i16, i16)>) {
        if unsafe { (*self.edit.get()).text.capacity() } == 0 {
            return;
        }
        self.reconcile();
        let admission = self.model(|p| p.gate.input(&input));
        if admission.retry {
            self.host.request_update();
            if input.released & model::BACK != 0 {
                let _guard = Guard::new(&self.host, Lock::Text);
                unsafe {
                    (*self.edit.get()).result = model::EffectKind::Cancel as u8;
                }
            }
        }
        if admission.consume {
            return;
        }
        let mut tapped = u32::MAX;
        self.host.with_layouts(|layouts| {
            if let Some((x, y)) = tap {
                let key = self
                    .model(|p| p.ready && view::hit(&p.state, layouts, &p.layout, x, y).is_some());
                if !key {
                    // Only the loop mutates edit bytes; render only copies them.
                    // Keep cursor allocation and measurement outside TextLock.
                    let edit = unsafe { &*self.edit.get() };
                    let state = &edit.state;
                    let text = &edit.text.as_slice()[..state.length as usize];
                    let mut display = Block::<u8, H::Memory>::default();
                    let masked = state.input_type == 1 && state.password_visible == 0;
                    if masked && !display.grow(state.length + 1, 0) {
                        self.host.error(
                            "Cannot allocate keyboard touch text: %u bytes",
                            state.length + 1,
                        );
                        // Continue routing the frame without a field cursor.
                    } else {
                        if masked {
                            state.mask(text, display.as_mut_slice());
                        }
                        // Loop reads only the immutable geometry initialized at entry.
                        let layout = &self.layout;
                        tapped = self
                            .host
                            .with_metrics(|metrics| {
                                view::cursor_at(
                                    state,
                                    if masked {
                                        &display.as_slice()[..state.length as usize]
                                    } else {
                                        text
                                    },
                                    layout,
                                    metrics,
                                    layouts,
                                    x,
                                    y,
                                )
                            })
                            .unwrap_or(u32::MAX);
                    }
                }
            }
            let due = self.model(|p| {
                if p.gate.failed() || !p.interest.due(&input) {
                    return false;
                }
                if p.ready && input.touch_flags != 0 {
                    if let Some(hit) =
                        view::hit(&p.state, layouts, &p.layout, input.touch_x, input.touch_y)
                    {
                        input.action = hit.action;
                        input.value = hit.value;
                    }
                }
                true
            });
            if !due {
                return;
            }
            if self.model(|p| p.gate.failed()) {
                self.host.request_update();
                return;
            }
            let (navigation, haptics) = {
                let _guard = Guard::new(&self.host, Lock::Text);
                // Rendering can reject while the loop waits for TextLock.
                if self.model(|p| p.gate.failed()) {
                    self.host.request_update();
                    return;
                }
                let edit = unsafe { &mut *self.edit.get() };
                edit.before = edit.state;
                edit.input = input;
                edit.tapped_cursor = tapped;
                edit.pending = keyboard::update(
                    &mut edit.state,
                    edit.text.as_mut_slice(),
                    layouts,
                    &input,
                    tapped,
                );
                if edit.pending.grow != 0 {
                    if edit
                        .text
                        .grow(edit.pending.required_capacity, edit.state.length + 1)
                    {
                        edit.state = edit.before;
                        edit.pending = keyboard::update(
                            &mut edit.state,
                            edit.text.as_mut_slice(),
                            layouts,
                            &input,
                            tapped,
                        );
                    } else {
                        self.host.error(
                            "Cannot grow keyboard text: %u bytes",
                            edit.pending.required_capacity,
                        );
                    }
                }
                self.model(|p| p.interest = keyboard::interest(&edit.state));
                let effect = edit.pending;
                edit.pending = Effect::default();
                (effect.navigation, effect.haptics)
            };
            if navigation.invalidate != 0 {
                self.host.request_update();
            }
            if haptics != 0 {
                self.host.haptic(haptics == 2);
            }
            if navigation.kind == model::EffectKind::Activate as u8
                || navigation.kind == model::EffectKind::Cancel as u8
            {
                let _guard = Guard::new(&self.host, Lock::Text);
                unsafe {
                    (*self.edit.get()).result = navigation.kind;
                }
            }
        });
    }
    pub fn render(&self) -> bool {
        let render = unsafe { &mut *self.render.get() };
        self.host.with_layouts(|layouts| {
            loop {
                let required = {
                    let _guard = Guard::new(&self.host, Lock::Text);
                    let edit = unsafe { &*self.edit.get() };
                    (edit.text.capacity() != 0).then_some(edit.state.length + 1)
                };
                let Some(required) = required else {
                    self.reject();
                    self.host.error("Cannot build scene in %s", 0);
                    return false;
                };
                if !render.raw.grow(required, 0) {
                    self.reject();
                    self.host.error("Cannot build scene in %s", 0);
                    return false;
                }
                let _guard = Guard::new(&self.host, Lock::Text);
                let edit = unsafe { &*self.edit.get() };
                if edit.text.capacity() == 0 {
                    return false;
                }
                if edit.state.length >= render.raw.capacity() {
                    continue;
                }
                render.snapshot = edit.state;
                self.model(|p| {
                    if p.pending {
                        keyboard::publish(&mut render.snapshot, &p.previous, &p.state, layouts);
                    }
                    keyboard::publish(&mut render.snapshot, &p.state, &edit.state, layouts);
                });
                let count = edit.state.length as usize + 1;
                render.raw.as_mut_slice()[..count].copy_from_slice(&edit.text.as_slice()[..count]);
                break;
            }
            let Some((commands, text)) = self
                .host
                .with_metrics(|metrics| render.build(&self.layout, layouts, metrics))
            else {
                self.reject();
                self.host.error("Cannot build scene in %s", 0);
                return false;
            };
            let scene = Scene {
                commands: &render.commands.as_slice()[..commands],
                text: &render.scene_text.as_slice()[..text],
                interactions: &[],
                refresh: Refresh::Fast,
                generation: 1,
            };
            let mut resources = PaintResources {
                host: &self.host,
                layouts,
                state: &render.snapshot,
                layout: &self.layout,
                title: self.title.as_slice(),
                raw: render.raw.as_slice(),
                display: render.display.as_slice(),
                spans: render.spans.as_slice(),
                text: render.paint_text.as_mut_slice(),
                keys: &mut render.row_keys,
            };
            if !self.host.paint(&scene, &mut resources) {
                self.host.error("Invalid scene in %s", 0);
                self.reject();
                return false;
            }
            self.model(|p| {
                p.gate.publish(true);
                if !p.pending {
                    p.previous = p.state;
                }
                p.state = render.snapshot;
                p.layout = self.layout;
                p.pending = true;
                p.ready = true;
            });
            self.host.refresh();
            true
        })
    }
    pub fn take_result(&self) -> Option<Result<H::Memory>> {
        let _guard = Guard::new(&self.host, Lock::Text);
        let edit = unsafe { &mut *self.edit.get() };
        match edit.result {
            1 => {
                edit.result = 0;
                Some(Result::Cancelled)
            }
            2 => {
                edit.result = 0;
                Some(Result::Text {
                    bytes: core::mem::take(&mut edit.text),
                    length: edit.state.length,
                })
            }
            _ => None,
        }
    }
    /// The caller holds the app RenderLock and excludes loop input.
    pub fn exit(&self) {
        self.model(|p| p.ready = false);
        unsafe {
            (*self.render.get()).clear();
            (*self.edit.get()).text.clear();
        }
    }
}
impl<H: Host> Drop for Keyboard<H> {
    fn drop(&mut self) {
        self.render.get_mut().clear();
        self.edit.get_mut().text.clear();
    }
}

struct PaintResources<'a, H: Host> {
    host: &'a H,
    layouts: &'a Layouts,
    state: &'a State,
    layout: &'a Layout,
    title: &'a [u8],
    raw: &'a [u8],
    display: &'a [u8],
    spans: &'a [TextSpan],
    text: &'a mut [u8],
    keys: &'a mut [PlacedKey; 16],
}
impl<H: Host> PaintResources<'_, H> {
    fn span(&self, resource: u32) -> Option<TextSpan> {
        let span = *self.spans.get(resource.checked_sub(0x200)? as usize)?;
        (span.raw <= 1
            && span.start <= self.state.length
            && span.length <= self.state.length - span.start
            && (span.length as usize) < self.text.len())
        .then_some(span)
    }
}
impl<H: Host> Resources for PaintResources<'_, H> {
    fn valid(&mut self, resource: u32, generation: u32) -> bool {
        if generation != 1 {
            return false;
        }
        if resource >= 0x200 {
            return self.span(resource).is_some();
        }
        if (1..=3).contains(&resource) {
            return true;
        }
        (0x100..0x105).contains(&resource)
            && view::row_keys(
                self.state,
                self.layouts,
                self.layout,
                (resource - 0x100) as u8,
                self.keys,
            ) != 0
    }
    fn paint(&mut self, command: &Command, clip: Rect) {
        if !self.valid(command.resource, command.generation) {
            return;
        }
        match command.resource {
            1..=3 => self.host.chrome(command, clip, self.title),
            0x100..=0x104 => {
                let row = (command.resource - 0x100) as u8;
                let count = view::row_keys(self.state, self.layouts, self.layout, row, self.keys);
                for col in 0..count {
                    let Some(key) = view::paint_key(self.state, self.layouts, row, col) else {
                        continue;
                    };
                    let placed = self.keys[col as usize];
                    let command = Command {
                        resource: 0x100 + u32::from(row) * 16 + u32::from(col),
                        rect: placed.visual,
                        paint: placed.selected,
                        ..Command::default()
                    };
                    self.host.key(
                        &command,
                        &key,
                        self.state.layout,
                        self.state.symbols != 0,
                        self.state.symbols != 0
                            || self.state.input_type == 2 && self.state.url_panel != 0,
                    );
                }
            }
            _ => {
                let Some(span) = self.span(command.resource) else {
                    return;
                };
                let masked = self.state.input_type == 1 && self.state.password_visible == 0;
                let source = if span.raw == 0 && masked {
                    self.display
                } else {
                    self.raw
                };
                let n = span.length as usize;
                let start = span.start as usize;
                self.text[..n].copy_from_slice(&source[start..start + n]);
                self.text[n] = 0;
                self.host.text(command, clip, &self.text[..=n]);
            }
        }
    }
}

#[cfg(test)]
mod tests;

#[cfg(test)]
mod lifecycle_tests;
