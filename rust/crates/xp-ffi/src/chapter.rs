//! One fallibly allocated chapter/XML owner. Event buffers never cross C.
extern crate alloc;
use crate::css::Css;
use alloc::alloc::{alloc, dealloc};
use core::{alloc::Layout, ffi::c_void, ptr, ptr::NonNull};
use xp_core::{
    chapter::{
        self,
        assembly::{
            self,
            runtime::{Callbacks, Storage},
        },
        feedback::Query,
        State,
    },
    xml::{Kind, Parser},
};

pub struct Chapter {
    state: State,
    // Payloads retain this address across State calls; borrow fields individually.
    storage: Storage,
    xml: Parser,
}
#[repr(C)]
pub struct Options {
    pub(crate) alignment: u8,
    pub(crate) embedded: u8,
    pub(crate) images: u8,
    pub(crate) css_enabled: u8,
    pub(crate) assembly: assembly::Options,
}

impl Chapter {
    fn allocate(options: &Options) -> Result<NonNull<Self>, i32> {
        if options.alignment > 4
            || options.embedded > 1
            || options.images > 2
            || options.css_enabled > 1
        {
            return Err(-1);
        }
        let handle = unsafe { alloc(Layout::new::<Chapter>()) }.cast::<Chapter>();
        if handle.is_null() {
            return Err(-2);
        }
        unsafe { ptr::addr_of_mut!((*handle).storage).write(Storage::default()) };
        Ok(NonNull::new(handle).unwrap())
    }

    pub(crate) unsafe fn begin(handle: NonNull<Self>) -> bool {
        unsafe { (*handle.as_ptr()).state.begin_assembly() };
        !unsafe { (*handle.as_ptr()).state.oom }
    }

    pub(crate) unsafe fn finish(handle: NonNull<Self>) -> bool {
        unsafe { (*handle.as_ptr()).state.finish_assembly() }
    }

    #[cfg(feature = "app-test")]
    pub(crate) unsafe fn oom(handle: NonNull<Self>) -> bool {
        unsafe { (*handle.as_ptr()).state.query(Query::Oom) != 0 }
    }

    pub(crate) unsafe fn destroy(handle: NonNull<Self>) {
        unsafe {
            let raw = handle.as_ptr();
            ptr::drop_in_place(ptr::addr_of_mut!((*raw).state));
            ptr::drop_in_place(ptr::addr_of_mut!((*raw).storage));
            ptr::drop_in_place(ptr::addr_of_mut!((*raw).xml));
            dealloc(handle.as_ptr().cast(), Layout::new::<Self>());
        }
    }

    #[cfg(feature = "app-test")]
    pub(crate) unsafe fn create_reader(
        services: NonNull<dyn chapter::ReaderServices>,
        css: *const Css,
        options: &Options,
    ) -> Result<NonNull<Self>, i32> {
        let handle = Self::allocate(options)?;
        let raw = handle.as_ptr();
        let store = if css.is_null() {
            ptr::null()
        } else {
            unsafe { ptr::addr_of!((*css).store) }
        };
        unsafe {
            State::initialize_reader(
                ptr::addr_of_mut!((*raw).state),
                ptr::addr_of_mut!((*raw).storage),
                services,
                store,
                options.alignment,
                options.embedded != 0,
                options.images,
                options.css_enabled != 0,
                options.assembly,
            );
            ptr::addr_of_mut!((*raw).xml).write(Parser::new());
        }
        Ok(handle)
    }
}

/// # Safety
/// out is writable; callbacks/context and css (if non-null) outlive the handle.
/// All callbacks are populated and synchronous. No callback may destroy/feed this
/// handle. Only table_file may pass its state reborrow to the supplied Rust visitor.
#[no_mangle]
pub unsafe extern "C" fn xp_chapter_create(
    out: *mut *mut Chapter,
    callbacks: *const Callbacks,
    ctx: *mut c_void,
    css: *const Css,
    options: *const Options,
) -> i32 {
    if out.is_null() || callbacks.is_null() || options.is_null() {
        return -1;
    }
    out.write(ptr::null_mut());
    let options = &*options;
    let handle = match Chapter::allocate(options) {
        Ok(handle) => handle.as_ptr(),
        Err(error) => return error,
    };
    let store = if css.is_null() {
        ptr::null()
    } else {
        ptr::addr_of!((*css).store)
    };
    State::initialize(
        ptr::addr_of_mut!((*handle).state),
        ptr::addr_of_mut!((*handle).storage),
        callbacks,
        ctx,
        store,
        options.alignment,
        options.embedded != 0,
        options.images,
        options.css_enabled != 0,
        options.assembly,
    );
    ptr::addr_of_mut!((*handle).xml).write(Parser::new());
    out.write(handle);
    0
}
/// # Safety
/// Live idle handle with a valid synchronous sink.
#[no_mangle]
pub unsafe extern "C" fn xp_chapter_begin(handle: *mut Chapter) -> u8 {
    NonNull::new(handle).is_some_and(|handle| unsafe { Chapter::begin(handle) }) as u8
}
/// # Safety
/// Live idle handle; completion may request stop but cannot destroy the handle.
#[no_mangle]
pub unsafe extern "C" fn xp_chapter_finish(handle: *mut Chapter) -> u8 {
    NonNull::new(handle).is_some_and(|handle| unsafe { Chapter::finish(handle) }) as u8
}
/// # Safety
/// Sole live owner, with no active callback; null is allowed.
#[no_mangle]
pub unsafe extern "C" fn xp_chapter_destroy(handle: *mut Chapter) {
    if let Some(handle) = NonNull::new(handle) {
        unsafe { Chapter::destroy(handle) };
    }
}
/// # Safety
/// Live handle. Returned pointer is borrowed until destruction; callbacks must
/// use the fresh state pointer passed by table measurement, not this outer borrow.
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
    feed_diagnostic(handle, data, len, final_input) as u8
}
/// # Safety
/// Same contract as xp_chapter_feed. Returns status in bits 0..7,
/// Expat error in bits 8..31, and one-based line in bits 32..63.
pub(crate) unsafe fn feed_diagnostic(
    handle: *mut Chapter,
    data: *const u8,
    len: u32,
    final_input: u8,
) -> u64 {
    if handle.is_null() || (data.is_null() && len != 0) || len > 1024 || final_input > 1 {
        return 2;
    }
    let state = &mut (*handle).state;
    let xml = &mut (*handle).xml;
    if state.stopped || state.oom {
        return 2;
    }
    let input = if len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(data, len as usize)
    };
    if xml.feed(input, final_input != 0) != len as usize {
        return 2;
    }
    loop {
        let event = xml.next();
        state.byte = event.byte;
        let text = event
            .fields
            .first()
            .and_then(|f| event.data.get(f.range.clone()))
            .unwrap_or(&[]);
        match event.kind {
            Kind::Start => state.start(&event),
            Kind::End => state.end(text),
            Kind::Text => state.characters(text),
            Kind::Entity => {
                let Some(length) = text.len().checked_add(2) else {
                    return 2;
                };
                let Ok(length32) = u32::try_from(length) else {
                    return 2;
                };
                let storage = state.entity_buffer(length32);
                if storage.is_null() {
                    state.oom = true;
                    return 2;
                }
                let buffer = core::slice::from_raw_parts_mut(storage, length);
                buffer[0] = b'&';
                buffer[1..length - 1].copy_from_slice(text);
                buffer[length - 1] = b';';
                let value = state.entity_lookup(chapter::services::Span::new(buffer));
                if value.data.is_null() {
                    state.characters(buffer);
                } else {
                    state.characters(core::slice::from_raw_parts(value.data, value.len as usize));
                }
            }
            Kind::Comment | Kind::Pi | Kind::CdataStart | Kind::CdataEnd => {}
            Kind::Done => return 1,
            Kind::NeedInput => return u64::from(final_input != 0),
            Kind::Error => {
                return (u64::from(event.line as u32) << 32)
                    | (u64::from(event.error) << 8)
                    | if state.ended { 1 } else { 2 };
            }
            Kind::Stopped => return 2,
        }
        if state.stopped {
            xml.stop();
            return 2;
        }
        if state.oom {
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
/// state is the current reborrow, read writes at most capacity synchronously,
/// context remains live until return, and out is writable/disjoint from state.
#[cfg(feature = "chapter-test")]
#[no_mangle]
pub unsafe extern "C" fn xp_chapter_measure_table(
    state: *mut State,
    ctx: *mut c_void,
    read: chapter::table::Read,
    out: *mut chapter::Measure,
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
#[cfg(feature = "chapter-test")]
#[no_mangle]
pub unsafe extern "C" fn xp_chapter_plan_columns(
    state: *mut State,
    measure: *const chapter::Measure,
) {
    if !state.is_null() && !measure.is_null() {
        (*state).plan_columns(*measure);
    }
}

// These symbols are absent from firmware builds.
#[cfg(feature = "chapter-test")]
mod test_access {
    #[no_mangle]
    pub extern "C" fn xp_chapter_test_storage_retry(reclaim: unsafe extern "C" fn()) -> u8 {
        let mut storage = xp_core::chapter::assembly::runtime::Storage::default();
        u8::from(storage.test_image(reclaim))
    }

    use super::*;
    use alloc::vec::Vec;
    use xp_core::{
        chapter::services::Span,
        xml::{Event, Field},
    };
    /// # Safety
    /// Live idle state; the returned paragraph is borrowed until its next mutation.
    #[no_mangle]
    pub unsafe extern "C" fn xp_chapter_test_paragraph(
        state: *mut State,
    ) -> *mut xp_core::para::paragraph::Paragraph {
        (*state).test_paragraph()
    }
    /// # Safety
    /// Live idle state. Operations are host-only fixture access.
    #[no_mangle]
    pub unsafe extern "C" fn xp_chapter_test_assembly(
        state: *mut State,
        operation: u32,
        value: u32,
    ) -> u32 {
        (*state).test_assembly(operation, value)
    }
    /// # Safety
    /// Live idle state.
    #[no_mangle]
    pub unsafe extern "C" fn xp_chapter_test_carry(
        state: *mut State,
        carry: u32,
        lines: u32,
        offset: u32,
    ) {
        (*state).test_carry(carry, lines, offset);
    }
    /// # Safety
    /// Live idle state, writable disjoint output.
    #[no_mangle]
    pub unsafe extern "C" fn xp_chapter_test_pending_note(
        state: *const State,
        index: u32,
        out: *mut chapter::style::Footnote,
    ) {
        out.write((*state).assembly.test_pending_note(index as usize));
    }
    /// # Safety
    /// Live idle state and readable note.
    #[no_mangle]
    pub unsafe extern "C" fn xp_chapter_test_note(
        state: *mut State,
        note: *const chapter::style::Footnote,
    ) {
        (*state).test_note(*note);
    }
    /// # Safety
    /// Live idle state and readable link.
    #[no_mangle]
    pub unsafe extern "C" fn xp_chapter_test_link(state: *mut State, link: *const assembly::Link) {
        let link = &*link;
        (*state).test_link(assembly::Link {
            href: link.href,
            x: link.x,
            y: link.y,
            width: link.width,
            height: link.height,
        });
    }
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
        (*state).assembly.options = value.assembly;
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
