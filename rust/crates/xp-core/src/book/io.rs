use core::{ffi::c_void, ptr};

#[repr(u32)]
pub enum Control {
    Normalize,
    IsText,
    IsEncrypted,
    PlainSize,
    OpenExtract,
    FlushExtract,
    CloseExtract,
    RemoveExtract,
}
#[repr(u32)]
pub enum Diagnostic {
    EmptyHref,
    Memory,
    ReadFailed,
    ZipReadFailed,
}
#[repr(u32)]
pub enum Stream {
    Text,
    Zip,
    Resource,
}
pub type Sink = unsafe extern "C" fn(*mut c_void, *const u8, usize) -> u8;
type Write = unsafe extern "C" fn(*mut c_void, *const u8, usize) -> usize;
#[repr(C)]
pub struct Services {
    pub ctx: *mut c_void,
    pub control: unsafe extern "C" fn(*mut c_void, u32) -> usize,
    pub zip_bytes: unsafe extern "C" fn(*mut c_void, *mut usize, u8) -> *mut u8,
    pub stream: unsafe extern "C" fn(*mut c_void, u32, usize, u8) -> u8,
    pub decrypt: unsafe extern "C" fn(*mut c_void, Sink, *mut c_void) -> u8,
    pub write: Write,
    pub size: unsafe extern "C" fn(*mut c_void, u8, *mut usize) -> u8,
    pub allocate: unsafe extern "C" fn(usize) -> *mut u8,
    pub release: unsafe extern "C" fn(*mut u8),
    pub diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, usize)>,
}
impl Services {
    fn control(&self, operation: Control) -> usize {
        unsafe { (self.control)(self.ctx, operation as u32) }
    }
    fn log(&self, event: Diagnostic, value: usize) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, event as u32, value) };
        }
    }
}
struct Buffer {
    data: *mut u8,
    capacity: usize,
    written: usize,
}
unsafe extern "C" fn append_buffer(ctx: *mut c_void, data: *const u8, size: usize) -> u8 {
    let buffer = unsafe { &mut *ctx.cast::<Buffer>() };
    if size > buffer.capacity.saturating_sub(buffer.written) {
        return 0;
    }
    if size != 0 {
        unsafe { ptr::copy_nonoverlapping(data, buffer.data.add(buffer.written), size) };
    }
    buffer.written += size;
    1
}
struct Output {
    ctx: *mut c_void,
    write: Write,
}
unsafe extern "C" fn append_stream(ctx: *mut c_void, data: *const u8, size: usize) -> u8 {
    let output = unsafe { &*ctx.cast::<Output>() };
    (unsafe { (output.write)(output.ctx, data, size) } == size) as u8
}

/// # Safety
/// All service callbacks and their context must be valid for this operation. Decrypt
/// invokes its sink synchronously and retains neither the sink nor its context.
/// Returned storage belongs to the C allocator; the caller releases it with free().
pub unsafe fn bytes(s: &Services, empty: bool, size: *mut usize, trailing: bool) -> *mut u8 {
    if empty {
        s.log(Diagnostic::EmptyHref, 0);
        return ptr::null_mut();
    }
    s.control(Control::Normalize);
    if s.control(Control::IsEncrypted) != 0 {
        let plain = s.control(Control::PlainSize);
        let Some(total) = plain.checked_add(usize::from(trailing)) else {
            return ptr::null_mut();
        };
        let content = unsafe { (s.allocate)(total.max(1)) };
        if content.is_null() {
            s.log(Diagnostic::Memory, total);
            return ptr::null_mut();
        }
        let mut buffer = Buffer {
            data: content,
            capacity: plain,
            written: 0,
        };
        let decrypted =
            unsafe { (s.decrypt)(s.ctx, append_buffer, (&mut buffer as *mut Buffer).cast()) };
        if decrypted == 0 || buffer.written != plain {
            unsafe { (s.release)(content) };
            s.log(Diagnostic::ReadFailed, 0);
            return ptr::null_mut();
        }
        if trailing {
            unsafe { *content.add(plain) = 0 };
        }
        if !size.is_null() {
            unsafe { *size = plain };
        }
        return content;
    }
    let content = unsafe { (s.zip_bytes)(s.ctx, size, trailing as u8) };
    if content.is_null() {
        s.log(Diagnostic::ZipReadFailed, 0);
    }
    content
}

/// # Safety
/// Callbacks must satisfy Services' borrowed/synchronous contract.
pub unsafe fn stream(s: &Services, empty: bool, chunk: usize, early: bool) -> bool {
    if empty {
        s.log(Diagnostic::EmptyHref, 0);
        return false;
    }
    if s.control(Control::IsText) != 0 {
        return unsafe { (s.stream)(s.ctx, Stream::Text as u32, chunk, early as u8) != 0 };
    }
    s.control(Control::Normalize);
    if s.control(Control::IsEncrypted) != 0 {
        let mut output = Output {
            ctx: s.ctx,
            write: s.write,
        };
        if unsafe { (s.decrypt)(s.ctx, append_stream, (&mut output as *mut Output).cast()) } == 0 {
            s.log(Diagnostic::ReadFailed, 0);
            return false;
        }
        return true;
    }
    unsafe { (s.stream)(s.ctx, Stream::Zip as u32, chunk, early as u8) != 0 }
}

/// # Safety
/// Callbacks must satisfy Services' borrowed/synchronous contract.
pub unsafe fn size(s: &Services, size: *mut usize) -> bool {
    let text = s.control(Control::IsText) != 0;
    if !text {
        s.control(Control::Normalize);
    }
    unsafe { (s.size)(s.ctx, text as u8, size) != 0 }
}

/// # Safety
/// Callbacks must satisfy Services' borrowed/synchronous contract. Resource streaming
/// may enter a separate book-read context; it must not reuse this extraction context.
pub unsafe fn extract(s: &Services) -> bool {
    if s.control(Control::OpenExtract) == 0 {
        return false;
    }
    let ok = unsafe { (s.stream)(s.ctx, Stream::Resource as u32, 8192, 0) != 0 };
    s.control(Control::FlushExtract);
    s.control(Control::CloseExtract);
    if !ok {
        s.control(Control::RemoveExtract);
    }
    ok
}
