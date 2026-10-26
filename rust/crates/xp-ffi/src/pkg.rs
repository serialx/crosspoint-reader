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

use facade::Sink;
use xp_core::epub::{cache, facade};

#[repr(C)]
pub struct XpPkgCache {
    state: cache::Cache,
}
#[derive(Clone, Copy)]
struct DriverSink {
    items: Option<cache::Io>,
    cache: *mut XpPkgCache,
    cache_io: Option<cache::Io>,
}
impl Sink for DriverSink {
    fn io(&self) -> Option<&cache::Io> {
        self.items.as_ref()
    }
    fn spine(&mut self, href: &[u8]) -> xp_core::epub::Result<()> {
        match (unsafe { self.cache.as_mut() }, self.cache_io.as_ref()) {
            (Some(cache), Some(io)) => cache.state.create_spine(io, href),
            _ => Ok(()),
        }
    }
    fn toc(
        &mut self,
        title: &[u8],
        href: &[u8],
        anchor: &[u8],
        level: u8,
    ) -> xp_core::epub::Result<()> {
        match (unsafe { self.cache.as_mut() }, self.cache_io.as_ref()) {
            (Some(cache), Some(io)) => cache.state.create_toc(io, title, href, anchor, level),
            _ => Ok(()),
        }
    }
}
#[repr(C)]
pub struct XpPkgPass {
    opf: bool,
}
#[repr(C)]
struct SimplePass {
    tag: XpPkgPass,
    driver: facade::Simple,
    sink: DriverSink,
}
#[repr(C)]
struct OpfPass {
    tag: XpPkgPass,
    driver: facade::Opf,
    sink: DriverSink,
}
unsafe fn bytes<'a>(value: XpPkgSpan) -> Option<&'a [u8]> {
    if value.len > isize::MAX as usize || (value.data.is_null() && value.len != 0) {
        None
    } else if value.len == 0 {
        Some(&[])
    } else {
        Some(core::slice::from_raw_parts(value.data, value.len))
    }
}
/// # Safety
/// out is writable; base is readable; sink contexts/cache outlive the exclusive pass.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_pass_create(
    kind: u32,
    base: XpPkgSpan,
    size: usize,
    metadata_only: u8,
    items: *const cache::Io,
    cache: *mut XpPkgCache,
    cache_io: *const cache::Io,
    out: *mut *mut XpPkgPass,
) -> i32 {
    if out.is_null() || metadata_only > 1 {
        return -1;
    }
    out.write(ptr::null_mut());
    let Some(base) = bytes(base) else {
        return -1;
    };
    let sink = DriverSink {
        items: items.as_ref().copied(),
        cache,
        cache_io: cache_io.as_ref().copied(),
    };
    if !cache.is_null() && sink.cache_io.is_none() {
        return -1;
    }
    if kind == 5 {
        let p = alloc(Layout::new::<OpfPass>()).cast::<OpfPass>();
        if p.is_null() {
            return -2;
        }
        ptr::addr_of_mut!((*p).tag).write(XpPkgPass { opf: true });
        ptr::addr_of_mut!((*p).driver).write(facade::Opf::new(
            size,
            metadata_only != 0,
            !cache.is_null(),
        ));
        ptr::addr_of_mut!((*p).sink).write(sink);
        if (*p).driver.parser.set_base(base).is_err() {
            xp_pkg_pass_destroy(p.cast());
            return -2;
        }
        out.write(p.cast());
    } else {
        let kind = match kind {
            1 => ParserKind::Container,
            2 => ParserKind::Ncx,
            3 => ParserKind::Nav,
            4 => ParserKind::Encryption,
            _ => return -1,
        };
        let p = alloc(Layout::new::<SimplePass>()).cast::<SimplePass>();
        if p.is_null() {
            return -2;
        }
        ptr::addr_of_mut!((*p).tag).write(XpPkgPass { opf: false });
        ptr::addr_of_mut!((*p).driver).write(facade::Simple::new(kind, size));
        ptr::addr_of_mut!((*p).sink).write(sink);
        if (*p).driver.parser.set_base(base).is_err() {
            xp_pkg_pass_destroy(p.cast());
            return -2;
        }
        out.write(p.cast());
    }
    0
}
/// # Safety
/// p is null or exclusively owned; callbacks cannot reenter or destroy it.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_pass_destroy(p: *mut XpPkgPass) {
    if p.is_null() {
        return;
    }
    if (*p).opf {
        let p = p.cast::<OpfPass>();
        if let Some(io) = (*p).sink.items.as_ref() {
            if io.control(cache::ITEMS, cache::IS_OPEN) != 0 {
                io.control(cache::ITEMS, cache::CLOSE);
            }
            io.control(cache::ITEMS, cache::REMOVE);
        }
        ptr::drop_in_place(p);
        dealloc(p.cast(), Layout::new::<OpfPass>());
    } else {
        ptr::drop_in_place(p.cast::<SimplePass>());
        dealloc(p.cast(), Layout::new::<SimplePass>());
    }
}
/// # Safety
/// p is live and exclusive; input is readable for this call. No callback reentry.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_pass_write(p: *mut XpPkgPass, input: XpPkgSpan) -> usize {
    if p.is_null() {
        return 0;
    }
    let Some(input) = bytes(input) else {
        return 0;
    };
    if (*p).opf {
        let p = &mut *p.cast::<OpfPass>();
        p.driver.write(input, &mut p.sink)
    } else {
        let p = &mut *p.cast::<SimplePass>();
        p.driver.write(input, &mut p.sink)
    }
}
/// # Safety
/// p is live and exclusive; callbacks cannot reenter it.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_pass_finish(p: *mut XpPkgPass) -> u8 {
    if p.is_null() || (*p).opf {
        return 0;
    }
    let p = &mut *p.cast::<SimplePass>();
    u8::from(p.driver.finish(&mut p.sink))
}
/// # Safety
/// p is live. Returned storage expires on write/finish/destroy. CSS fields start at 32.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_pass_value(p: *const XpPkgPass, field: u32) -> XpPkgSpan {
    let value = if p.is_null() {
        &[][..]
    } else if (*p).opf {
        let driver = &(*p.cast::<OpfPass>()).driver;
        if field >= 32 {
            driver
                .css
                .get((field - 32) as usize)
                .map_or(&[][..], |v| v.as_slice())
        } else {
            driver.parser.value(field)
        }
    } else {
        (*p.cast::<SimplePass>()).driver.parser.value()
    };
    XpPkgSpan {
        data: value.as_ptr(),
        len: value.len(),
    }
}
/// # Safety
/// p is live; index is writable when nonnull. Returns whether a series index exists.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_pass_index(p: *const XpPkgPass, index: *mut f32) -> u8 {
    if p.is_null() || !(*p).opf || index.is_null() {
        return 0;
    }
    if let Some(value) = (*p.cast::<OpfPass>()).driver.parser.index() {
        index.write(value);
        1
    } else {
        0
    }
}
/// # Safety
/// p is live. OPF returns CSS count; simple passes return the protection latch.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_pass_count(p: *const XpPkgPass) -> usize {
    if p.is_null() {
        0
    } else if (*p).opf {
        (*p.cast::<OpfPass>()).driver.css.len()
    } else {
        usize::from((*p.cast::<SimplePass>()).driver.protection)
    }
}
/// # Safety
/// out is writable and receives the sole owner.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_cache_create(out: *mut *mut XpPkgCache) -> i32 {
    if out.is_null() {
        return -1;
    }
    out.write(ptr::null_mut());
    let p = alloc(Layout::new::<XpPkgCache>()).cast::<XpPkgCache>();
    if p.is_null() {
        return -2;
    }
    p.write(XpPkgCache {
        state: cache::Cache::default(),
    });
    out.write(p);
    0
}
/// # Safety
/// p is null or exclusively owned, with no surviving pass borrowing it.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_cache_destroy(p: *mut XpPkgCache) {
    if !p.is_null() {
        ptr::drop_in_place(p);
        dealloc(p.cast(), Layout::new::<XpPkgCache>());
    }
}
/// # Safety
/// p and io are live; callbacks cannot reenter p. op: begin/end OPF, begin/end TOC, end write.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_cache_control(
    p: *mut XpPkgCache,
    io: *const cache::Io,
    op: u32,
) -> i32 {
    let (Some(p), Some(io)) = (p.as_mut(), io.as_ref()) else {
        return -1;
    };
    let result = match op {
        0 => p.state.begin_opf(io),
        1 => p.state.end_opf(io),
        2 => p.state.begin_toc(io),
        3 => p.state.end_toc(io),
        4 => {
            if !p.state.building {
                Err(2)
            } else {
                p.state.building = false;
                Ok(())
            }
        }
        _ => return -1,
    };
    result.map_or_else(|e| -(e as i32), |_| 0)
}
/// # Safety
/// p/io are live; spans readable; callbacks cannot reenter p. kind 0 spine, 1 TOC.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_cache_entry(
    p: *mut XpPkgCache,
    io: *const cache::Io,
    kind: u32,
    title: XpPkgSpan,
    href: XpPkgSpan,
    anchor: XpPkgSpan,
    level: u8,
) -> i32 {
    let (Some(p), Some(io), Some(title), Some(href), Some(anchor)) = (
        p.as_mut(),
        io.as_ref(),
        bytes(title),
        bytes(href),
        bytes(anchor),
    ) else {
        return -1;
    };
    let result = match kind {
        0 => p.state.create_spine(io, href),
        1 => p.state.create_toc(io, title, href, anchor, level),
        _ => return -1,
    };
    result.map_or_else(|e| -(e as i32), |_| 0)
}
/// # Safety
/// p/io are live; metadata points to five readable spans; callbacks cannot reenter p.
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_cache_build(
    p: *mut XpPkgCache,
    io: *const cache::Io,
    metadata: *const XpPkgSpan,
) -> i32 {
    let (Some(p), Some(io)) = (p.as_mut(), io.as_ref()) else {
        return -1;
    };
    if metadata.is_null() {
        return -1;
    }
    let mut values = [&[][..]; 5];
    for (i, value) in values.iter_mut().enumerate() {
        let Some(v) = bytes(metadata.add(i).read()) else {
            return -1;
        };
        *value = v;
    }
    p.state
        .build(io, values)
        .map_or_else(|e| -(e as i32), |_| 0)
}
/// # Safety
/// p is live or null; returns spine count (0) or TOC count (1).
#[no_mangle]
pub unsafe extern "C" fn xp_pkg_cache_count(p: *const XpPkgCache, toc: u8) -> u16 {
    p.as_ref().map_or(0, |p| {
        if toc == 0 {
            p.state.spine_count
        } else {
            p.state.toc_count
        }
    })
}
