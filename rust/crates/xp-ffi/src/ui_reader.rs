//! Thin reader-family exports. Policy and wire layouts live in xp-ui.
use xp_ui::reader::{self, BuildWait, LinkNote, Progress, Session};

/// # Safety
/// Non-null pointers must be aligned, valid for the declared reader-family wire
/// value or buffer, and exclusively borrowed for mutations during this call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_session_note(
    state: *mut Session,
    forward: u8,
    succeeded: u8,
) {
    if let Some(state) = state.as_mut() {
        state.note(forward != 0, succeeded != 0);
    }
}
/// # Safety
/// Non-null pointers must be aligned, valid for the declared reader-family wire
/// value or buffer, and exclusively borrowed for mutations during this call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_session_render(
    state: *mut Session,
    now: u32,
    epoch: i64,
    progress: i32,
) {
    if let Some(state) = state.as_mut() {
        state.render(now, epoch, progress);
    }
}
/// # Safety
/// Non-null pointers must be aligned, valid for the declared reader-family wire
/// value or buffer, and exclusively borrowed for mutations during this call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_session_worthy(state: *const Session) -> u8 {
    u8::from(state.as_ref().is_some_and(Session::worthy))
}
/// # Safety
/// Non-null pointers must be aligned, valid for the declared reader-family wire
/// value or buffer, and exclusively borrowed for mutations during this call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_progress_encode(
    spine: i32,
    page: i32,
    count: i32,
    has_offset: u8,
    offset: u32,
    out: *mut u8,
) -> u32 {
    let Some(out) = out.cast::<[u8; 10]>().as_mut() else {
        return 0;
    };
    reader::progress_encode(spine, page, count, (has_offset != 0).then_some(offset), out)
}
/// # Safety
/// Non-null pointers must be aligned, valid for the declared reader-family wire
/// value or buffer, and exclusively borrowed for mutations during this call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_progress_decode(
    data: *const u8,
    len: u32,
    out: *mut Progress,
) -> u8 {
    if data.is_null() || out.is_null() || !matches!(len, 4 | 6 | 10) {
        return 0;
    }
    let Some((progress, stale)) =
        reader::progress_decode(core::slice::from_raw_parts(data, len as usize))
    else {
        return 0;
    };
    out.write(progress);
    if stale {
        2
    } else {
        1
    }
}
/// # Safety
/// Non-null pointers must be aligned, valid for the declared reader-family wire
/// value or buffer, and exclusively borrowed for mutations during this call.
#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_reader_remote_allowed(shown: u8, rendering: u8, input_over_page: u8) -> u8 {
    u8::from(reader::remote_allowed(
        shown != 0,
        rendering != 0,
        input_over_page != 0,
    ))
}
/// # Safety
/// Non-null pointers must be aligned, valid for the declared reader-family wire
/// value or buffer, and exclusively borrowed for mutations during this call.
#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_reader_build_allowed(running: u8, stopping: u8, starting: u8) -> u8 {
    u8::from(reader::build_allowed(
        running != 0,
        stopping != 0,
        starting != 0,
    ))
}
/// # Safety
/// Non-null pointers must be aligned, valid for the declared reader-family wire
/// value or buffer, and exclusively borrowed for mutations during this call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_wait_build(
    services: *const BuildWait,
    wait_ms: u32,
    step_ms: u32,
) -> u8 {
    let Some(services) = services.as_ref().copied() else {
        return 0;
    };
    u8::from(reader::wait_build(services, wait_ms, step_ms))
}
/// # Safety
/// Non-null pointers must be aligned, valid for the declared reader-family wire
/// value or buffer, and exclusively borrowed for mutations during this call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_refresh(remaining: *mut i32, frequency: i32) -> u8 {
    let Some(remaining) = remaining.as_mut() else {
        return 0;
    };
    u8::from(reader::refresh(remaining, frequency))
}
/// # Safety
/// Non-null pointers must be aligned, valid for the declared reader-family wire
/// value or buffer, and exclusively borrowed for mutations during this call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_link_draw(state: *mut LinkNote, shown: u8, note: u8) -> u8 {
    let Some(state) = state.as_mut() else {
        return 0;
    };
    state.draw(shown != 0, note)
}
/// # Safety
/// Non-null pointers must be aligned, valid for the declared reader-family wire
/// value or buffer, and exclusively borrowed for mutations during this call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_link_repaint(state: *mut LinkNote, note: u8) -> u8 {
    let Some(state) = state.as_mut() else {
        return 0;
    };
    u8::from(state.repaint(note))
}

#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_reader_turn(
    spine: i32,
    spine_count: i32,
    page: i32,
    page_count: u16,
    building: u8,
    forward: u8,
) -> reader::PageMove {
    reader::turn(
        spine,
        spine_count,
        page,
        page_count,
        building != 0,
        forward != 0,
    )
}
#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_reader_skip(spine: i32, page: i32, amount: i32) -> reader::PageMove {
    reader::skip(spine, page, amount)
}
/// # Safety
/// Services and byte ranges follow xp_ui_reader.h; written must be writable and
/// callbacks must neither retain bytes nor reenter this operation.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_write_progress(
    io: *const reader::ProgressIo,
    data: *const u8,
    len: u32,
    written: *mut u32,
) -> u8 {
    if written.is_null() {
        return reader::ProgressStatus::Invalid as u8;
    }
    written.write(0);
    let Some(io) = io.as_ref().copied() else {
        return reader::ProgressStatus::Invalid as u8;
    };
    let (status, count) = reader::write_progress(io, data, len);
    written.write(count);
    status as u8
}
/// # Safety
/// positions points to three readable records; out points to thirteen writable
/// bytes. The ranges must not overlap and are borrowed only for this call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_links_encode(
    positions: *const reader::LinkPosition,
    depth: u8,
    out: *mut u8,
) -> u32 {
    let (Some(positions), Some(out)) = (
        positions.cast::<[reader::LinkPosition; 3]>().as_ref(),
        out.cast::<[u8; 13]>().as_mut(),
    ) else {
        return 0;
    };
    reader::links_encode(positions, depth, out)
}
/// # Safety
/// data points to len readable bytes and positions to three writable records;
/// ranges must not overlap and remain borrowed for this call only.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_links_decode(
    data: *const u8,
    len: u32,
    spine_count: i32,
    positions: *mut reader::LinkPosition,
) -> u8 {
    if data.is_null() || len > 13 {
        return 0;
    }
    let Some(positions) = positions.cast::<[reader::LinkPosition; 3]>().as_mut() else {
        return 0;
    };
    reader::links_decode(
        core::slice::from_raw_parts(data, len as usize),
        spine_count,
        positions,
    )
}

use xp_ui::model::ListState;
use xp_ui::reader::input;
/// # Safety
/// Reader-family input callbacks and pointers obey xp_ui_reader.h and remain valid for the call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_buttons(
    services: *const input::Services,
    config: *const input::Config,
) -> input::Turn {
    let (Some(services), Some(config)) = (services.as_ref(), config.as_ref()) else {
        return input::Turn::default();
    };
    input::buttons(*services, *config)
}
/// # Safety
/// Reader-family input callbacks and pointers obey xp_ui_reader.h and remain valid for the call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_touch(
    services: *const input::Services,
    config: *const input::Config,
    width: i32,
    height: i32,
    rtl: u8,
) -> input::Turn {
    let (Some(services), Some(config)) = (services.as_ref(), config.as_ref()) else {
        return input::Turn::default();
    };
    input::touch(*services, *config, width, height, rtl != 0)
}
/// # Safety
/// Reader-family input callbacks and pointers obey xp_ui_reader.h and remain valid for the call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_menu_gesture(
    services: *const input::Services,
    gesture: u8,
    width: i32,
    height: i32,
) -> u8 {
    let Some(services) = services.as_ref() else {
        return 0;
    };
    u8::from(input::menu(*services, gesture, width, height))
}
/// # Safety
/// Reader-family input callbacks and pointers obey xp_ui_reader.h and remain valid for the call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_back(
    services: *const input::Services,
    short_browser: u8,
) -> u8 {
    let Some(services) = services.as_ref() else {
        return 0;
    };
    input::back(*services, short_browser != 0)
}
#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_reader_gesture(gesture: u8) -> u8 {
    input::gesture(gesture)
}
/// # Safety
/// tag points to length readable bytes; null is accepted only for length zero.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_language_rtl(tag: *const u8, length: u32) -> u8 {
    if tag.is_null() || length > isize::MAX as u32 {
        return 0;
    }
    u8::from(input::language_rtl(core::slice::from_raw_parts(
        tag,
        length as usize,
    )))
}
#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_reader_chapter_display(page: i32, total: i32) -> input::ChapterDisplay {
    input::chapter_display(page, total)
}
/// # Safety
/// Non-null state points to an exclusively borrowed valid ListState for this call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_toolbar_nav_reset(state: *mut ListState) {
    if let Some(state) = state.as_mut() {
        input::toolbar_reset(state)
    }
}
/// # Safety
/// Non-null state points to a readable valid ListState for this call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_toolbar_page_rows(state: *const ListState) -> i32 {
    state.as_ref().map_or(0, input::toolbar_page_rows)
}
/// # Safety
/// Non-null state points to an exclusively borrowed valid ListState for this call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_toolbar_scroll(
    state: *mut ListState,
    delta: i32,
    count: i32,
) -> u8 {
    state
        .as_mut()
        .map_or(0, |s| u8::from(input::toolbar_scroll(s, delta, count)))
}

/// # Safety
/// Non-null states are valid, aligned, disjoint, and exclusively borrowed for this call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_toolbar_nav_publish(
    state: *mut ListState,
    painted: *const ListState,
) {
    if state.is_null() || painted.is_null() {
        return;
    }
    debug_assert!((state as usize).abs_diff(painted as usize) >= core::mem::size_of::<ListState>());
    input::toolbar_publish(&mut *state, &*painted);
}

use xp_ui::reader::render;
/// # Safety
/// Services obey the reader-family synchronous callback contract.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_render_page(
    services: *const render::Services,
    strip_enabled: u8,
) {
    if let Some(services) = services.as_ref() {
        render::render(*services, strip_enabled != 0)
    }
}

use xp_ui::reader::base;
/// # Safety
/// Services obey the reader-family synchronous resource callback contract.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_base_run(
    services: *const base::Services,
    operation: u8,
    a: u8,
    b: u8,
) -> u8 {
    services
        .as_ref()
        .map_or(0, |s| base::run(*s, operation, a != 0, b != 0))
}
/// # Safety
/// Non-null model is valid and exclusively borrowed for this POD-only call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_base_init(model: *mut base::Model, fast: u8, frequency: i32) {
    if let Some(model) = model.as_mut() {
        *model = base::Model::new(fast != 0, frequency);
    }
}
/// # Safety
/// Non-null model is valid and readable for this POD-only call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_base_model_read(model: *const base::Model, field: u8) -> u32 {
    model.as_ref().map_or(0, |model| model.read(field))
}
/// # Safety
/// Non-null model is valid and exclusively borrowed for this POD-only call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_base_model_write(
    model: *mut base::Model,
    field: u8,
    value: u32,
) {
    if let Some(model) = model.as_mut() {
        model.write(field, value);
    }
}
/// # Safety
/// Non-null models are valid, disjoint and exclusively borrowed for this POD-only call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_base_begin_render(
    model: *mut base::Model,
    frame: *mut base::Model,
) {
    if model.is_null() || frame.is_null() {
        return;
    }
    debug_assert!((model as usize).abs_diff(frame as usize) >= core::mem::size_of::<base::Model>());
    frame.write((*model).begin_render());
}
/// # Safety
/// Non-null models are valid, disjoint and exclusively borrowed for this POD-only call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_base_publish(
    model: *mut base::Model,
    frame: *const base::Model,
) {
    if model.is_null() || frame.is_null() {
        return;
    }
    debug_assert!((model as usize).abs_diff(frame as usize) >= core::mem::size_of::<base::Model>());
    (*model).publish(&*frame);
}
/// # Safety
/// Non-null model is valid and exclusively borrowed for this POD-only call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_reader_base_force_refresh(model: *mut base::Model) {
    if let Some(model) = model.as_mut() {
        model.force_refresh();
    }
}
