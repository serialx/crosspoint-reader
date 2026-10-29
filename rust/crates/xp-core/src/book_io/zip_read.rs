//! ZIP extraction sequencing; allocation and the existing inflater loan stay with C++.
use core::{ffi::c_void, ptr};

pub type FillFn = unsafe extern "C" fn(*mut c_void, *mut *const u8) -> usize;
#[repr(C)]
pub struct Services {
    pub context: *mut c_void,
    pub read: unsafe extern "C" fn(*mut c_void, *mut u8, usize) -> i32,
    pub allocate: unsafe extern "C" fn(*mut c_void, usize) -> *mut u8,
    pub release: unsafe extern "C" fn(*mut c_void, *mut u8),
    pub init: unsafe extern "C" fn(*mut c_void, bool, FillFn, *mut c_void) -> bool,
    pub exact: unsafe extern "C" fn(*mut c_void, *mut u8, usize) -> bool,
    pub inflate: unsafe extern "C" fn(*mut c_void, *mut u8, usize, *mut usize) -> i32,
    pub write: unsafe extern "C" fn(*mut c_void, *const u8, usize) -> usize,
    pub diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32, u32, u32, u32)>,
}
#[repr(u32)]
enum Event {
    ReadCompressed = 1,
    OutputAllocation,
    ReadData,
    InputAllocation,
    Init,
    Inflate,
    Method,
    BufferAllocation,
    ReadMore,
    Write,
    StreamOutputAllocation,
    Exceeds,
    Mismatch,
    Decompressed,
    Failed,
}
impl Services {
    unsafe fn log(&self, event: Event, a: usize, b: usize) {
        if let Some(log) = self.diagnostic {
            log(self.context, event as u32, a as u32, b as u32, 0, 0);
        }
    }
    unsafe fn allocate(&self, size: usize) -> *mut u8 {
        (self.allocate)(self.context, size)
    }
    unsafe fn release(&self, data: *mut u8) {
        (self.release)(self.context, data);
    }
}
struct Fill<'a> {
    io: &'a Services,
    remaining: usize,
    buffer: *mut u8,
    capacity: usize,
}
unsafe extern "C" fn fill(context: *mut c_void, out: *mut *const u8) -> usize {
    let ctx = &mut *context.cast::<Fill<'_>>();
    if ctx.remaining == 0 {
        return 0;
    }
    let count = (ctx.io.read)(ctx.io.context, ctx.buffer, ctx.remaining.min(ctx.capacity));
    if count < 0 {
        ctx.io.log(Event::ReadCompressed, count as usize, 0);
        return 0;
    }
    ctx.remaining -= count as usize;
    out.write(ctx.buffer);
    count as usize
}

/// # Safety
/// Callbacks obey their buffer lengths, do not reenter or retain pointers beyond
/// this operation. Allocate/release preserve C malloc ownership of the returned buffer.
pub unsafe fn memory(
    io: &Services,
    method: u16,
    compressed: u32,
    inflated: u32,
    trailing_nul: bool,
    size: *mut usize,
) -> *mut u8 {
    let Some(capacity) = inflated.checked_add(u32::from(trailing_nul)) else {
        return ptr::null_mut();
    };
    let data = io.allocate(capacity as usize);
    if data.is_null() {
        io.log(Event::OutputAllocation, capacity as usize, 0);
        return data;
    }
    match method {
        0 => {
            let read = (io.read)(io.context, data, inflated as usize);
            if read < 0 || read as usize != inflated as usize {
                io.log(Event::ReadData, 0, 0);
                io.release(data);
                return ptr::null_mut();
            }
        }
        8 => {
            let input = io.allocate(1024);
            if input.is_null() {
                io.log(Event::InputAllocation, 0, 0);
                io.release(data);
                return ptr::null_mut();
            }
            let mut context = Fill {
                io,
                remaining: compressed as usize,
                buffer: input,
                capacity: 1024,
            };
            if !(io.init)(
                io.context,
                false,
                fill,
                (&mut context as *mut Fill<'_>).cast(),
            ) {
                io.log(Event::Init, 0, 0);
                io.release(input);
                io.release(data);
                return ptr::null_mut();
            }
            if !(io.exact)(io.context, data, inflated as usize) {
                io.log(Event::Inflate, 0, 0);
                io.release(input);
                io.release(data);
                return ptr::null_mut();
            }
            io.release(input);
        }
        _ => {
            io.log(Event::Method, 0, 0);
            io.release(data);
            return ptr::null_mut();
        }
    }
    if trailing_nul {
        data.add(inflated as usize).write(0);
    }
    if !size.is_null() {
        size.write(inflated as usize);
    }
    data
}

/// # Safety
/// Same callback contract as memory. Write borrows each chunk only for its call.
pub unsafe fn stream(
    io: &Services,
    method: u16,
    compressed: u32,
    inflated: u32,
    chunk_size: usize,
    early_stop: bool,
) -> bool {
    match method {
        0 => {
            let buffer = io.allocate(chunk_size);
            if buffer.is_null() {
                io.log(Event::BufferAllocation, 0, 0);
                return false;
            }
            let mut remaining = inflated as usize;
            while remaining != 0 {
                let count = (io.read)(io.context, buffer, remaining.min(chunk_size));
                if count <= 0 {
                    io.log(Event::ReadMore, 0, 0);
                    io.release(buffer);
                    return false;
                }
                let count = count as usize;
                if (io.write)(io.context, buffer, count) != count {
                    io.release(buffer);
                    if early_stop {
                        return true;
                    }
                    io.log(Event::Write, 0, 0);
                    return false;
                }
                remaining -= count;
            }
            io.release(buffer);
            true
        }
        8 => {
            let input = io.allocate(chunk_size);
            if input.is_null() {
                io.log(Event::InputAllocation, 0, 0);
                return false;
            }
            let output = io.allocate(chunk_size);
            if output.is_null() {
                io.log(Event::StreamOutputAllocation, 0, 0);
                io.release(input);
                return false;
            }
            let mut context = Fill {
                io,
                remaining: compressed as usize,
                buffer: input,
                capacity: chunk_size,
            };
            if !(io.init)(
                io.context,
                true,
                fill,
                (&mut context as *mut Fill<'_>).cast(),
            ) {
                io.log(Event::Init, 0, 0);
                io.release(output);
                io.release(input);
                return false;
            }
            let mut success = false;
            let mut total = 0usize;
            loop {
                let mut produced = 0;
                let status = (io.inflate)(io.context, output, chunk_size, &mut produced);
                total = total.wrapping_add(produced);
                if total > inflated as usize {
                    io.log(Event::Exceeds, total, inflated as usize);
                    break;
                }
                if produced != 0 && (io.write)(io.context, output, produced) != produced {
                    if early_stop {
                        success = true;
                    } else {
                        io.log(Event::Write, 0, 0);
                    }
                    break;
                }
                match status {
                    1 => {
                        if total != inflated as usize {
                            io.log(Event::Mismatch, inflated as usize, total);
                            break;
                        }
                        io.log(Event::Decompressed, compressed as usize, inflated as usize);
                        success = true;
                        break;
                    }
                    2 => {
                        io.log(Event::Failed, 0, 0);
                        break;
                    }
                    _ => {}
                }
            }
            io.release(output);
            io.release(input);
            success
        }
        _ => {
            io.log(Event::Method, 0, 0);
            false
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    struct Probe {
        blocks: [[u8; 1024]; 2],
        attempts: usize,
        live: usize,
        fail: usize,
        init_ok: bool,
        events: [u32; 8],
        event_count: usize,
    }
    impl Probe {
        fn new(fail: usize) -> Self {
            Self {
                blocks: [[0; 1024]; 2],
                attempts: 0,
                live: 0,
                fail,
                init_ok: true,
                events: [0; 8],
                event_count: 0,
            }
        }
        fn services(&mut self) -> Services {
            Services {
                context: (self as *mut Self).cast(),
                read,
                allocate,
                release,
                init,
                exact,
                inflate,
                write,
                diagnostic: Some(diagnostic),
            }
        }
    }
    unsafe extern "C" fn read(_: *mut c_void, _: *mut u8, size: usize) -> i32 {
        size as i32
    }
    unsafe extern "C" fn allocate(ctx: *mut c_void, size: usize) -> *mut u8 {
        let p = &mut *ctx.cast::<Probe>();
        let index = p.attempts;
        p.attempts += 1;
        if index == p.fail {
            return ptr::null_mut();
        }
        assert!(size <= 1024 && index < 2);
        p.live += 1;
        p.blocks[index].as_mut_ptr()
    }
    unsafe extern "C" fn release(ctx: *mut c_void, _: *mut u8) {
        (*ctx.cast::<Probe>()).live -= 1;
    }
    unsafe extern "C" fn init(ctx: *mut c_void, _: bool, _: FillFn, _: *mut c_void) -> bool {
        (*ctx.cast::<Probe>()).init_ok
    }
    unsafe extern "C" fn exact(_: *mut c_void, _: *mut u8, _: usize) -> bool {
        true
    }
    unsafe extern "C" fn inflate(
        _: *mut c_void,
        _: *mut u8,
        _: usize,
        produced: *mut usize,
    ) -> i32 {
        produced.write(0);
        1
    }
    unsafe extern "C" fn write(_: *mut c_void, _: *const u8, size: usize) -> usize {
        size
    }
    unsafe extern "C" fn diagnostic(ctx: *mut c_void, event: u32, _: u32, _: u32, _: u32, _: u32) {
        let p = &mut *ctx.cast::<Probe>();
        p.events[p.event_count] = event;
        p.event_count += 1;
    }

    #[test]
    fn every_extraction_buffer_failure_releases_prior_owners() {
        for method in [0, 8] {
            let allocations = if method == 0 { 1 } else { 2 };
            for fail in 0..allocations {
                let mut p = Probe::new(fail);
                let io = p.services();
                assert!(unsafe { memory(&io, method, 0, 0, false, ptr::null_mut()) }.is_null());
                assert_eq!(p.attempts, fail + 1);
                assert_eq!(p.live, 0);
                let mut p = Probe::new(fail);
                let io = p.services();
                assert!(!unsafe { stream(&io, method, 0, 0, 64, false) });
                assert_eq!(p.attempts, fail + 1);
                assert_eq!(p.live, 0);
            }
        }
    }
    #[test]
    fn inflater_failure_releases_both_buffers_and_overflow_allocates_nothing() {
        for streaming in [false, true] {
            let mut p = Probe::new(usize::MAX);
            p.init_ok = false;
            let io = p.services();
            if streaming {
                assert!(!unsafe { stream(&io, 8, 0, 0, 64, false) });
            } else {
                assert!(unsafe { memory(&io, 8, 0, 0, false, ptr::null_mut()) }.is_null());
            }
            assert_eq!(p.attempts, 2);
            assert_eq!(p.live, 0);
            assert_eq!(&p.events[..p.event_count], &[Event::Init as u32]);
        }
        let mut p = Probe::new(usize::MAX);
        let io = p.services();
        assert!(unsafe { memory(&io, 0, 0, u32::MAX, true, ptr::null_mut()) }.is_null());
        assert_eq!(p.attempts, 0);
    }
}
