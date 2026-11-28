use super::{log, Extract, XpEpubLeaves};
use core::ffi::c_void;
use xp_core::book_io::zip_read::Event;

pub(super) unsafe extern "C" fn extract(
    raw: *mut c_void,
    event: u32,
    a: u32,
    b: u32,
    _: u32,
    _: u32,
) {
    let ctx = raw.cast::<Extract<'_>>();
    let (leaves, name) = unsafe { ((*ctx).leaves, (*ctx).name) };
    message(leaves, name, event, a, b);
}

pub(super) fn message(leaves: &XpEpubLeaves, name: &[u8], event: u32, a: u32, b: u32) {
    macro_rules! emit {
        ($severity:expr, $($argument:tt)*) => { log::message(leaves, $severity, b"ZIP", format_args!($($argument)*)) };
    }
    match event {
        x if x == Event::ReadCompressed as u32 => {
            emit!(1, "Failed to read compressed data: {}", a as i32)
        }
        x if x == Event::OutputAllocation as u32 => emit!(
            1,
            "Failed to allocate memory for output buffer ({} bytes)",
            a
        ),
        x if x == Event::ReadData as u32 => emit!(1, "Failed to read data"),
        x if x == Event::InputAllocation as u32 => {
            emit!(1, "Failed to allocate memory for zip file read buffer")
        }
        x if x == Event::Init as u32 => {
            let name = name.split(|&byte| byte == 0).next().unwrap_or_default();
            log::with_bytes(
                leaves,
                1,
                b"ZIP",
                &[b"Failed to init inflate stream for ", name],
                format_args!(""),
            );
        }
        x if x == Event::Inflate as u32 => emit!(1, "Failed to inflate file"),
        x if x == Event::Method as u32 => emit!(1, "Unsupported compression method"),
        x if x == Event::BufferAllocation as u32 => {
            emit!(1, "Failed to allocate memory for buffer")
        }
        x if x == Event::ReadMore as u32 => emit!(1, "Could not read more bytes"),
        x if x == Event::Write as u32 => emit!(1, "Failed to write all output bytes to stream"),
        x if x == Event::StreamOutputAllocation as u32 => {
            emit!(1, "Failed to allocate memory for output buffer")
        }
        x if x == Event::Exceeds as u32 => {
            emit!(1, "Decompressed size exceeds expected ({} > {})", a, b)
        }
        x if x == Event::Mismatch as u32 => {
            emit!(1, "Decompressed size mismatch (expected {}, got {})", a, b)
        }
        x if x == Event::Decompressed as u32 => {
            emit!(0, "Decompressed {} bytes into {} bytes", a as i32, b as i32)
        }
        x if x == Event::Failed as u32 => emit!(1, "Decompression failed"),
        _ => (),
    }
}
