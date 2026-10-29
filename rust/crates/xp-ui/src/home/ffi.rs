//! Private family boundary; the exported non-inlined entries live in xp-ffi.
use super::{
    view::{self, Cover, Layout, Lines, Prepared, Texts},
    HomeState, HomeTouch, HomeUpdate,
};
use crate::{
    ffi::{Buffer, SceneView},
    model::{Input, Interest},
    scene::{Arena, Error, Refresh, MAX_COMMAND_BYTES},
};
use core::ffi::c_void;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct TextServices {
    pub context: *mut c_void,
    pub width: Option<unsafe extern "C" fn(*mut c_void, u32, i32, u8) -> i32>,
    pub line: Option<unsafe extern "C" fn(*mut c_void, i32) -> i32>,
    pub empty: Option<unsafe extern "C" fn(*mut c_void, u32) -> u8>,
    pub wrap: Option<unsafe extern "C" fn(*mut c_void, u32, i32, i16, u8, u8) -> Lines>,
    pub truncate: Option<unsafe extern "C" fn(*mut c_void, u32, i32, i16, u8) -> u32>,
}
impl Texts for TextServices {
    fn width(&mut self, h: u32, f: i32, b: bool) -> Result<i16, Error> {
        let value = unsafe { self.width.ok_or(Error::Invalid)?(self.context, h, f, u8::from(b)) };
        i16::try_from(value)
            .ok()
            .filter(|v| *v >= 0)
            .ok_or(Error::Invalid)
    }
    fn line(&mut self, f: i32) -> Result<i16, Error> {
        let value = unsafe { self.line.ok_or(Error::Invalid)?(self.context, f) };
        i16::try_from(value)
            .ok()
            .filter(|v| (1..=4096).contains(v))
            .ok_or(Error::Invalid)
    }
    fn empty(&mut self, h: u32) -> bool {
        self.empty
            .is_none_or(|f| unsafe { f(self.context, h) != 0 })
    }
    fn wrap(&mut self, h: u32, f: i32, w: i16, n: u8, b: bool) -> Result<Lines, Error> {
        let value =
            unsafe { self.wrap.ok_or(Error::Invalid)?(self.context, h, f, w, n, u8::from(b)) };
        if value.count > n
            || value.max_width < 0
            || value.first.checked_add(u32::from(value.count)).is_none()
        {
            Err(Error::Invalid)
        } else {
            Ok(value)
        }
    }
    fn truncate(&mut self, h: u32, f: i32, w: i16, b: bool) -> Result<u32, Error> {
        let value =
            unsafe { self.truncate.ok_or(Error::Invalid)?(self.context, h, f, w, u8::from(b)) };
        if value == 0 {
            Err(Error::Invalid)
        } else {
            Ok(value)
        }
    }
}

/// # Safety
/// State is aligned and exclusively writable for this call.
#[inline(never)]
pub unsafe fn init(
    state: *mut HomeState,
    books: u8,
    grid: u8,
    opds: u8,
    plugins: u8,
    continuing: u8,
    initial: i32,
) {
    if let Some(s) = unsafe { state.as_mut() } {
        *s = HomeState::new(
            books.min(7),
            grid != 0,
            opds != 0,
            plugins != 0,
            continuing != 0,
        );
        s.initial_selection(initial);
    }
}
/// # Safety
/// Non-null state is aligned, live and immutable for the call.
#[inline(never)]
pub unsafe fn interest(state: *const HomeState, now: u32) -> Interest {
    unsafe { state.as_ref() }.map_or_else(Interest::default, |s| s.interest(now))
}
/// # Safety
/// Descriptors are aligned and disjoint; state is exclusively writable.
#[inline(never)]
pub unsafe fn update(
    state: *mut HomeState,
    input: *const Input,
    published: *const HomeTouch,
) -> HomeUpdate {
    let (Some(s), Some(i), Some(p)) = (
        unsafe { state.as_mut() },
        unsafe { input.as_ref() },
        unsafe { published.as_ref() },
    ) else {
        return HomeUpdate::default();
    };
    if s.books > 7 || !(0..s.count()).contains(&s.selected) {
        return HomeUpdate::default();
    }
    s.update(*i, p)
}
/// # Safety
/// All descriptors are aligned, live, disjoint and exclusively borrowed as
/// indicated by mutability. Callbacks lend immutable strings through painting,
/// do not reenter Rust, and do not change descriptors. Failure leaves outputs
/// unchanged; scratch commands may change. Only one render owner calls this.
#[allow(clippy::too_many_arguments)]
#[inline(never)]
pub unsafe fn standard_view(
    state: *const HomeState,
    layout: *const Layout,
    prepared: *const Prepared,
    texts: *const TextServices,
    buffer: *const Buffer,
    clean_initial: u8,
    touch: *mut HomeTouch,
    out: *mut SceneView,
) -> u8 {
    let (Some(s), Some(l), Some(p), Some(t), Some(b)) = (
        unsafe { state.as_ref() },
        unsafe { layout.as_ref() },
        unsafe { prepared.as_ref() },
        unsafe { texts.as_ref() },
        unsafe { buffer.as_ref() },
    ) else {
        return 0;
    };
    if touch.is_null()
        || out.is_null()
        || b.commands.is_null()
        || usize::from(b.command_capacity) > MAX_COMMAND_BYTES / 32
        || b.hit_capacity != 0
        || t.width.is_none()
        || t.line.is_none()
        || t.empty.is_none()
        || t.wrap.is_none()
        || t.truncate.is_none()
    {
        return 0;
    }
    let commands =
        unsafe { core::slice::from_raw_parts_mut(b.commands, usize::from(b.command_capacity)) };
    let Ok(mut arena) = Arena::new(commands, &mut [], &mut []) else {
        return 0;
    };
    let mut text_services = *t;
    let next = match view::standard(&mut arena, *s, l, p, &mut text_services) {
        Ok(n) => n,
        Err(Error::Capacity) => return 2,
        Err(_) => return 0,
    };
    let refresh = if clean_initial != 0 && s.first_render == 0 {
        Refresh::Half
    } else {
        Refresh::Fast
    };
    let Ok(scene) = arena.finish(refresh, 1) else {
        return 0;
    };
    unsafe {
        touch.write(next);
        out.write(SceneView {
            commands: b.commands,
            text: core::ptr::null(),
            interactions: core::ptr::null(),
            generation: 1,
            command_count: scene.commands.len() as u16,
            text_len: 0,
            interaction_count: 0,
            refresh: refresh as u8,
            reserved: 0,
        });
    }
    1
}

const _: () = {
    assert!(core::mem::size_of::<HomeState>() == 16);
    assert!(core::mem::size_of::<HomeTouch>() == 20);
    assert!(core::mem::size_of::<HomeUpdate>() == 12);
    assert!(core::mem::size_of::<Layout>() == 68);
    assert!(core::mem::size_of::<Cover>() == 12);
    assert!(core::mem::size_of::<Prepared>() == 44);
    assert!(core::mem::size_of::<Lines>() == 8);
};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GridTexts {
    pub context: *mut c_void,
    pub width: Option<unsafe extern "C" fn(*mut c_void, u32, i32) -> i32>,
    pub line: Option<unsafe extern "C" fn(*mut c_void, i32) -> i32>,
    pub height: Option<unsafe extern "C" fn(*mut c_void, u32, i32, i16, u8) -> i32>,
    pub empty: Option<unsafe extern "C" fn(*mut c_void, u32) -> u8>,
}
impl super::grid::Texts for GridTexts {
    fn width(&mut self, h: u32, f: i32) -> Result<i16, Error> {
        i16::try_from(unsafe { self.width.ok_or(Error::Invalid)?(self.context, h, f) })
            .ok()
            .filter(|v| *v >= 0)
            .ok_or(Error::Invalid)
    }
    fn line(&mut self, f: i32) -> Result<i16, Error> {
        i16::try_from(unsafe { self.line.ok_or(Error::Invalid)?(self.context, f) })
            .ok()
            .filter(|v| (1..=4096).contains(v))
            .ok_or(Error::Invalid)
    }
    fn height(&mut self, h: u32, f: i32, w: i16, n: u8) -> Result<i16, Error> {
        i16::try_from(unsafe { self.height.ok_or(Error::Invalid)?(self.context, h, f, w, n) })
            .ok()
            .filter(|v| *v >= 0)
            .ok_or(Error::Invalid)
    }
    fn empty(&mut self, h: u32) -> bool {
        self.empty
            .is_none_or(|f| unsafe { f(self.context, h) != 0 })
    }
}
/// # Safety
/// Descriptors are aligned, live and disjoint for the call; commands are exclusive.
/// Callbacks borrow immutable resource strings and never reenter the builder.
#[allow(clippy::too_many_arguments)]
#[inline(never)]
pub unsafe fn grid_view(
    state: *const HomeState,
    layout: *const super::grid::Layout,
    texts: *const GridTexts,
    buffer: *const Buffer,
    clean_initial: u8,
    geometry: *mut super::grid::Geometry,
    out: *mut SceneView,
) -> u8 {
    let (Some(s), Some(l), Some(t), Some(b)) = (
        unsafe { state.as_ref() },
        unsafe { layout.as_ref() },
        unsafe { texts.as_ref() },
        unsafe { buffer.as_ref() },
    ) else {
        return 0;
    };
    if geometry.is_null()
        || out.is_null()
        || b.commands.is_null()
        || usize::from(b.command_capacity) > MAX_COMMAND_BYTES / 32
        || b.hit_capacity != 0
        || t.width.is_none()
        || t.line.is_none()
        || t.height.is_none()
        || t.empty.is_none()
    {
        return 0;
    }
    let commands =
        unsafe { core::slice::from_raw_parts_mut(b.commands, usize::from(b.command_capacity)) };
    let Ok(mut arena) = Arena::new(commands, &mut [], &mut []) else {
        return 0;
    };
    if arena
        .push(crate::scene::Command {
            kind: crate::scene::Kind::Clear as u8,
            ..Default::default()
        })
        .is_err()
    {
        return 2;
    }
    let mut services = *t;
    let next = match super::grid::view(&mut arena, *s, l, &mut services) {
        Ok(n) => n,
        Err(Error::Capacity) => return 2,
        Err(_) => return 0,
    };
    if arena
        .push(crate::scene::Command {
            kind: crate::scene::Kind::Page as u8,
            resource: view::FOOTER,
            generation: 1,
            ..Default::default()
        })
        .is_err()
    {
        return 2;
    }
    let refresh = if clean_initial != 0 && s.first_render == 0 {
        Refresh::Half
    } else {
        Refresh::Fast
    };
    let Ok(scene) = arena.finish(refresh, 1) else {
        return 0;
    };
    unsafe {
        geometry.write(next);
        out.write(SceneView {
            commands: b.commands,
            text: core::ptr::null(),
            interactions: core::ptr::null(),
            generation: 1,
            command_count: scene.commands.len() as u16,
            text_len: 0,
            interaction_count: 0,
            refresh: refresh as u8,
            reserved: 0,
        });
    }
    1
}
