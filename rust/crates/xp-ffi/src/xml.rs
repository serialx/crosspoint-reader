//! XML handles own all event storage; callers retain no Rust references.
extern crate alloc;

use alloc::alloc::{alloc, dealloc};
use alloc::vec::Vec;
use core::alloc::Layout;
use core::ffi::c_char;
use core::ptr;
use xp_core::xml::{Kind, Parser};

pub struct XpXml {
    parser: Parser,
    attributes: Vec<*const c_char>,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpXmlSpan {
    data: *const c_char,
    len: usize,
}

#[repr(C)]
pub struct XpXmlEvent {
    kind: u32,
    error: u32,
    byte: u64,
    line: u64,
    name: XpXmlSpan,
    value: XpXmlSpan,
    attributes: *const *const c_char,
    attribute_count: usize,
}

/// # Safety
/// out must be writable. The returned handle has one owner and is not thread safe.
#[no_mangle]
pub unsafe extern "C" fn xp_xml_create(out: *mut *mut XpXml) -> i32 {
    if out.is_null() {
        return -1;
    }
    out.write(ptr::null_mut());
    let handle = alloc(Layout::new::<XpXml>()).cast::<XpXml>();
    if handle.is_null() {
        return -2;
    }
    handle.write(XpXml {
        parser: Parser::new(),
        attributes: Vec::new(),
    });
    out.write(handle);
    0
}

/// # Safety
/// handle is live and exclusively borrowed. data has len readable bytes (null only
/// for len zero); consumed is writable. Drain next to NEED_INPUT before feeding.
#[no_mangle]
pub unsafe extern "C" fn xp_xml_feed(
    handle: *mut XpXml,
    data: *const u8,
    len: usize,
    final_input: u8,
    consumed: *mut usize,
) -> i32 {
    if handle.is_null()
        || consumed.is_null()
        || (data.is_null() && len != 0)
        || len > isize::MAX as usize
        || final_input > 1
    {
        return -1;
    }
    let bytes = if len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(data, len)
    };
    consumed.write((*handle).parser.feed(bytes, final_input != 0));
    0
}

/// # Safety
/// handle is live and exclusively borrowed; out is writable. All returned spans
/// expire at the next feed, next, stop or destroy call on this handle.
#[no_mangle]
pub unsafe extern "C" fn xp_xml_next(handle: *mut XpXml, out: *mut XpXmlEvent) -> i32 {
    if handle.is_null() || out.is_null() {
        return -1;
    }
    let handle = &mut *handle;
    handle.attributes.clear();
    let event = handle.parser.next();
    let empty = XpXmlSpan {
        data: ptr::null(),
        len: 0,
    };
    let mut result = XpXmlEvent {
        kind: event.kind as u32,
        error: event.error,
        byte: event.byte,
        line: event.line,
        name: empty,
        value: empty,
        attributes: ptr::null(),
        attribute_count: 0,
    };
    if matches!(
        event.kind,
        Kind::Start | Kind::End | Kind::Text | Kind::Comment | Kind::Pi | Kind::Entity
    ) {
        if let Some(field) = event.fields.first() {
            result.name = XpXmlSpan {
                data: event.data.as_ptr().add(field.range.start).cast(),
                len: field.range.len(),
            };
        }
    }
    if event.kind == Kind::Pi {
        if let Some(field) = event.fields.get(1) {
            result.value = XpXmlSpan {
                data: event.data.as_ptr().add(field.range.start).cast(),
                len: field.range.len(),
            };
        }
    }
    if event.kind == Kind::Start {
        if handle.attributes.try_reserve(event.fields.len()).is_err() {
            handle.parser.allocation_failed();
            return -2;
        }
        for field in event.fields.iter().skip(1) {
            handle
                .attributes
                .push(event.data.as_ptr().add(field.range.start).cast());
        }
        handle.attributes.push(ptr::null());
        result.attribute_count = (event.fields.len() - 1) / 2;
        result.attributes = handle.attributes.as_ptr();
    }
    out.write(result);
    0
}

/// # Safety
/// handle is null or a live exclusively borrowed handle.
#[no_mangle]
pub unsafe extern "C" fn xp_xml_stop(handle: *mut XpXml) {
    if !handle.is_null() {
        (*handle).parser.stop();
    }
}

/// # Safety
/// handle is null or the sole live owner returned by create; no borrowed event
/// spans or calls may survive destruction. Destroy exactly once.
#[no_mangle]
pub unsafe extern "C" fn xp_xml_destroy(handle: *mut XpXml) {
    if !handle.is_null() {
        ptr::drop_in_place(handle);
        dealloc(handle.cast(), Layout::new::<XpXml>());
    }
}
