use xp_ui::{
    ffi::{Buffer, SceneView},
    home,
    model::{Input, Interest},
};

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
/// State and facts are live aligned disjoint descriptors, borrowed for this call.
#[unsafe(no_mangle)]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_file_activate(
    state: *mut home::file::State,
    facts: *const home::file::Facts,
) -> xp_ui::model::Effect {
    let (Some(s), Some(f)) = (unsafe { state.as_ref() }, unsafe { facts.as_ref() }) else {
        return xp_ui::model::Effect::default();
    };
    s.activate_bound(*f)
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
