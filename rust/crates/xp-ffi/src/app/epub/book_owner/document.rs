use super::{app_abi, log, Text, XpEpubLeaves};
use core::{ffi::c_void, marker::PhantomPinned, mem::MaybeUninit, pin::Pin, ptr};

// Plugin event variables are C strings even though the wire view has length 32.
pub(in crate::app::epub) struct DocumentId([u8; 33]);
impl DocumentId {
    pub(in crate::app::epub) fn wire(&self) -> app_abi::Text {
        app_abi::Text::from_bytes(&self.0[..32])
    }
}

#[repr(u8)]
enum Operation {
    Init,
    Drop,
    Open,
    Size,
    Seek,
    Read,
    DigestBegin,
    DigestAdd,
    DigestFinish,
}
struct Storage {
    bytes: MaybeUninit<[usize; 32]>,
    _pin: PhantomPinned,
}
struct Physical<'a> {
    leaves: &'a XpEpubLeaves,
    storage: Pin<&'a mut Storage>,
}
impl Physical<'_> {
    fn call(&mut self, op: Operation, input: *const u8, output: *mut u8, value: u32) -> i64 {
        // The native HalFile remains at this address from Init through Drop.
        let address = unsafe { self.storage.as_mut().get_unchecked_mut().bytes.as_mut_ptr() };
        unsafe { (self.leaves.document_io)(address.cast(), op as u8, input, output, value) }
    }
    fn step(&mut self, op: Operation, value: u32) -> i64 {
        self.call(op, ptr::null(), ptr::null_mut(), value)
    }
}
impl Drop for Physical<'_> {
    fn drop(&mut self) {
        self.step(Operation::Drop, 0);
    }
}
struct Sample<'a, 'b> {
    io: &'a mut Physical<'b>,
    bytes: [u8; 1024],
    failed: bool,
}
unsafe extern "C" fn read(raw: *mut c_void, offset: usize, count: usize) -> usize {
    let sample = unsafe { &mut *raw.cast::<Sample<'_, '_>>() };
    if sample.failed {
        return 0;
    }
    if sample.io.step(Operation::Seek, offset as u32) == 0 {
        sample.failed = true;
        log::message(
            sample.io.leaves,
            0,
            b"KODoc",
            format_args!("Failed to seek to offset {offset}"),
        );
        return 0;
    }
    let read = sample.io.call(
        Operation::Read,
        ptr::null(),
        sample.bytes.as_mut_ptr(),
        count as u32,
    );
    // The plan bounds count by the size captured at open, including the final sample.
    if read != count as i64 {
        sample.failed = true;
        return 0;
    }
    sample.io.call(
        Operation::DigestAdd,
        sample.bytes.as_ptr(),
        ptr::null_mut(),
        read as u32,
    );
    read as usize
}

pub(in crate::app::epub) fn calculate(leaves: &XpEpubLeaves, path: &Text) -> Option<DocumentId> {
    let mut storage = core::pin::pin!(Storage {
        bytes: MaybeUninit::uninit(),
        _pin: PhantomPinned
    });
    let mut io = Physical {
        leaves,
        storage: storage.as_mut(),
    };
    io.step(Operation::Init, 0);
    let bytes = path.bytes();
    let logged_path = &bytes[..bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len())];
    if io.call(Operation::Open, path.wire().data, ptr::null_mut(), 0) == 0 {
        log::with_bytes(
            leaves,
            0,
            b"KODoc",
            &[b"Failed to open file: ", logged_path],
            format_args!(""),
        );
        return None;
    }
    let size = io.step(Operation::Size, 0) as usize;
    log::with_bytes(
        leaves,
        0,
        b"KODoc",
        &[b"Calculating hash for file: ", logged_path],
        format_args!(" (size: {size})"),
    );
    io.step(Operation::DigestBegin, 0);
    let mut sample = Sample {
        io: &mut io,
        bytes: [0; 1024],
        failed: false,
    };
    let total = unsafe {
        xp_core::sync::document::samples((&mut sample as *mut Sample<'_, '_>).cast(), size, read)
    };
    if sample.failed {
        return None;
    }
    let mut digest = [0; 16];
    io.call(Operation::DigestFinish, ptr::null(), digest.as_mut_ptr(), 0);
    let mut text = [0; 33];
    for (byte, chars) in digest.iter().zip(text.as_chunks_mut::<2>().0.iter_mut()) {
        chars[0] = b"0123456789abcdef"[(byte >> 4) as usize];
        chars[1] = b"0123456789abcdef"[(byte & 15) as usize];
    }
    log::with_bytes(
        leaves,
        0,
        b"KODoc",
        &[b"Hash calculated: ", &text[..32]],
        format_args!(" (from {total} bytes)"),
    );
    Some(DocumentId(text))
}

#[cfg(feature = "chapter-test")]
#[path = "../test_support/document.rs"]
mod tests;
