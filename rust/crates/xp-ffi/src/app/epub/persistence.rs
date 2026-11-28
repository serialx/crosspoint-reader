use super::{abi::XpEpubLeaves, app_abi, book_owner::log, Text};
use core::{ffi::c_void, ptr};
use xp_ui::reader::{write_progress, ProgressIo, ProgressStatus};

struct Progress<'a> {
    leaves: &'a XpEpubLeaves,
    final_path: Text,
    temporary: Text,
    file: *mut c_void,
}
impl Drop for Progress<'_> {
    fn drop(&mut self) {
        if !self.file.is_null() {
            unsafe { (self.leaves.file_close)(self.file) };
        }
    }
}

pub(super) fn save_progress(leaves: &XpEpubLeaves, cache: &Text, bytes: &[u8]) -> bool {
    let mut progress = Progress {
        leaves,
        final_path: Text::default(),
        temporary: Text::default(),
        file: ptr::null_mut(),
    };
    if progress
        .final_path
        .assign_parts(&[cache.bytes(), b"/progress.bin"])
        .is_err()
        || progress
            .temporary
            .assign_parts(&[cache.bytes(), b"/progress.bin.tmp"])
            .is_err()
    {
        return false;
    }
    unsafe fn context<'a>(raw: *mut c_void) -> &'a mut Progress<'a> {
        unsafe { &mut *raw.cast::<Progress<'a>>() }
    }
    unsafe extern "C" fn open(raw: *mut c_void) -> u8 {
        let p = unsafe { context(raw) };
        p.file = unsafe {
            (p.leaves.file_open_write)(
                p.temporary.wire(),
                app_abi::Text::from_terminated(c"PRG".to_bytes_with_nul()),
            )
        };
        u8::from(!p.file.is_null())
    }
    unsafe extern "C" fn write(raw: *mut c_void, data: *const u8, len: u32) -> u32 {
        let p = unsafe { context(raw) };
        unsafe { (p.leaves.file_write)(p.file, data, len) }
    }
    unsafe extern "C" fn flush(raw: *mut c_void) {
        let p = unsafe { context(raw) };
        unsafe { (p.leaves.file_flush)(p.file) };
    }
    unsafe extern "C" fn close(raw: *mut c_void) {
        let p = unsafe { context(raw) };
        unsafe { (p.leaves.file_close)(p.file) };
        p.file = ptr::null_mut();
    }
    unsafe extern "C" fn remove(raw: *mut c_void) {
        let p = unsafe { context(raw) };
        unsafe { (p.leaves.path_remove)(p.final_path.wire()) };
    }
    unsafe extern "C" fn rename(raw: *mut c_void) -> u8 {
        let p = unsafe { context(raw) };
        unsafe { (p.leaves.path_rename)(p.temporary.wire(), p.final_path.wire()) }
    }
    let io = ProgressIo {
        context: (&mut progress as *mut Progress<'_>).cast(),
        open_temp: Some(open),
        write_temp: Some(write),
        flush_temp: Some(flush),
        close_temp: Some(close),
        remove_final: Some(remove),
        rename_temp: Some(rename),
    };
    let (status, written) = unsafe { write_progress(io, bytes.as_ptr(), bytes.len() as u32) };
    match status {
        ProgressStatus::Ok => return true,
        ProgressStatus::OpenFailed => log::with_tail(
            leaves,
            1,
            b"PRG",
            format_args!("Could not open temp progress file for write: "),
            &[progress.temporary.bytes()],
        ),
        ProgressStatus::ShortWrite => log::with_tail(
            leaves,
            1,
            b"PRG",
            format_args!("Short write saving progress to "),
            &[
                progress.temporary.bytes(),
                b": ",
                decimal(written, &mut [0; 10]),
                b"/",
                decimal(bytes.len() as u32, &mut [0; 10]),
                b" bytes",
            ],
        ),
        ProgressStatus::RenameFailed => log::with_tail(
            leaves,
            1,
            b"PRG",
            format_args!("Failed to rename temp progress into place: "),
            &[progress.final_path.bytes()],
        ),
        ProgressStatus::Invalid => log::with_tail(
            leaves,
            1,
            b"PRG",
            format_args!("Invalid progress write"),
            &[],
        ),
    }
    false
}
fn decimal(mut value: u32, out: &mut [u8; 10]) -> &[u8] {
    let mut start = out.len();
    loop {
        start -= 1;
        out[start] = b'0' + (value % 10) as u8;
        value /= 10;
        if value == 0 {
            return &out[start..];
        }
    }
}

pub(super) fn read_file<T>(
    leaves: &XpEpubLeaves,
    path: &Text,
    consume: impl FnOnce(&[u8]) -> T,
) -> Option<T> {
    // Holds the native String in place until its borrowed bytes have been consumed.
    let mut storage = core::mem::MaybeUninit::<[usize; 6]>::uninit();
    let bytes = unsafe { (leaves.storage_read_file)(path.wire(), storage.as_mut_ptr().cast()) };
    let result = unsafe { bytes.bytes() }
        .filter(|bytes| !bytes.is_empty())
        .map(consume);
    unsafe { (leaves.storage_read_release)(storage.as_mut_ptr().cast()) };
    result
}

#[cfg(not(feature = "app-test"))]
pub(super) unsafe fn now(leaves: &XpEpubLeaves) -> i64 {
    unsafe { crate::hal::time::xp_hal_time_now((leaves.time_services)()) }
}
#[cfg(not(feature = "app-test"))]
pub(super) unsafe fn sync_time(leaves: &XpEpubLeaves, timeout: u32) -> bool {
    let sync = unsafe { (leaves.time_start_sync)() };
    unsafe { crate::hal::time::xp_hal_time_sync((leaves.time_services)(), &sync, timeout) }
}
#[cfg(feature = "app-test")]
pub(super) unsafe fn now(leaves: &XpEpubLeaves) -> i64 {
    unsafe { (leaves.trusted_now)() }
}
#[cfg(feature = "app-test")]
pub(super) unsafe fn sync_time(leaves: &XpEpubLeaves, timeout: u32) -> bool {
    unsafe { (leaves.trusted_sync)(timeout) != 0 }
}
