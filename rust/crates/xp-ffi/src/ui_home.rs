use xp_ui::home;
use xp_ui::{
    ffi::{Buffer, SceneView},
    home::{
        ffi::{self, TextServices},
        view::{Layout, Prepared},
        HomeState, HomeTouch, HomeUpdate,
    },
    model::{Input, Interest},
};

/// # Safety
/// Descriptors are aligned, valid and disjoint. Keys has capacity writable bytes.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_menu_keys(
    s: *const HomeState,
    continuing: u8,
    keys: *mut u8,
    capacity: u8,
) -> u8 {
    let Some(s) = (unsafe { s.as_ref() }) else {
        return 0;
    };
    let (values, n) = s.menu_keys(continuing != 0);
    if keys.is_null() || usize::from(capacity) < n {
        return 0;
    }
    unsafe {
        core::ptr::copy_nonoverlapping(values.as_ptr(), keys, n);
    }
    n as u8
}
/// # Safety
/// The state is aligned and exclusively writable.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_painted(
    s: *mut HomeState,
    changed: u8,
) -> xp_ui::model::Effect {
    unsafe { s.as_mut() }.map_or_else(xp_ui::model::Effect::default, |s| s.painted(changed != 0))
}
/// # Safety
/// The state is aligned and exclusively writable.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_loaded(s: *mut HomeState) {
    if let Some(s) = unsafe { s.as_mut() } {
        s.loaded();
    }
}
/// # Safety
/// Descriptors are aligned, valid and disjoint, under the existing render lock.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_prepare_widths(
    w: *mut xp_ui::home::view::Widths,
    s: *const HomeState,
    l: *const Layout,
    p: *mut Prepared,
) {
    if let (Some(w), Some(s), Some(l), Some(p)) = (
        unsafe { w.as_mut() },
        unsafe { s.as_ref() },
        unsafe { l.as_ref() },
        unsafe { p.as_mut() },
    ) {
        w.prepare(*s, *l, p);
    }
}
/// # Safety
/// The layout is aligned, live and immutable.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_thumb_height(l: *const Layout) -> i32 {
    unsafe { l.as_ref() }.map_or(0, |l| xp_ui::home::view::thumbnail_height(*l))
}

/// # Safety
/// Follows the private xp_ui_home.h borrowed descriptor contract.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_init(
    s: *mut HomeState,
    b: u8,
    g: u8,
    o: u8,
    p: u8,
    c: u8,
    i: i32,
) {
    unsafe { ffi::init(s, b, g, o, p, c, i) }
}
/// # Safety
/// Follows the private xp_ui_home.h borrowed descriptor contract.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_interest(s: *const HomeState, n: u32) -> Interest {
    unsafe { ffi::interest(s, n) }
}
/// # Safety
/// Follows the private xp_ui_home.h borrowed descriptor contract.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_update(
    s: *mut HomeState,
    i: *const Input,
    p: *const HomeTouch,
) -> HomeUpdate {
    unsafe { ffi::update(s, i, p) }
}
/// # Safety
/// Follows the private xp_ui_home.h borrowed descriptor contract.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_standard_view(
    s: *const HomeState,
    l: *const Layout,
    p: *const Prepared,
    t: *const TextServices,
    b: *const Buffer,
    c: u8,
    h: *mut HomeTouch,
    o: *mut SceneView,
) -> u8 {
    unsafe { ffi::standard_view(s, l, p, t, b, c, h, o) }
}

use xp_ui::{
    home::cache::{Span, State as Cache},
    scene::Rect,
};
/// # Safety
/// The cache descriptor is aligned and exclusively writable for the call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_cache_init(
    s: *mut Cache,
    region: u32,
    width: i32,
    height: i32,
) -> u32 {
    let Some(s) = (unsafe { s.as_mut() }) else {
        return 0;
    };
    *s = Cache::default();
    s.capacity = Cache::capacity(region, width, height);
    s.capacity
}
/// # Safety
/// The cache descriptor is aligned and exclusively writable for the call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_cache_allocated(s: *mut Cache, success: u8) {
    if let Some(s) = unsafe { s.as_mut() } {
        if success == 0 {
            s.capacity = 0;
        }
    }
}
/// # Safety
/// The cache descriptor is aligned and exclusively writable for the call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_cache_prepare(s: *mut Cache, orientation: i32) {
    if let Some(s) = unsafe { s.as_mut() } {
        s.prepare(orientation);
    }
}
/// # Safety
/// The cache descriptor is aligned and exclusively writable for the call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_cache_invalidate(s: *mut Cache) {
    if let Some(s) = unsafe { s.as_mut() } {
        s.invalidate();
    }
}
/// # Safety
/// The cache descriptor is aligned and exclusively writable for the call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_cache_invalidate_slot(s: *mut Cache, index: u8) {
    if let Some(s) = unsafe { s.as_mut() } {
        s.invalidate_slot(usize::from(index));
    }
}
/// # Safety
/// The cache descriptor is aligned and immutable for the call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_cache_lookup(s: *const Cache, index: u8, r: Rect) -> Span {
    unsafe { s.as_ref() }.map_or_else(Span::default, |s| s.lookup(usize::from(index), r))
}
/// # Safety
/// The cache descriptor is aligned and exclusively writable for the call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_cache_reserve(
    s: *mut Cache,
    index: u8,
    r: Rect,
    needed: u32,
) -> Span {
    unsafe { s.as_mut() }.map_or_else(Span::default, |s| s.reserve(usize::from(index), r, needed))
}
/// # Safety
/// The cache descriptor is aligned and exclusively writable for the call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_cache_copied(s: *mut Cache, index: u8, success: u8) {
    if let Some(s) = unsafe { s.as_mut() } {
        s.copied(usize::from(index), success != 0);
    }
}

/// # Safety
/// See the private family header for the borrowed descriptors and arena contract.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_grid_view(
    state: *const home::HomeState,
    layout: *const home::grid::Layout,
    texts: *const home::ffi::GridTexts,
    buffer: *const xp_ui::ffi::Buffer,
    clean_initial: u8,
    geometry: *mut home::grid::Geometry,
    out: *mut xp_ui::ffi::SceneView,
) -> u8 {
    unsafe { home::ffi::grid_view(state, layout, texts, buffer, clean_initial, geometry, out) }
}
/// # Safety
/// State is exclusively borrowed and input/geometry are aligned immutable descriptors.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_grid_update(
    state: *mut home::HomeState,
    input: *const xp_ui::model::Input,
    geometry: *const home::grid::Geometry,
) -> home::HomeUpdate {
    let (Some(s), Some(i)) = (unsafe { state.as_mut() }, unsafe { input.as_ref() }) else {
        return home::HomeUpdate::default();
    };
    s.update_grid(*i, unsafe { geometry.as_ref() })
}

/// # Safety
/// State is aligned and exclusively writable.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_file_init(state: *mut home::file::State, picker: u8) {
    if let Some(s) = unsafe { state.as_mut() } {
        *s = home::file::State::new(0, picker != 0);
    }
}
/// # Safety
/// State is a live aligned immutable descriptor.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_file_interest(
    state: *const home::file::State,
    now: u32,
) -> Interest {
    unsafe { state.as_ref() }.map_or_else(Interest::default, |s| s.interest(now))
}
/// # Safety
/// State is exclusive; input and facts are aligned disjoint borrowed descriptors.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_file_update(
    state: *mut home::file::State,
    input: *const Input,
    facts: *const home::file::Facts,
) -> home::file::Update {
    let (Some(s), Some(i), Some(f)) = (
        unsafe { state.as_mut() },
        unsafe { input.as_ref() },
        unsafe { facts.as_ref() },
    ) else {
        return home::file::Update::default();
    };
    s.update_bound(*i, *f)
}
/// # Safety
/// All descriptors follow the private family's resource and arena contract.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_file_view(
    state: *const home::file::State,
    facts: *const home::file::Facts,
    layout: *const home::file_view::Layout,
    rows: *const home::list_ffi::Rows,
    buffer: *const Buffer,
    next: *mut home::file_view::Feedback,
    hits: *mut home::hits::Layout,
    out: *mut SceneView,
) -> u8 {
    unsafe { home::list_ffi::file_view(state, facts, layout, rows, buffer, next, hits, out) }
}
/// # Safety
/// State is exclusively borrowed; next is immutable and disjoint.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_file_publish(
    state: *mut home::file::State,
    next: *const home::file_view::Feedback,
    scroll: i32,
) {
    if let (Some(s), Some(n)) = (unsafe { state.as_mut() }, unsafe { next.as_ref() }) {
        s.list.publish(&n.list, scroll);
    }
}
/// # Safety
/// State is aligned and exclusively writable.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_file_loaded(
    state: *mut home::file::State,
    reason: u8,
    selected: i32,
    count: u16,
) {
    if let Some(s) = unsafe { state.as_mut() } {
        s.loaded(reason, selected, count);
    }
}
/// # Safety
/// Geometry is from a completed paint; out is aligned and writable.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_list_hit(
    layout: *const home::hits::Layout,
    x: i16,
    y: i16,
    out: *mut xp_ui::scene::Interaction,
) -> u8 {
    let (Some(l), Some(out)) = (unsafe { layout.as_ref() }, unsafe { out.as_mut() }) else {
        return 0;
    };
    if let Some(hit) = l.hit(x, y) {
        *out = hit;
        1
    } else {
        0
    }
}

/// # Safety
/// State is aligned and exclusively writable.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_library_init(state: *mut home::library::State) {
    if let Some(s) = unsafe { state.as_mut() } {
        *s = home::library::State::default();
    }
}
/// # Safety
/// State is aligned and exclusively writable.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_library_event(
    state: *mut home::library::State,
    event: u8,
    value: i32,
    count: u16,
) {
    if let Some(s) = unsafe { state.as_mut() } {
        s.event(event, value, count);
    }
}
/// # Safety
/// State is an aligned immutable descriptor.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_library_flags(state: *const home::library::State, books: u16) -> u8 {
    unsafe { state.as_ref() }.map_or(0, |s| s.flags(books))
}
/// # Safety
/// State is an aligned immutable descriptor.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_library_entry(state: *const home::library::State) -> i32 {
    unsafe { state.as_ref() }.map_or(0, |s| s.entry())
}
/// # Safety
/// State is an aligned immutable descriptor.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_library_pinned(state: *const home::library::State, total: u8) -> u8 {
    unsafe { state.as_ref() }.map_or(0, |s| s.pinned(total))
}
/// # Safety
/// State is an aligned immutable descriptor.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_library_books(
    state: *const home::library::State,
    indexed: u16,
    filtered: u16,
    pins: u8,
    overlap: u8,
) -> u16 {
    unsafe { state.as_ref() }.map_or(0, |s| s.books(indexed, filtered, pins, overlap))
}
/// # Safety
/// Buffers hold the declared live u16 elements; state is immutable and aligned.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_library_row(
    state: *const home::library::State,
    entry: i32,
    pins: u8,
    filtered: *const u16,
    filtered_count: u16,
    overlap: *const u16,
    overlap_count: u8,
) -> i32 {
    let Some(s) = (unsafe { state.as_ref() }) else {
        return 0;
    };
    let f = if filtered.is_null() {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(filtered, usize::from(filtered_count)) }
    };
    let o = if overlap.is_null() {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(overlap, usize::from(overlap_count.min(10))) }
    };
    s.row_for(entry, pins, f, o)
}
/// # Safety
/// Starts contains count aligned immutable elements.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_library_group(book: i32, starts: *const u16, count: u16) -> i32 {
    if starts.is_null() {
        return 0;
    }
    home::library::group_for(book, unsafe {
        core::slice::from_raw_parts(starts, usize::from(count))
    })
}
/// # Safety
/// Disjoint aligned buffers contain capacity elements, no more than ten.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_library_overlap(
    ascending: *const u16,
    count: u8,
    total: u16,
    descending: u8,
    out: *mut u16,
) -> u8 {
    if ascending.is_null() || out.is_null() || count > 10 {
        return 0;
    }
    home::library::overlap(
        unsafe { core::slice::from_raw_parts(ascending, usize::from(count)) },
        total,
        descending != 0,
        unsafe { core::slice::from_raw_parts_mut(out, usize::from(count)) },
    ) as u8
}
/// # Safety
/// State is aligned and immutable.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_library_interest(
    state: *const home::library::State,
    now: u32,
) -> Interest {
    unsafe { state.as_ref() }.map_or_else(Interest::default, |s| s.interest(now))
}
/// # Safety
/// State is exclusive; input and facts are aligned immutable descriptors.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_library_update(
    state: *mut home::library::State,
    input: *const Input,
    facts: *const home::library::Facts,
) -> home::library::Update {
    let (Some(s), Some(i), Some(f)) = (
        unsafe { state.as_mut() },
        unsafe { input.as_ref() },
        unsafe { facts.as_ref() },
    ) else {
        return home::library::Update::default();
    };
    s.update_bound(*i, *f)
}
/// # Safety
/// Borrowed descriptors follow the private family resource and arena contract.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_library_view(
    state: *const home::library::State,
    facts: *const home::library::Facts,
    layout: *const home::library_view::Layout,
    rows: *const home::list_ffi::Rows,
    buffer: *const Buffer,
    next: *mut home::library_view::Feedback,
    hits: *mut home::hits::Layout,
    out: *mut SceneView,
) -> u8 {
    unsafe { home::list_ffi::library_view(state, facts, layout, rows, buffer, next, hits, out) }
}
/// # Safety
/// State is exclusive, feedback is an immutable disjoint descriptor.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_library_publish(
    state: *mut home::library::State,
    next: *const home::library_view::Feedback,
    tab: u8,
    scroll: i32,
) {
    if let (Some(s), Some(n)) = (unsafe { state.as_mut() }, unsafe { next.as_ref() }) {
        if tab < 3 {
            s.lists[usize::from(tab)].publish(&n.list, scroll);
        }
    }
}

/// # Safety
/// Hits is exclusive; controls is a disjoint aligned array containing count entries.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_home_list_header(
    hits: *mut home::hits::Layout,
    controls: *const xp_ui::reader_chrome::hits::Control,
    count: u8,
) -> u8 {
    if count > 3 || controls.is_null() {
        return 0;
    }
    let Some(h) = (unsafe { hits.as_mut() }) else {
        return 0;
    };
    u8::from(
        h.header(unsafe { core::slice::from_raw_parts(controls, usize::from(count)) })
            .is_ok(),
    )
}
