//! Visit ownership for Wi-Fi; radio and native result transport remain physical.
extern crate alloc;
mod effects;
mod view;
use super::{
    wifi::{self, Phase},
    wifi_physical::{Bytes, Diagnostic, Physical},
    wifi_resources::{Buffer, Error as StorageError, Memory, Row, SmallZ},
    wifi_view,
};
use crate::{
    ffi::Paint,
    model::{self, Input, ListState},
    runtime::{Publication, SceneGate},
    scene::{Command, Error, Refresh, Scene},
};
use alloc::boxed::Box;
use core::{alloc::Layout, cell::UnsafeCell, ffi::c_void, mem::ManuallyDrop, ptr};

/// # Safety
/// Model and Resource locks serialize their fields. App serializes rendering and
/// excludes it during entry, exit and destruction. Callbacks retain no loans or
/// recursively render. The returned owner allocation uses the global allocator.
pub unsafe trait Host {
    type Memory: Memory;
    fn allocate_owner(layout: Layout) -> *mut u8 {
        unsafe { alloc::alloc::alloc(layout) }
    }
    fn physical(&self) -> &Physical;
    fn object(&self) -> *mut c_void;
    fn model_lock(&self);
    fn model_unlock(&self);
    fn render_lock(&self) -> *mut c_void;
    fn render_unlock(&self, token: *mut c_void);
    fn enter(&self);
    fn exit(&self);
    fn now(&self) -> u32;
    fn delay(&self, ms: u32);
    fn sample_input(&self, long_press: bool) -> (Input, bool);
    fn request_update(&self);
    fn haptic(&self, long: bool);
    fn complete(&self);
    fn keyboard(&self, hidden: bool) -> bool;
    fn paint(&self, scene: &Scene<'_>, resources: &Paint) -> bool;
    fn refresh(&self, refresh: Refresh);
}
struct ModelGuard<'a, H: Host>(&'a H);
impl<H: Host> Drop for ModelGuard<'_, H> {
    fn drop(&mut self) {
        self.0.model_unlock();
    }
}
struct ResourceGuard<'a, H: Host>(&'a H);
impl<H: Host> Drop for ResourceGuard<'_, H> {
    fn drop(&mut self) {
        unsafe { (self.0.physical().resource_lock)(self.0.object(), 0) };
    }
}
struct RenderGuard<'a, H: Host>(&'a H, *mut c_void);
impl<H: Host> Drop for RenderGuard<'_, H> {
    fn drop(&mut self) {
        self.0.render_unlock(self.1);
    }
}
#[repr(u8)]
#[derive(Clone, Copy, PartialEq)]
enum Retry {
    None,
    Entry,
    Scan,
}
#[repr(C)]
struct Model {
    state: wifi::State,
    hits: wifi_view::Hits,
    gate: SceneGate,
    ready: bool,
    keyboard: bool,
    exited: bool,
    retry: Retry,
}
#[repr(C)]
struct Owned<M: Memory> {
    rows: Buffer<Row<M>, M>,
    selected: SmallZ<M>,
    password: SmallZ<M>,
    attempts: Buffer<SmallZ<M>, M>,
    ip: [u8; 16],
    mac: [u8; 64],
    error: *const core::ffi::c_char,
}
impl<M: Memory> Owned<M> {
    fn new() -> Self {
        Self {
            rows: Buffer::new(),
            selected: SmallZ::new(),
            password: SmallZ::new(),
            attempts: Buffer::new(),
            ip: [0; 16],
            mac: [0; 64],
            error: c"".as_ptr(),
        }
    }
}
#[repr(C)]
#[derive(Clone, Copy)]
struct VisibleRow {
    ssid: [u8; 33],
    status: [u8; 9],
    index: u16,
    hidden: u8,
}
impl VisibleRow {
    const EMPTY: Self = Self {
        ssid: [0; 33],
        status: [0; 9],
        index: u16::MAX,
        hidden: 0,
    };
}
#[repr(C)]
struct Frame<M: Memory> {
    state: wifi::State,
    next: ListState,
    hits: wifi_view::Hits,
    mac: [u8; 64],
    count_text: [u8; 64],
    commands: Buffer<Command, M>,
    rows: Buffer<VisibleRow, M>,
    text: SmallZ<M>,
    painting_rows: bool,
    retire_rows: bool,
    needs_capacity: u8,
    language: u8,
}
impl<M: Memory> Frame<M> {
    unsafe fn initialize(out: *mut Self) {
        // Initialize fields in their final storage; Buffer's NonNull is not zero-valid.
        unsafe {
            ptr::addr_of_mut!((*out).state).write(wifi::State::default());
            ptr::addr_of_mut!((*out).next).write(ListState::default());
            ptr::addr_of_mut!((*out).hits).write(wifi_view::Hits::default());
            ptr::addr_of_mut!((*out).mac).write([0; 64]);
            ptr::addr_of_mut!((*out).count_text).write([0; 64]);
            ptr::addr_of_mut!((*out).commands).write(Buffer::new());
            ptr::addr_of_mut!((*out).rows).write(Buffer::new());
            ptr::addr_of_mut!((*out).text).write(SmallZ::new());
            ptr::addr_of_mut!((*out).painting_rows).write(false);
            ptr::addr_of_mut!((*out).retire_rows).write(false);
            ptr::addr_of_mut!((*out).needs_capacity).write(0);
            ptr::addr_of_mut!((*out).language).write(0);
        }
    }
}
#[repr(C)]
pub struct Wifi<H: Host> {
    host: H,
    model: UnsafeCell<Model>,
    owned: UnsafeCell<ManuallyDrop<Owned<H::Memory>>>,
    frame: UnsafeCell<ManuallyDrop<Frame<H::Memory>>>,
}
impl<H: Host> Wifi<H> {
    /// Consume this Box into a StableOwner before retaining its raw target.
    pub fn try_new(host: H) -> Option<Box<Self>> {
        let out = H::allocate_owner(Layout::new::<Self>()).cast::<Self>();
        if out.is_null() {
            return None;
        }
        unsafe {
            ptr::addr_of_mut!((*out).host).write(host);
            // Model consists only of zero-valid scalar/POD fields.
            ptr::addr_of_mut!((*out).model).write_bytes(0, 1);
            let model = ptr::addr_of_mut!((*out).model).cast::<Model>();
            ptr::addr_of_mut!((*model).state.phase).write(Phase::Scanning as u8);
            ptr::addr_of_mut!((*out).owned)
                .cast::<Owned<H::Memory>>()
                .write(Owned::new());
            Frame::initialize(ptr::addr_of_mut!((*out).frame).cast::<Frame<H::Memory>>());
            Some(Box::from_raw(out))
        }
    }
    pub fn host(&self) -> &H {
        &self.host
    }
    fn physical(&self) -> &Physical {
        self.host.physical()
    }
    fn object(&self) -> *mut c_void {
        self.host.object()
    }
    fn owned(&self) -> *mut Owned<H::Memory> {
        self.owned.get().cast()
    }
    fn frame(&self) -> *mut Frame<H::Memory> {
        self.frame.get().cast()
    }
    fn model<R>(&self, f: impl FnOnce(&mut Model) -> R) -> R {
        self.host.model_lock();
        let _guard = ModelGuard(&self.host);
        f(unsafe { &mut *self.model.get() })
    }
    fn resource(&self) -> ResourceGuard<'_, H> {
        unsafe { (self.physical().resource_lock)(self.object(), 1) };
        ResourceGuard(&self.host)
    }
    fn render_guard(&self) -> RenderGuard<'_, H> {
        RenderGuard(&self.host, self.host.render_lock())
    }
    fn diagnostic(&self, event: Diagnostic, text: Bytes, bytes: u32) {
        unsafe { (self.physical().diagnostic)(event, text, bytes) };
    }
    fn allocation_error(&self, error: StorageError, scene: bool) {
        match error {
            StorageError::Allocation(bytes) => self.diagnostic(
                if scene {
                    Diagnostic::SceneOom
                } else {
                    Diagnostic::SnapshotOom
                },
                Bytes::EMPTY,
                bytes as u32,
            ),
            StorageError::Capacity => self.diagnostic(Diagnostic::InvalidScene, Bytes::EMPTY, 0),
        }
    }
    pub fn enter(&self) {
        self.host.enter();
        let auto = unsafe { (self.physical().allow_auto)(self.object()) };
        self.model(|m| {
            m.state.allow_auto = auto;
            m.ready = false;
            m.exited = false;
            m.retry = Retry::None;
        });
        self.run(0, 0, None);
    }
    pub fn exit(&self) {
        self.host.exit();
        {
            let mut call = effects::Call::new(self, 0, None);
            wifi::EffectHost::call(&mut call, wifi::Call::Exit, 0);
        }
        self.retire_rows();
        self.model(|m| {
            m.exited = true;
            m.ready = false;
        });
    }
    fn run(&self, event: u8, value: u32, input: Option<Input>) {
        let mut state = self.model(|m| m.state);
        let scanning = state.phase == Phase::Scanning as u8;
        let mut call = effects::Call::new(self, state.list.navigation_revision, input);
        if call.current() {
            if event == 4 {
                let automatic = state.automatic != 0;
                state.scan(&mut call, automatic);
                wifi::EffectHost::publish(&mut call, &mut state);
            } else {
                state.run(&mut call, event, value, input.unwrap_or_default());
            }
        }
        if call.failed() {
            self.model(|m| {
                if event == 0 {
                    m.retry = Retry::Entry;
                } else if event == 4 || scanning && call.scan_deleted() {
                    m.retry = Retry::Scan;
                }
            });
            self.reject();
            self.host.request_update();
        }
        let keyboard_failed = call.keyboard_failed();
        drop(call);
        if keyboard_failed {
            self.keyboard_returned(None);
        }
    }
    pub fn poll(&self) {
        if self.model(|m| m.exited) {
            return;
        }
        let retry = self.model(|m| core::mem::replace(&mut m.retry, Retry::None));
        if retry != Retry::None {
            let (input, _) = self.host.sample_input(false);
            if input.pressed & model::BACK != 0 {
                self.run(2, 0, Some(input));
            } else {
                self.run(if retry == Retry::Entry { 0 } else { 4 }, 0, None);
            }
            return;
        }
        let demand = {
            let _guard = self.resource();
            unsafe { (*self.frame()).needs_capacity != 0 }
        };
        if demand {
            let state = self.model(|m| m.state);
            if self.prepare(&state).is_err() {
                self.reject();
            }
            self.host.request_update();
        }
        let needs = self.model(|m| m.gate.needs_input());
        let blocked = if needs {
            let (input, _) = self.host.sample_input(false);
            let admission = self.model(|m| m.gate.input(&input));
            if admission.retry {
                self.host.request_update();
            }
            admission.consume
        } else {
            false
        };
        let list = self.model(|m| m.state.phase == Phase::List as u8);
        let (mut input, long) = self.host.sample_input(list);
        input.pressed &= 255;
        input.released &= 255;
        input.held &= 255;
        let routed = self.model(|m| {
            if blocked {
                input.pressed &= model::BACK;
                input.released &= model::BACK;
                input.held = 0;
                input.swipe = 0;
                input.touch_flags = 0;
            }
            if !m.ready || input.touch_flags == 0 {
                return false;
            }
            let routed = input.touch_flags & (model::TOUCH_PRESSED | model::TOUCH_RELEASED) != 0;
            if let Some(hit) = m.hits.hit(&m.state, input.touch_x, input.touch_y) {
                input.action = hit.action;
                input.value = if hit.action == 1
                    && input.touch_flags & model::TOUCH_PRESSED != 0
                    && input.touch_flags & model::TOUCH_RELEASED == 0
                {
                    hit.value - m.hits.rows.first_index
                } else {
                    hit.value
                };
            }
            routed
        });
        self.run(
            2,
            u32::from(!blocked && long) | (u32::from(routed) << 1),
            Some(input),
        );
    }
    pub fn keyboard_returned(&self, bytes: Option<&[u8]>) {
        let mut cancelled = bytes.is_none();
        let mut empty = false;
        if let Some(bytes) = bytes {
            let hidden = self.model(|m| m.keyboard);
            let _guard = self.resource();
            let owned = unsafe { &mut *self.owned() };
            let target = if hidden {
                &mut owned.selected
            } else {
                &mut owned.password
            };
            match target.try_assign(bytes) {
                Ok(()) => empty = bytes.is_empty(),
                Err(error) => {
                    self.allocation_error(error, false);
                    cancelled = true;
                }
            }
        }
        if cancelled || empty && self.model(|m| m.keyboard) {
            // Delivery has consumed the child. Commit cancellation even if list
            // storage is unavailable; the next loop can prepare its repaint.
            let state = self.model(|m| {
                m.state.keyboard_result(true, false);
                m.ready = false;
                m.state
            });
            if self.prepare(&state).is_err() {
                {
                    let _guard = self.resource();
                    unsafe { (*self.frame()).needs_capacity = 1 };
                }
                self.reject();
            }
            self.host.request_update();
            return;
        }
        self.run(3, 0, None);
    }
    fn reject(&self) {
        self.model(|m| {
            m.gate.reject();
            m.ready = false;
            m.state.list.active = -1;
            m.state.list.last_repeat = 0;
            m.state.list.pending_scroll = 0;
            m.state.list.navigation_revision = m.state.list.navigation_revision.wrapping_add(1);
            m.state.flash_ticks = 0;
            m.state.flash_index = -1;
        });
    }
    fn retire_rows(&self) {
        let _guard = self.resource();
        let frame = self.frame();
        unsafe {
            (*frame).retire_rows = true;
            if !(*frame).painting_rows {
                (*frame).rows = Buffer::new();
            }
        }
    }
    #[inline(never)]
    fn prepare(&self, state: &wifi::State) -> Result<(), Error> {
        let _render = self.render_guard();
        let _resource = self.resource();
        let layout = self.layout();
        let line = unsafe { (self.physical().line)(self.object(), layout.base.list.font) };
        let budget = wifi_view::budget(state, &layout, line)?;
        let frame = self.frame();
        // No rendering callback can run while frame storage is being replaced.
        unsafe {
            (*frame)
                .commands
                .try_reserve(usize::from(budget.commands))
                .map_err(|e| {
                    self.allocation_error(e, true);
                    Error::Capacity
                })?;
            while (*frame).commands.as_slice().len() < usize::from(budget.commands) {
                (*frame)
                    .commands
                    .try_push(Command::default())
                    .map_err(|_| Error::Capacity)?;
            }
            (*frame)
                .rows
                .try_reserve(usize::from(budget.rows))
                .map_err(|e| {
                    self.allocation_error(e, false);
                    Error::Capacity
                })?;
            while (*frame).rows.as_slice().len() < usize::from(budget.rows) {
                (*frame)
                    .rows
                    .try_push(VisibleRow::EMPTY)
                    .map_err(|_| Error::Capacity)?;
            }
            let owned = &*self.owned();
            let selected = owned
                .selected
                .len()
                .max(if owned.rows.as_slice().len() > 1 {
                    32
                } else {
                    0
                });
            let needed = selected.checked_add(17).ok_or(Error::Capacity)?.max(38);
            (*frame).text.try_reserve(needed).map_err(|e| {
                self.allocation_error(e, false);
                Error::Capacity
            })?;
            (*frame).needs_capacity = 0;
        }
        Ok(())
    }
    fn layout(&self) -> wifi_view::Layout {
        let mut out = core::mem::MaybeUninit::uninit();
        unsafe {
            (self.physical().layout)(self.object(), out.as_mut_ptr());
            out.assume_init()
        }
    }
    #[inline(never)]
    fn build(&self) -> Result<(usize, Refresh, bool), Error> {
        let f = self.frame();
        self.model(|m| {
            m.state.prepare_view();
            unsafe { (*f).state = m.state };
        });
        let state = unsafe { &(*f).state };
        let mut layout = self.layout();
        let owned = self.owned();
        let list = state.phase == Phase::List as u8;
        unsafe {
            if list && usize::from(state.count) != (*owned).rows.as_slice().len() {
                return Err(Error::Invalid);
            }
            (*f).language = layout.language;
            (*f).mac = (*owned).mac;
            layout.base.flags = (*owned)
                .rows
                .as_slice()
                .get(state.list.selected as usize)
                .is_some_and(Row::saved)
                .into();
            let line = (self.physical().line)(self.object(), layout.base.list.font);
            let budget = wifi_view::budget(state, &layout, line)?;
            let text_need = (*owned)
                .selected
                .len()
                .checked_add(17)
                .ok_or(Error::Capacity)?
                .max(38);
            if (*f).commands.as_slice().len() < usize::from(budget.commands)
                || (*f).rows.as_slice().len() < usize::from(budget.rows)
                || (*f).text.capacity() < text_need
            {
                (*f).needs_capacity = 1;
                return Err(Error::Capacity);
            }
            if budget.commands == 0 {
                return Ok((0, Refresh::None, list));
            }
            if list {
                (*f).retire_rows = false;
            }
            for row in (*f).rows.as_mut_slice() {
                row.index = u16::MAX;
            }
            view::count(&mut (*f).count_text, layout.language, state.real_count)?;
            let (count, refresh) = wifi_view::build(
                (*f).commands.as_mut_slice(),
                state,
                &layout,
                &mut view::View { owner: self },
                &mut (*f).next,
                &mut (*f).hits,
            )?;
            Ok((count, refresh, list))
        }
    }
    #[inline(never)]
    pub fn render(&self) -> bool {
        if self.model(|m| m.exited) {
            return false;
        }
        let mut guard = Some(self.resource());
        let Ok((count, refresh, list)) = self.build() else {
            self.reject();
            return false;
        };
        if count == 0 {
            return false;
        }
        let frame = self.frame();
        if list {
            unsafe { (*frame).painting_rows = true };
            drop(guard.take());
        }
        let scene = Scene {
            commands: unsafe { (*frame).commands.as_slice().get_unchecked(..count) },
            text: &[],
            interactions: &[],
            refresh: Refresh::None,
            generation: 1,
        };
        let painted = self.host.paint(&scene, &view::bindings(self));
        if painted {
            let current = self.model(|m| {
                let source = unsafe { &(*frame).state };
                let current = m.state.generation == source.generation
                    && m.state.phase == source.phase
                    && m.state.count == source.count;
                m.ready = m.gate.publish(current) == Publication::Current;
                if m.ready {
                    m.state.publish_view(source, unsafe { &(*frame).next });
                    m.hits = unsafe { (*frame).hits };
                }
                m.ready
            });
            if !current {
                self.host.request_update();
            }
        } else {
            self.diagnostic(Diagnostic::InvalidScene, Bytes::EMPTY, 0);
            self.reject();
        }
        if list {
            let _finish = self.resource();
            unsafe {
                (*frame).painting_rows = false;
                if (*frame).retire_rows {
                    (*frame).rows = Buffer::new();
                }
            }
        }
        drop(guard.take());
        if painted && refresh != Refresh::None {
            self.host.refresh(refresh);
        }
        painted
    }
}
impl<H: Host> Drop for Wifi<H> {
    fn drop(&mut self) {
        // App excluded all targets. End text/row loans, then SDK, then commands;
        // the host destroys the native wrapper after this body returns.
        unsafe {
            let frame = self.frame();
            ptr::drop_in_place(ptr::addr_of_mut!((*frame).rows));
            ptr::drop_in_place(ptr::addr_of_mut!((*frame).text));
            ManuallyDrop::drop(&mut *self.owned.get());
            (self.physical().release_resources)(self.object());
            ptr::drop_in_place(ptr::addr_of_mut!((*frame).commands));
        }
    }
}
fn copy_z<const N: usize>(out: &mut [u8; N], bytes: &[u8]) {
    let length = bytes
        .iter()
        .position(|b| *b == 0)
        .unwrap_or(bytes.len())
        .min(N - 1);
    out[..length].copy_from_slice(&bytes[..length]);
    out[length] = 0;
}
#[cfg(test)]
mod tests;

#[cfg(test)]
mod test_host;

#[cfg(target_pointer_width = "32")]
const _: () = {
    use super::wifi_resources::Global;
    assert!(core::mem::size_of::<Model>() == 188);
    assert!(core::mem::size_of::<Owned<Global>>() == 148);
    assert!(core::mem::size_of::<Frame<Global>>() == 396);
    assert!(core::mem::size_of::<VisibleRow>() == 46);
};
