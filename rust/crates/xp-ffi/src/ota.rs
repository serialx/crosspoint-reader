//! Synchronous OTA orchestration. Platform ownership and log formatting stay in C++.
use crate::web_jobs::board::Scanner;
use core::{
    ffi::{c_char, c_void, CStr},
    ptr, slice,
};

#[repr(C)]
pub struct State {
    size: usize,
    processed: usize,
    total: usize,
    available: bool,
}
#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Op {
    Parser,
    Feed,
    HasTag,
    HasFirmware,
    Tag,
    Url,
    Size,
    Asset,
    Store,
    Partition,
    Begin,
    Write,
    Abort,
    End,
    Select,
    Chip,
    Progress,
}
type Sink = unsafe extern "C" fn(*mut c_void, *const u8, usize) -> bool;
#[repr(C)]
pub struct Services {
    ctx: *mut c_void,
    operation: unsafe extern "C" fn(*mut c_void, Op, *const u8, usize) -> usize,
    fetch: unsafe extern "C" fn(*mut c_void, bool, Sink, *mut c_void) -> bool,
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, usize, usize, *const c_char)>,
    board_tag: *const u8,
    board_tag_len: usize,
    has_progress: bool,
}
impl Services {
    unsafe fn op(&self, op: Op, data: *const u8, len: usize) -> usize {
        (self.operation)(self.ctx, op, data, len)
    }
    unsafe fn call(&self, op: Op) -> usize {
        self.op(op, ptr::null(), 0)
    }
    unsafe fn diag(&self, e: u32, a: usize, b: usize, text: *const u8) {
        if let Some(diagnostic) = self.diagnostic {
            diagnostic(self.ctx, e, a, b, text.cast());
        }
    }
    unsafe fn tag(&self) -> &[u8] {
        slice::from_raw_parts(self.board_tag, self.board_tag_len)
    }
}
unsafe fn bytes<'a>(p: *const u8) -> &'a [u8] {
    CStr::from_ptr(p.cast()).to_bytes()
}

#[no_mangle]
pub unsafe extern "C" fn xp_ota_newer(
    available: bool,
    current: *const c_char,
    latest: *const c_char,
) -> bool {
    xp_core::ota::newer(available, bytes(current.cast()), bytes(latest.cast()))
}

struct Release<'a> {
    s: &'a Services,
    asset: [u8; 48],
    named: bool,
}
impl Release<'_> {
    fn append(&mut self, text: &[u8], pos: &mut usize) {
        for &byte in text {
            if *pos == 47 {
                break;
            }
            self.asset[*pos] = byte;
            *pos += 1;
        }
    }
    unsafe fn name(&mut self) {
        let mut pos = 0;
        self.append(b"crosspoint-", &mut pos);
        self.append(bytes(self.s.call(Op::Tag) as *const u8), &mut pos);
        let tag = self.s.tag();
        let board = tag.get(20..tag.len().saturating_sub(1)).unwrap_or(&[]);
        // Preserve the original 20-byte snprintf suffix buffer.
        let board = if board == b"x4" {
            b"x3-x4".as_slice()
        } else {
            board
        };
        self.append(b"-", &mut pos);
        self.append(&board[..board.len().min(18)], &mut pos);
        self.append(b".bin", &mut pos);
        self.s.op(Op::Asset, self.asset.as_ptr(), 0);
        self.named = true;
    }
}
unsafe extern "C" fn release_chunk(ctx: *mut c_void, data: *const u8, len: usize) -> bool {
    let r = &mut *ctx.cast::<Release<'_>>();
    let mut off = 0;
    while !r.named && off < len {
        r.s.op(Op::Feed, data.add(off), 1);
        off += 1;
        if r.s.call(Op::HasTag) != 0 {
            r.name();
        }
    }
    if off < len {
        r.s.op(Op::Feed, data.add(off), len - off);
    }
    true
}
#[no_mangle]
pub unsafe extern "C" fn xp_ota_check(state: *mut State, services: *const Services) -> u32 {
    let s = &*services;
    if s.call(Op::Parser) == 0 {
        s.diag(0, 0, 0, ptr::null());
        return 6;
    }
    s.op(Op::Asset, c"".as_ptr().cast(), 0);
    let mut release = Release {
        s,
        asset: [0; 48],
        named: false,
    };
    if !(s.fetch)(
        s.ctx,
        true,
        release_chunk,
        ptr::addr_of_mut!(release).cast(),
    ) {
        s.diag(1, 0, 0, ptr::null());
        return 2;
    }
    let has_tag = s.call(Op::HasTag);
    let has_fw = s.call(Op::HasFirmware);
    s.diag(2, has_tag, has_fw, ptr::null());
    if has_tag == 0 {
        s.diag(3, 0, 0, ptr::null());
        return 3;
    }
    if has_fw == 0 {
        s.diag(4, 0, 0, release.asset.as_ptr());
        return 1;
    }
    s.call(Op::Store);
    (*state).size = s.call(Op::Size);
    (*state).total = (*state).size;
    (*state).available = true;
    s.diag(5, (*state).size, 0, s.call(Op::Tag) as *const u8);
    s.diag(6, 0, 0, s.call(Op::Url) as *const u8);
    0
}

struct Install<'a> {
    s: &'a Services,
    state: *mut State,
    header: [u8; 14],
    header_len: usize,
    scanner: Scanner,
    wrong_chip: bool,
    flash_ok: bool,
    last_percent: i32,
}
unsafe extern "C" fn image_chunk(ctx: *mut c_void, data: *const u8, len: usize) -> bool {
    let r = &mut *ctx.cast::<Install<'_>>();
    let data = if len == 0 {
        &[]
    } else {
        slice::from_raw_parts(data, len)
    };
    if r.header_len < 14 {
        let take = data.len().min(14 - r.header_len);
        r.header[r.header_len..r.header_len + take].copy_from_slice(&data[..take]);
        r.header_len += take;
        if r.header_len == 14 {
            let image = u16::from_le_bytes([r.header[12], r.header[13]]);
            let device = r.s.call(Op::Chip) as u16;
            if device != u16::MAX && image != device {
                r.s.diag(9, image as usize, device as usize, ptr::null());
                r.wrong_chip = true;
                return false;
            }
        }
    }
    r.scanner.feed(data, r.s.tag());
    if r.scanner.mismatch {
        r.s.diag(10, 0, 0, r.scanner.captured.as_ptr());
        return false;
    }
    if r.s.op(Op::Write, data.as_ptr(), len) != 0 {
        r.flash_ok = false;
        return false;
    }
    (*r.state).processed = (*r.state).processed.wrapping_add(len);
    if r.s.has_progress && (*r.state).total > 0 {
        let pct =
            (((*r.state).processed as u64).wrapping_mul(100) / (*r.state).total as u64) as i32;
        if pct != r.last_percent {
            r.last_percent = pct;
            r.s.call(Op::Progress);
        }
    }
    true
}
#[no_mangle]
pub unsafe extern "C" fn xp_ota_install(
    state: *mut State,
    services: *const Services,
    newer: bool,
) -> u32 {
    if !newer {
        return 4;
    }
    let s = &*services;
    if s.call(Op::Partition) == 0 {
        s.diag(7, 0, 0, ptr::null());
        return 5;
    }
    let err = s.call(Op::Begin);
    if err != 0 {
        s.diag(8, err, 0, ptr::null());
        return 5;
    }
    (*state).processed = 0;
    let mut run = Install {
        s,
        state,
        header: [0; 14],
        header_len: 0,
        scanner: Scanner::new(),
        wrong_chip: false,
        flash_ok: true,
        last_percent: -1,
    };
    let fetched = (s.fetch)(s.ctx, false, image_chunk, ptr::addr_of_mut!(run).cast());
    if run.wrong_chip || run.scanner.mismatch {
        s.diag(11, 0, 0, ptr::null());
        s.call(Op::Abort);
        return 7;
    }
    if !fetched || !run.flash_ok {
        s.diag(12, usize::from(run.flash_ok), 0, ptr::null());
        s.call(Op::Abort);
        return if run.flash_ok { 2 } else { 5 };
    }
    let err = s.call(Op::End);
    if err != 0 {
        s.diag(13, err, 0, ptr::null());
        return 5;
    }
    let err = s.call(Op::Select);
    if err != 0 {
        s.diag(14, err, 0, ptr::null());
        return 5;
    }
    s.diag(15, 0, 0, ptr::null());
    0
}

#[repr(u32)]
#[derive(Clone, Copy)]
pub enum BootOp {
    Find,
    Size,
    Read,
    Erase,
    Write,
}
#[repr(C)]
pub struct BootServices {
    ctx: *mut c_void,
    operation: unsafe extern "C" fn(*mut c_void, BootOp, usize, *mut u8, usize) -> usize,
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32, u32, u32)>,
}
impl BootServices {
    unsafe fn op(&self, o: BootOp, off: usize, data: *mut u8, len: usize) -> usize {
        (self.operation)(self.ctx, o, off, data, len)
    }
    unsafe fn diag(&self, e: u32, a: u32, b: u32, c: u32) {
        if let Some(diagnostic) = self.diagnostic {
            diagnostic(self.ctx, e, a, b, c);
        }
    }
}
#[no_mangle]
pub extern "C" fn xp_ota_seq_crc(seq: u32) -> u32 {
    xp_core::ota::seq_crc(seq)
}
fn word(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes([
        bytes[offset],
        bytes[offset + 1],
        bytes[offset + 2],
        bytes[offset + 3],
    ])
}
#[no_mangle]
pub unsafe extern "C" fn xp_ota_boot_switch(services: *const BootServices, subtype: u8) -> bool {
    let s = &*services;
    if s.op(BootOp::Find, 0, ptr::null_mut(), 0) == 0 {
        s.diag(0, 0, 0, 0);
        return false;
    }
    let size = s.op(BootOp::Size, 0, ptr::null_mut(), 0);
    if size < 8192 {
        s.diag(1, size as u32, 0, 0);
        return false;
    }
    let mut slots = [[0u8; 32]; 2];
    if s.op(BootOp::Read, 0, slots[0].as_mut_ptr(), 32) == 0
        || s.op(BootOp::Read, 4096, slots[1].as_mut_ptr(), 32) == 0
    {
        s.diag(2, 0, 0, 0);
        return false;
    }
    let mut active = -1i32;
    let mut seq = 0u32;
    for (i, slot) in slots.iter().enumerate() {
        let n = word(slot, 0);
        let state = word(slot, 24);
        if n == u32::MAX || word(slot, 28) != xp_ota_seq_crc(n) || state == 3 || state == 4 {
            continue;
        }
        if active < 0 || n > seq {
            active = i as i32;
            seq = n;
        }
    }
    s.diag(3, active as u32, seq, 0);
    let dest = (subtype as u32).wrapping_sub(16);
    if dest > 15 {
        s.diag(4, subtype as u32, 0, 0);
        return false;
    }
    let mut next = seq.wrapping_add(1);
    if next.wrapping_sub(1) % 2 != dest % 2 {
        next = next.wrapping_add(1);
    }
    let mut entry = [0xffu8; 32];
    entry[..4].copy_from_slice(&next.to_le_bytes());
    entry[24..28].fill(0);
    let crc = xp_ota_seq_crc(next);
    entry[28..].copy_from_slice(&crc.to_le_bytes());
    let target = usize::from(active == 0);
    let off = target * 4096;
    if s.op(BootOp::Erase, off, ptr::null_mut(), 4096) == 0 {
        s.diag(5, target as u32, 0, 0);
        return false;
    }
    if s.op(BootOp::Write, off, entry.as_mut_ptr(), 32) == 0 {
        s.diag(6, target as u32, 0, 0);
        return false;
    }
    s.diag(7, target as u32, next, crc);
    true
}

#[repr(u32)]
#[derive(Clone, Copy)]
pub enum UsbOp {
    StopSerial,
    ResetPhy,
    PrepareInterrupts,
    CreateSemaphore,
    AllocInterrupt,
    DeleteSemaphore,
    Connect,
    WaitReset,
    DisableInterrupts,
    FreeInterrupt,
    Delay,
}
#[no_mangle]
pub unsafe extern "C" fn xp_ota_usb_handoff(
    ctx: *mut c_void,
    op: unsafe extern "C" fn(*mut c_void, UsbOp) -> bool,
) {
    op(ctx, UsbOp::StopSerial);
    op(ctx, UsbOp::ResetPhy);
    op(ctx, UsbOp::PrepareInterrupts);
    let mut listener = op(ctx, UsbOp::CreateSemaphore);
    if listener && !op(ctx, UsbOp::AllocInterrupt) {
        op(ctx, UsbOp::DeleteSemaphore);
        listener = false;
    }
    op(ctx, UsbOp::Connect);
    if listener {
        op(ctx, UsbOp::WaitReset);
    }
    op(ctx, UsbOp::DisableInterrupts);
    if listener {
        op(ctx, UsbOp::FreeInterrupt);
        op(ctx, UsbOp::DeleteSemaphore);
    } else {
        op(ctx, UsbOp::Delay);
    }
}
