//! SD font loading and content-shared interval ownership over synchronous HAL reads.
use super::{pool::Resident, Diagnostic, Intervals};
use crate::font::raw::{BmpInterval, Interval};
use core::{ffi::c_void, mem, ptr};

#[repr(C)]
pub struct Shared {
    pub table: *const c_void,
    pub count: u32,
    pub bmp16: bool,
    pub refs: u16,
}
#[repr(C)]
pub struct Services {
    pub ctx: *mut c_void,
    pub read: unsafe extern "C" fn(*mut c_void, u32, *mut u8, usize, bool) -> i32,
    pub alloc_styles: unsafe extern "C" fn(*mut c_void, u8) -> *mut Resident,
    pub alloc: unsafe extern "C" fn(usize) -> *mut u8,
    pub cleanup: unsafe extern "C" fn(*mut c_void),
    pub setup: unsafe extern "C" fn(*mut c_void, u8),
    pub diagnostic: unsafe extern "C" fn(*mut c_void, u32, u32, u32, u32),
    pub parse_error: unsafe extern "C" fn(*const Diagnostic, u32, u32),
}
impl Services {
    unsafe fn log(&self, event: u32, a: u32, b: u32, c: u32) {
        (self.diagnostic)(self.ctx, event, a, b, c);
    }
    unsafe fn fail(&self, event: u32, a: u32, b: u32) -> bool {
        self.log(event, a, b, 0);
        (self.cleanup)(self.ctx);
        false
    }
}
/// # Safety
/// A serialized owner lends sixteen shared slots, a live resident style, and
/// the same allocator used by load. No borrowed interval view may remain live.
pub unsafe fn release(style: &mut Resident, shared: *mut Shared, io: &super::pool::Services) {
    let table = if style.intervals_are_bmp16 {
        style.bmp_intervals.cast::<c_void>()
    } else {
        style.full_intervals.cast::<c_void>()
    };
    let mut last = true;
    for k in 0..16 {
        let entry = &mut *shared.add(k);
        if table.is_null() || entry.table != table {
            continue;
        }
        entry.refs = entry.refs.wrapping_sub(1);
        last = entry.refs == 0;
        if last {
            ptr::write_bytes(entry, 0, 1);
        }
        break;
    }
    if last {
        (io.free)(style.full_intervals.cast(), true);
        (io.free)(style.bmp_intervals.cast(), true);
    }
    style.full_intervals = ptr::null_mut();
    style.bmp_intervals = ptr::null_mut();
}
/// # Safety
/// The HAL file is at offset zero; slots has four bytes and hash is writable.
/// alloc_styles yields initialized, stable Resident records until cleanup.
/// Shared has sixteen serialized slots. Callbacks cannot retain loans or reenter.
pub unsafe fn load(slots: *mut u8, hash: *mut u32, shared: *mut Shared, io: &Services) -> bool {
    let mut header = [0u8; 32];
    if (io.read)(io.ctx, 0, header.as_mut_ptr(), 32, false) != 1 {
        io.log(1, 0, 0, 0);
        return false;
    }
    let mut directory = match super::header_diagnostic(&header) {
        Ok(d) => d,
        Err(e) => {
            (io.parse_error)(&e, 0, 0);
            return false;
        }
    };
    let styles = (io.alloc_styles)(io.ctx, directory.style_count);
    if styles.is_null() {
        io.log(
            2,
            u32::from(directory.style_count) * mem::size_of::<Resident>() as u32,
            0,
            0,
        );
        return false;
    }
    for i in 0..directory.style_count {
        let mut toc = [0u8; 32];
        if (io.read)(io.ctx, 0, toc.as_mut_ptr(), 32, false) != 1 {
            return io.fail(3, u32::from(i), 0);
        }
        let parsed = match super::style_diagnostic(&toc, &mut directory) {
            Ok(s) => s,
            Err(e) => {
                (io.parse_error)(&e, u32::from(toc[0]), 0);
                (io.cleanup)(io.ctx);
                return false;
            }
        };
        slots.add(usize::from(parsed.id)).write(i + 1);
        let s = &mut *styles.add(usize::from(i));
        s.header = parsed.header;
        s.offsets = parsed.offsets;
    }
    hash.write(directory.hash);
    for i in 0..4 {
        let slot = *slots.add(i);
        if slot == 0 {
            continue;
        }
        let s = &mut *styles.add(usize::from(slot - 1));
        if (io.read)(io.ctx, s.offsets[0], ptr::null_mut(), 0, true) != 1 {
            return io.fail(4, i as u32, 0);
        }
        let mut state = Intervals {
            previous_last: 0,
            expected_offset: 0,
            bmp16: s.header.glyph_count <= u16::MAX as u32,
        };
        let mut candidates = 0u16;
        for k in 0..16 {
            let entry = &*shared.add(k);
            if !entry.table.is_null() && entry.count == s.header.interval_count {
                candidates |= 1 << k;
            }
        }
        for j in 0..s.header.interval_count {
            let mut bytes = [0u8; 12];
            if (io.read)(io.ctx, 0, bytes.as_mut_ptr(), 12, false) != 1 {
                return io.fail(5, j, i as u32);
            }
            let iv = match super::interval_diagnostic(&bytes, s.header.glyph_count, j, &mut state) {
                Ok(iv) => iv,
                Err(e) => {
                    (io.parse_error)(&e, i as u32, j);
                    (io.cleanup)(io.ctx);
                    return false;
                }
            };
            for k in 0..16 {
                if candidates & (1 << k) == 0 {
                    continue;
                }
                let entry = &*shared.add(k);
                let (first, last, offset) = if entry.bmp16 {
                    let r = &*entry.table.cast::<BmpInterval>().add(j as usize);
                    (u32::from(r.first), u32::from(r.last), u32::from(r.offset))
                } else {
                    let r = &*entry.table.cast::<Interval>().add(j as usize);
                    (r.first, r.last, r.offset)
                };
                if first != iv.first || last != iv.last || offset != iv.offset {
                    candidates &= !(1 << k);
                }
            }
        }
        let mut aliased = false;
        for k in 0..16 {
            if candidates & (1 << k) == 0 {
                continue;
            }
            let entry = &mut *shared.add(k);
            if entry.bmp16 != state.bmp16 {
                continue;
            }
            if entry.bmp16 {
                s.bmp_intervals = entry.table.cast_mut().cast();
            } else {
                s.full_intervals = entry.table.cast_mut().cast();
            }
            s.intervals_are_bmp16 = entry.bmp16;
            entry.refs = entry.refs.wrapping_add(1);
            aliased = true;
            io.log(
                6,
                i as u32,
                s.header.interval_count,
                s.header.interval_count * if state.bmp16 { 6 } else { 12 },
            );
            break;
        }
        if !aliased {
            if (io.read)(io.ctx, s.offsets[0], ptr::null_mut(), 0, true) != 1 {
                return io.fail(7, i as u32, 0);
            }
            if state.bmp16 {
                s.bmp_intervals = (io.alloc)(s.header.interval_count.max(1) as usize * 6).cast();
                if s.bmp_intervals.is_null() {
                    return io.fail(8, i as u32, 0);
                }
                for j in 0..s.header.interval_count {
                    let mut bytes = [0u8; 12];
                    if (io.read)(io.ctx, 0, bytes.as_mut_ptr(), 12, false) != 1 {
                        return io.fail(9, j, i as u32);
                    }
                    // The first pass validated the records; preserve the second read's values.
                    let iv = ptr::read_unaligned(bytes.as_ptr().cast::<Interval>());
                    s.bmp_intervals.add(j as usize).write(BmpInterval {
                        first: iv.first as u16,
                        last: iv.last as u16,
                        offset: iv.offset as u16,
                    });
                }
                s.intervals_are_bmp16 = true;
            } else {
                s.full_intervals = (io.alloc)(s.header.interval_count.max(1) as usize * 12).cast();
                if s.full_intervals.is_null() {
                    return io.fail(10, s.header.interval_count, i as u32);
                }
                if (io.read)(
                    io.ctx,
                    0,
                    s.full_intervals.cast(),
                    s.header.interval_count as usize * 12,
                    false,
                ) != 1
                {
                    return io.fail(11, i as u32, 0);
                }
            }
            for k in 0..16 {
                let entry = &mut *shared.add(k);
                if !entry.table.is_null() {
                    continue;
                }
                entry.table = if s.intervals_are_bmp16 {
                    s.bmp_intervals.cast()
                } else {
                    s.full_intervals.cast()
                };
                entry.count = s.header.interval_count;
                entry.bmp16 = s.intervals_are_bmp16;
                entry.refs = 1;
                break;
            }
        }
        if s.header.kern_left_entry_count > 0 && s.header.kern_right_entry_count > 0 {
            let count = u32::from(s.header.kern_left_entry_count).div_ceil(64)
                + u32::from(s.header.kern_right_entry_count).div_ceil(64)
                + 2;
            s.kern_block_index = (io.alloc)(count as usize * 2).cast();
            if s.kern_block_index.is_null() {
                io.log(12, i as u32, 0, 0);
            }
        }
        ptr::write_bytes(&mut s.stub, 0, 1);
        s.stub.advance_y = s.header.advance_y;
        s.stub.ascender = i32::from(s.header.ascender);
        s.stub.descender = i32::from(s.header.descender);
        s.stub.is2_bit = s.header.is2_bit;
        s.font = &s.stub;
        (io.setup)(io.ctx, i as u8);
    }
    true
}

/// # Safety
/// Pool owns all style buffers. Shared is the serialized interval registry;
/// setup rewires only the requested style's facade handlers, after the loan ends.
pub unsafe fn clear(
    pool: &mut super::pool::Cache,
    shared: *mut Shared,
    count: u8,
    all: bool,
    ctx: *mut c_void,
    setup: Option<unsafe extern "C" fn(*mut c_void, u8)>,
    generation: unsafe extern "C" fn(),
) {
    generation();
    pool.clear_overflow();
    generation();
    pool.clear_advances();
    let io = &*pool.services;
    for i in 0..if all { count } else { 4 } {
        let slot = if all {
            i + 1
        } else {
            *pool.slots.add(usize::from(i))
        };
        if slot == 0 {
            continue;
        }
        let s = &mut *pool.styles.add(usize::from(slot - 1));
        s.free_mini(io);
        if all {
            release(s, shared, io);
            s.intervals_are_bmp16 = false;
        }
        s.free_ligatures(io);
        if all {
            (io.free)(s.kern_block_index.cast(), true);
            s.kern_block_index = ptr::null_mut();
            s.kern_block_index_ready = false;
        } else if let Some(setup) = setup {
            setup(ctx, i);
        }
    }
}
