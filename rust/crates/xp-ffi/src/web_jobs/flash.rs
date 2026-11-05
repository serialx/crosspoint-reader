use super::board::Scanner;
use core::{ffi::c_void, slice};

#[repr(u32)]
#[derive(Clone, Copy)]
enum Op {
    Open,
    Close,
    Size,
    Chip,
    Allocate,
    Free,
    Read,
    ShaInit,
    ShaUpdate,
    ShaFinish,
    ShaFree,
    Partition,
    Erase,
    Write,
    Switch,
    Progress,
    Delay,
    PartitionSize,
}
#[repr(C)]
pub struct Services {
    ctx: *mut c_void,
    operation: unsafe extern "C" fn(*mut c_void, u32, usize, usize, *mut u8) -> usize,
    diagnostic:
        Option<unsafe extern "C" fn(*mut c_void, u32, usize, usize, usize, usize, *const u8)>,
    tag: *const u8,
    tag_len: usize,
}
impl Services {
    unsafe fn op(&self, op: Op, a: usize, b: usize, p: *mut u8) -> usize {
        (self.operation)(self.ctx, op as u32, a, b, p)
    }
    unsafe fn simple(&self, op: Op) -> usize {
        self.op(op, 0, 0, core::ptr::null_mut())
    }
    unsafe fn log(&self, event: u32, args: [usize; 4], text: *const u8) {
        if let Some(f) = self.diagnostic {
            f(self.ctx, event, args[0], args[1], args[2], args[3], text);
        }
    }
    unsafe fn event(&self, event: u32, args: [usize; 4]) {
        self.log(event, args, core::ptr::null());
    }
}
const OK: u32 = 0;
const OPEN: u32 = 1;
const SMALL: u32 = 2;
const LARGE: u32 = 3;
const MAGIC: u32 = 4;
const SEGMENTS: u32 = 5;
const CHECKSUM: u32 = 6;
const SHA: u32 = 7;
const CHIP: u32 = 8;
const BOARD: u32 = 9;
const SIZE: u32 = 10;
const PARTITION: u32 = 11;
const OOM: u32 = 12;
const READ: u32 = 13;
const ERASE: u32 = 14;
const WRITE: u32 = 15;
const SWITCH: u32 = 16;

unsafe fn read(s: &Services, buf: *mut u8, len: usize) -> bool {
    s.op(Op::Read, len, 0, buf) == len
}
unsafe fn update(s: &Services, buf: *mut u8, len: usize) {
    s.op(Op::ShaUpdate, len, 0, buf);
}
unsafe fn body(s: &Services, file_size: usize, header: &mut [u8; 24], buf: *mut u8) -> u32 {
    let mut scanner = Scanner::new();
    let tag = slice::from_raw_parts(s.tag, s.tag_len);
    let mut xor = 0xef;
    let mut pos = 24usize;
    for seg in 0..header[1] {
        if pos.wrapping_add(8) > file_size {
            s.event(6, [seg as usize, pos, 0, 0]);
            return SEGMENTS;
        }
        let mut sh = [0; 8];
        if !read(s, sh.as_mut_ptr(), 8) {
            return READ;
        }
        update(s, sh.as_mut_ptr(), 8);
        pos = pos.wrapping_add(8);
        let len = u32::from_le_bytes([sh[4], sh[5], sh[6], sh[7]]) as usize;
        if pos.wrapping_add(len) > file_size {
            s.event(7, [seg as usize, pos, len, file_size]);
            return SEGMENTS;
        }
        let mut remaining = len;
        while remaining > 0 {
            let want = remaining.min(4096);
            if !read(s, buf, want) {
                return READ;
            }
            update(s, buf, want);
            let bytes = slice::from_raw_parts(buf, want);
            scanner.feed(bytes, tag);
            for &b in bytes {
                xor ^= b;
            }
            remaining -= want;
        }
        pos = pos.wrapping_add(len);
    }
    if scanner.mismatch {
        s.log(8, [0; 4], scanner.captured.as_ptr());
        return BOARD;
    }
    let pad_end = pos.wrapping_add(16) & !15;
    let trailer = if header[23] != 0 { 32 } else { 0 };
    let total = pad_end.wrapping_add(trailer);
    if total != file_size {
        s.event(9, [pad_end, trailer, total, file_size]);
        return SIZE;
    }
    let pad_len = pad_end.wrapping_sub(pos);
    let mut pad = [0; 16];
    if pad_len == 0 || pad_len > 16 {
        return SIZE;
    }
    if !read(s, pad.as_mut_ptr(), pad_len) {
        return READ;
    }
    update(s, pad.as_mut_ptr(), pad_len);
    let stored = pad[pad_len - 1];
    if xor != stored {
        s.event(10, [xor as usize, stored as usize, 0, 0]);
        return CHECKSUM;
    }
    if trailer != 0 {
        let mut computed = [0; 32];
        let mut stored = [0; 32];
        s.op(Op::ShaFinish, 0, 0, computed.as_mut_ptr());
        if !read(s, stored.as_mut_ptr(), 32) {
            return READ;
        }
        if computed != stored {
            s.event(11, [0; 4]);
            return SHA;
        }
    }
    OK
}
unsafe fn validate(s: &Services, partition_size: usize) -> u32 {
    if s.simple(Op::Open) == 0 {
        s.event(0, [0; 4]);
        return OPEN;
    }
    let size = s.simple(Op::Size);
    let result = validate_open(s, size, partition_size);
    s.simple(Op::Close);
    s.simple(Op::Free);
    result
}
unsafe fn validate_open(s: &Services, size: usize, partition_size: usize) -> u32 {
    if size < 65536 {
        s.event(1, [size, 0, 0, 0]);
        return SMALL;
    }
    if partition_size > 0 && size > partition_size {
        s.event(2, [size, partition_size, 0, 0]);
        return LARGE;
    }
    let mut header = [0; 24];
    if !read(s, header.as_mut_ptr(), 24) {
        s.event(3, [0; 4]);
        return READ;
    }
    if header[0] != 0xe9 {
        s.event(4, [header[0] as usize, 0, 0, 0]);
        return MAGIC;
    }
    let chip = u16::from_le_bytes([header[12], header[13]]) as usize;
    let device = s.simple(Op::Chip);
    if device != 0xffff && chip != device {
        s.event(5, [chip, device, 0, 0]);
        return CHIP;
    }
    let buf = s.simple(Op::Allocate) as *mut u8;
    if buf.is_null() {
        return OOM;
    }
    s.simple(Op::ShaInit);
    update(s, header.as_mut_ptr(), 24);
    let result = body(s, size, &mut header, buf);
    s.simple(Op::ShaFree);
    result
}
// File/crypto/partition callbacks are synchronous and cannot reenter this job.
#[no_mangle]
pub unsafe extern "C" fn xp_flash_validate(s: *const Services, partition_size: usize) -> u32 {
    validate(&*s, partition_size)
}

#[no_mangle]
pub unsafe extern "C" fn xp_flash_run(s: *const Services, already_validated: bool) -> u32 {
    let s = &*s;
    if s.simple(Op::Partition) == 0 {
        s.event(12, [0; 4]);
        return PARTITION;
    }
    let partition = s.simple(Op::PartitionSize);
    if !already_validated {
        let result = validate(s, partition);
        if result != OK {
            s.event(13, [result as usize, 0, 0, 0]);
            return result;
        }
    }
    if s.simple(Op::Open) == 0 {
        s.event(14, [0; 4]);
        return OPEN;
    }
    let size = s.simple(Op::Size);
    s.event(15, [size, 0, 0, 0]);
    let buf = s.simple(Op::Allocate) as *mut u8;
    if buf.is_null() {
        s.event(16, [0; 4]);
        s.simple(Op::Close);
        return OOM;
    }
    let result = write(s, partition, size, buf);
    s.simple(Op::Close);
    let result = if result == OK && s.simple(Op::Switch) == 0 {
        s.event(20, [0; 4]);
        SWITCH
    } else {
        result
    };
    s.op(Op::Free, 0, 0, buf);
    result
}
unsafe fn write(s: &Services, partition: usize, size: usize, buf: *mut u8) -> u32 {
    let mut pos = 0;
    let mut erased = 0;
    while pos < size {
        if pos >= erased {
            let room = partition.wrapping_sub(pos);
            let len = ((room.min(65536) + 4095) & !4095).min(room);
            if s.op(Op::Erase, pos, len, core::ptr::null_mut()) == 0 {
                s.event(17, [pos, len, 0, 0]);
                return ERASE;
            }
            erased = pos.wrapping_add(len);
        }
        let want = (size - pos).min(4096);
        let got = s.op(Op::Read, want, 0, buf);
        if got != want {
            s.event(18, [pos, got, want, 0]);
            return READ;
        }
        if s.op(Op::Write, pos, want, buf) == 0 {
            s.event(19, [pos, 0, 0, 0]);
            return WRITE;
        }
        pos += want;
        s.op(Op::Progress, pos, size, core::ptr::null_mut());
        s.simple(Op::Delay);
    }
    OK
}
