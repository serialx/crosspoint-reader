//! Package handles own their parser and all borrowed result storage.
extern crate alloc;
use alloc::alloc::{alloc, dealloc};
use core::{alloc::Layout, ptr};
use xp_core::epub::{opf, Parser, ParserKind};

#[repr(C)]
pub struct XpPkg {
    opf: bool,
}
#[repr(C)]
struct SimpleHandle {
    tag: XpPkg,
    parser: Parser,
}
#[repr(C)]
struct OpfHandle {
    tag: XpPkg,
    parser: opf::Parser,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpPkgSpan {
    data: *const u8,
    len: usize,
}
#[repr(C)]
pub struct XpPkgRecord {
    kind: u32,
    error: u32,
    value: XpPkgSpan,
    href: XpPkgSpan,
    anchor: XpPkgSpan,
    level: u8,
    field: u32,
    number: f32,
    number_present: u8,
}
unsafe fn create(kind: ParserKind, base: *const u8, len: usize, out: *mut *mut XpPkg) -> i32 {
    if out.is_null() || (base.is_null() && len != 0) || len > isize::MAX as usize {
        return -1;
    }
    out.write(ptr::null_mut());
    let bytes = if len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(base, len)
    };
    let handle = alloc(Layout::new::<SimpleHandle>()).cast::<SimpleHandle>();
    if handle.is_null() {
        return -2;
    }
    handle.write(SimpleHandle {
        tag: XpPkg { opf: false },
        parser: Parser::new(kind),
    });
    if (*handle).parser.set_base(bytes).is_err() {
        xp_pkg_destroy(handle.cast());
        return -2;
    }
    out.write(handle.cast());
    0
}
/// # Safety
/// out is writable and becomes the sole owner; base has len readable bytes.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_container_create(
    base: *const u8,
    len: usize,
    out: *mut *mut XpPkg,
) -> i32 {
    create(ParserKind::Container, base, len, out)
}
/// # Safety
/// out is writable and becomes the sole owner; base has len readable bytes.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_ncx_create(
    base: *const u8,
    len: usize,
    out: *mut *mut XpPkg,
) -> i32 {
    create(ParserKind::Ncx, base, len, out)
}
/// # Safety
/// out is writable and becomes the sole owner; base has len readable bytes.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_nav_create(
    base: *const u8,
    len: usize,
    out: *mut *mut XpPkg,
) -> i32 {
    create(ParserKind::Nav, base, len, out)
}
/// # Safety
/// out is writable and becomes the sole owner; base has len readable bytes.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_encryption_create(
    base: *const u8,
    len: usize,
    out: *mut *mut XpPkg,
) -> i32 {
    create(ParserKind::Encryption, base, len, out)
}
/// # Safety
/// out is writable; base has len readable bytes. Boolean parameters are zero or one.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_opf_create(
    base: *const u8,
    len: usize,
    metadata_only: u8,
    has_cache: u8,
    out: *mut *mut XpPkg,
) -> i32 {
    if out.is_null()
        || (base.is_null() && len != 0)
        || len > isize::MAX as usize
        || metadata_only > 1
        || has_cache > 1
    {
        return -1;
    }
    out.write(ptr::null_mut());
    let bytes = if len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(base, len)
    };
    let handle = alloc(Layout::new::<OpfHandle>()).cast::<OpfHandle>();
    if handle.is_null() {
        return -2;
    }
    handle.write(OpfHandle {
        tag: XpPkg { opf: true },
        parser: opf::Parser::new(metadata_only != 0, has_cache != 0),
    });
    if (*handle).parser.set_base(bytes).is_err() {
        xp_pkg_destroy(handle.cast());
        return -2;
    }
    out.write(handle.cast());
    0
}
/// # Safety
/// handle is live and exclusive, data has len readable bytes, consumed is writable.
/// Drain next to NEED_INPUT before feeding; null data is valid only for zero len.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_feed(
    handle: *mut XpPkg,
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
    consumed.write(if (*handle).opf {
        (*handle.cast::<OpfHandle>())
            .parser
            .feed(bytes, final_input != 0)
    } else {
        (*handle.cast::<SimpleHandle>())
            .parser
            .feed(bytes, final_input != 0)
    });
    0
}
/// # Safety
/// handle is live and exclusive; out is writable. Spans expire on the next call
/// to feed, next or destroy for this handle.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_next(handle: *mut XpPkg, out: *mut XpPkgRecord) -> i32 {
    if handle.is_null() || out.is_null() {
        return -1;
    }
    let span = |bytes: &[u8]| XpPkgSpan {
        data: bytes.as_ptr(),
        len: bytes.len(),
    };
    if (*handle).opf {
        let record = (*handle.cast::<OpfHandle>()).parser.next();
        let kind = match record.id {
            opf::NEED_INPUT => 0,
            opf::DONE => 1,
            opf::ERROR => 2,
            _ => 6,
        };
        out.write(XpPkgRecord {
            kind,
            error: record.error,
            value: span(record.value),
            href: span(record.href),
            anchor: span(&[]),
            level: 0,
            field: record.id,
            number: record.index.unwrap_or(0.0),
            number_present: u8::from(record.index.is_some()),
        });
    } else {
        let record = (*handle.cast::<SimpleHandle>()).parser.next();
        out.write(XpPkgRecord {
            kind: record.kind as u32,
            error: record.error,
            value: span(record.value),
            href: span(record.href),
            anchor: span(record.anchor),
            level: record.level,
            field: 0,
            number: 0.0,
            number_present: 0,
        });
    }
    0
}
/// # Safety
/// handle is null or the sole live owner. No call or borrowed span survives destroy.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_destroy(handle: *mut XpPkg) {
    if !handle.is_null() {
        if (*handle).opf {
            ptr::drop_in_place(handle.cast::<OpfHandle>());
            dealloc(handle.cast(), Layout::new::<OpfHandle>());
        } else {
            ptr::drop_in_place(handle.cast::<SimpleHandle>());
            dealloc(handle.cast(), Layout::new::<SimpleHandle>());
        }
    }
}
