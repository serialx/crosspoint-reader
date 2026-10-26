//! One fallibly allocated chapter/XML owner. Event buffers never cross C.
extern crate alloc;
use crate::css::Css;
use alloc::alloc::{alloc, dealloc};
use core::{alloc::Layout, ffi::c_void, ptr};
use xp_core::{
    chapter::{
        self,
        feedback::{Notify, Query},
        services::Services,
        style::{Block, DropCap},
        Measure, State,
    },
    xml::{Kind, Parser},
};

pub struct Chapter {
    state: State,
    xml: Parser,
}
#[repr(C)]
pub struct Options {
    alignment: u8,
    embedded: u8,
    images: u8,
    css_enabled: u8,
}

/// # Safety
/// out is writable; services/context and css (if non-null) outlive the handle.
/// All callbacks are populated and synchronous. No callback may destroy/feed this
/// handle; only feedback on its supplied state reborrow is allowed during a call.
#[no_mangle]
pub unsafe extern "C" fn xp_chapter_create(
    out: *mut *mut Chapter,
    services: *const Services,
    ctx: *mut c_void,
    css: *const Css,
    options: *const Options,
) -> i32 {
    if out.is_null() || services.is_null() || options.is_null() {
        return -1;
    }
    out.write(ptr::null_mut());
    let options = &*options;
    if options.alignment > 4
        || options.embedded > 1
        || options.images > 2
        || options.css_enabled > 1
    {
        return -1;
    }
    let handle = alloc(Layout::new::<Chapter>()).cast::<Chapter>();
    if handle.is_null() {
        return -2;
    }
    let store = if css.is_null() {
        ptr::null()
    } else {
        ptr::addr_of!((*css).store)
    };
    State::initialize(
        ptr::addr_of_mut!((*handle).state),
        services,
        ctx,
        store,
        options.alignment,
        options.embedded != 0,
        options.images,
        options.css_enabled != 0,
    );
    ptr::addr_of_mut!((*handle).xml).write(Parser::new());
    out.write(handle);
    0
}
/// # Safety
/// Sole live owner, with no active callback; null is allowed.
#[no_mangle]
pub unsafe extern "C" fn xp_chapter_destroy(handle: *mut Chapter) {
    if !handle.is_null() {
        ptr::drop_in_place(handle);
        dealloc(handle.cast(), Layout::new::<Chapter>());
    }
}
/// # Safety
/// Live handle. Returned pointer is borrowed until destruction; callbacks must
/// use the fresh state pointer passed by the service, not this outer borrow.
#[no_mangle]
pub unsafe extern "C" fn xp_chapter_state(handle: *mut Chapter) -> *mut State {
    if handle.is_null() {
        ptr::null_mut()
    } else {
        ptr::addr_of_mut!((*handle).state)
    }
}
/// # Safety
/// Live exclusive handle; input is readable for len bytes and len <= 1024.
/// Returns More=0, Done=1, Error=2; callbacks cannot feed/destroy the handle.
#[no_mangle]
pub unsafe extern "C" fn xp_chapter_feed(
    handle: *mut Chapter,
    data: *const u8,
    len: u32,
    final_input: u8,
) -> u8 {
    if handle.is_null() || (data.is_null() && len != 0) || len > 1024 || final_input > 1 {
        return 2;
    }
    let chapter = &mut *handle;
    if chapter.state.stopped || chapter.state.oom {
        return 2;
    }
    let input = if len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(data, len as usize)
    };
    if chapter.xml.feed(input, final_input != 0) != len as usize {
        return 2;
    }
    loop {
        let event = chapter.xml.next();
        chapter.state.byte = event.byte;
        let text = event
            .fields
            .first()
            .and_then(|f| event.data.get(f.range.clone()))
            .unwrap_or(&[]);
        match event.kind {
            Kind::Start => chapter.state.start(&event),
            Kind::End => chapter.state.end(text),
            Kind::Text => chapter.state.characters(text),
            Kind::Entity => {
                if let Some(value) = chapter::entities::lookup(text) {
                    chapter.state.characters(value);
                } else {
                    let Some(length) = text.len().checked_add(2) else {
                        return 2;
                    };
                    let Ok(length32) = u32::try_from(length) else {
                        return 2;
                    };
                    let storage = chapter.state.entity_buffer(length32);
                    if storage.is_null() {
                        chapter.state.oom = true;
                        return 2;
                    }
                    let buffer = core::slice::from_raw_parts_mut(storage, length);
                    buffer[0] = b'&';
                    buffer[1..length - 1].copy_from_slice(text);
                    buffer[length - 1] = b';';
                    chapter.state.characters(buffer);
                }
            }
            Kind::Comment | Kind::Pi | Kind::CdataStart | Kind::CdataEnd => {}
            Kind::Done => return 1,
            Kind::NeedInput => return u8::from(final_input != 0),
            Kind::Error => return if chapter.state.ended { 1 } else { 2 },
            Kind::Stopped => return 2,
        }
        if chapter.state.stopped {
            chapter.xml.stop();
            return 2;
        }
        if chapter.state.oom {
            return 2;
        }
    }
}
/// # Safety
/// state is the current synchronous callback reborrow or an idle handle's state.
#[no_mangle]
pub unsafe extern "C" fn xp_chapter_query(state: *const State, query: u32) -> u32 {
    if state.is_null() || query > Query::TableBorder as u32 {
        return 0;
    }
    (*state).query(core::mem::transmute::<u32, Query>(query))
}
/// # Safety
/// Same state lifetime as query; notification must be a declared value.
/// A reverse FlushWord may synchronously nest assembly callbacks. Other semantic
/// storage is not borrowed across such a call by the assembler.
#[no_mangle]
pub unsafe extern "C" fn xp_chapter_notify(state: *mut State, notification: u32) -> u32 {
    if state.is_null() || notification > Notify::BeginLists as u32 {
        return 0;
    }
    (*state).notify(core::mem::transmute::<u32, Notify>(notification))
}
/// # Safety
/// state is live; out is writable and disjoint from state.
#[no_mangle]
pub unsafe extern "C" fn xp_chapter_block(state: *const State, out: *mut Block) {
    if !state.is_null() && !out.is_null() {
        out.write((*state).parent_block());
    }
}
/// # Safety
/// state is live; out is writable and disjoint from state.
#[no_mangle]
pub unsafe extern "C" fn xp_chapter_drop_cap(state: *const State, out: *mut DropCap) {
    if !state.is_null() && !out.is_null() {
        out.write((*state).drop_cap());
    }
}
/// # Safety
/// state is live during the callback.
#[no_mangle]
pub unsafe extern "C" fn xp_chapter_byte(state: *const State) -> u64 {
    if state.is_null() {
        0
    } else {
        (*state).byte
    }
}
/// # Safety
/// state is the current reborrow, read writes at most capacity synchronously,
/// context remains live until return, and out is writable/disjoint from state.
#[no_mangle]
pub unsafe extern "C" fn xp_chapter_measure_table(
    state: *mut State,
    ctx: *mut c_void,
    read: chapter::table::Read,
    out: *mut Measure,
) -> u8 {
    if state.is_null() || out.is_null() {
        return 0;
    }
    match (*state).measure_table(ctx, read) {
        Some(value) => {
            out.write(value);
            1
        }
        None => 0,
    }
}
/// # Safety
/// state is the current reborrow; measure is readable for the call.
#[no_mangle]
pub unsafe extern "C" fn xp_chapter_plan_columns(state: *mut State, measure: *const Measure) {
    if !state.is_null() && !measure.is_null() {
        (*state).plan_columns(*measure);
    }
}
#[no_mangle]
pub extern "C" fn xp_chapter_handle_size() -> u32 {
    core::mem::size_of::<Chapter>() as u32
}

// These symbols are absent from firmware builds.
#[cfg(feature = "chapter-test")]
mod test_access {
    use super::*;
    use alloc::vec::Vec;
    use xp_core::{
        chapter::services::Span,
        xml::{Event, Field},
    };
    /// # Safety
    /// Live idle state and borrowed CSS/options valid for this call.
    #[no_mangle]
    pub unsafe extern "C" fn xp_chapter_test_options(
        state: *mut State,
        css: *const Css,
        options: *const Options,
    ) {
        if state.is_null() || options.is_null() {
            return;
        }
        let value = &*options;
        let store = if css.is_null() {
            ptr::null()
        } else {
            ptr::addr_of!((*css).store)
        };
        (*state).test_options(
            store,
            value.alignment,
            value.embedded != 0,
            value.images,
            value.css_enabled != 0,
        );
    }
    /// # Safety
    /// Live idle state; every span is readable for its length. Start fields are
    /// name followed by alternating attribute names and values.
    #[no_mangle]
    pub unsafe extern "C" fn xp_chapter_test_event(
        state: *mut State,
        kind: u32,
        spans: *const Span,
        count: u32,
    ) {
        if state.is_null() || spans.is_null() || count == 0 {
            return;
        }
        let spans = core::slice::from_raw_parts(spans, count as usize);
        let mut data = Vec::new();
        let mut fields = Vec::new();
        for span in spans {
            if span.data.is_null() && span.len != 0 {
                return;
            }
            if data.try_reserve(span.len as usize + 1).is_err() || fields.try_reserve(1).is_err() {
                (*state).oom = true;
                return;
            }
            let start = data.len();
            if span.len != 0 {
                data.extend_from_slice(core::slice::from_raw_parts(span.data, span.len as usize));
            }
            fields.push(Field {
                range: start..data.len(),
            });
            data.push(0);
        }
        let event = Event {
            kind: Kind::Start,
            data: &data,
            fields: &fields,
            byte: 0,
            line: 0,
            error: 0,
        };
        match kind {
            0 => (*state).start(&event),
            1 => (*state).characters(&data[fields[0].range.clone()]),
            2 => (*state).end(&data[fields[0].range.clone()]),
            _ => {}
        }
    }
    /// # Safety
    /// State is live and idle.
    #[no_mangle]
    pub unsafe extern "C" fn xp_chapter_test_query(state: *const State, field: u32) -> u32 {
        if state.is_null() {
            0
        } else {
            (*state).test_query(field)
        }
    }
    /// # Safety
    /// State is live and idle.
    #[no_mangle]
    pub unsafe extern "C" fn xp_chapter_test_columns(state: *mut State, first: u16, second: u16) {
        if !state.is_null() {
            (*state).test_columns(first, second);
        }
    }
}
