use super::{log, writer::io::FileSlot, File, Text, XpEpubLeaves};
use crate::app::{abi, resources::Buffer};
use core::ffi::c_void;
use xp_core::{
    book::{io::Sink, residual},
    book_io::txt,
};

mod build;
pub(super) use build::{build, cleanup};

#[cfg(feature = "app-test")]
#[path = "../test_support/book_txt.rs"]
mod tests;

pub(super) fn is_text(path: &[u8]) -> bool {
    residual::extension(path, b".txt") || residual::extension(path, b".md")
}

fn tag(path: &[u8]) -> &'static [u8] {
    if residual::extension(path, b".md") {
        b"<!-- MD_CACHE_VERSION: 1 -->"
    } else {
        b"<!-- TXT_CACHE_VERSION: 1 -->"
    }
}

pub(super) unsafe fn stream(
    path: &Text,
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    now: unsafe extern "C" fn() -> u32,
    sink: Sink,
    sink_context: *mut c_void,
) -> bool {
    let start = unsafe { now() };
    let Some(file) = File::open(leaves, path, c"TXT") else {
        log::with_bytes(
            leaves,
            1,
            b"TXT",
            &[b"Failed to open TXT/MD for streaming: ", path.bytes()],
            format_args!(""),
        );
        return false;
    };
    let size = file.size();
    log::with_bytes(
        leaves,
        0,
        b"TXT",
        &[b"Converting TXT/MD to HTML: ", path.bytes()],
        format_args!(" ({} bytes)", size),
    );
    let result = unsafe { convert(path, &file, resources, leaves, sink, sink_context) };
    let elapsed = unsafe { now() }.wrapping_sub(start);
    log::message(
        leaves,
        0,
        b"TXT",
        format_args!(
            "Converted TXT/MD to HTML in {} ms ({} bytes in)",
            elapsed, size
        ),
    );
    result
}

unsafe fn convert(
    path: &Text,
    file: &File,
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    sink: Sink,
    sink_context: *mut c_void,
) -> bool {
    // Preserve both native allocation attempts and default-memory placement.
    let input = Buffer::allocate(resources, abi::MEMORY_DEFAULT, txt::BUFFER_SIZE);
    let output = Buffer::allocate(resources, abi::MEMORY_DEFAULT, txt::BUFFER_SIZE);
    let (Some(input), Some(output)) = (input, output) else {
        log::message(
            leaves,
            1,
            b"TXT",
            format_args!("OOM: TXT/MD HTML streaming buffers"),
        );
        return false;
    };
    unsafe {
        input.as_ptr().write_bytes(0, input.len());
        output.as_ptr().write_bytes(0, output.len());
    }
    struct Context<'a> {
        file: &'a File,
        sink: Sink,
        sink_context: *mut c_void,
    }
    unsafe extern "C" fn read(raw: *mut c_void, bytes: *mut u8, length: usize) -> i32 {
        let ctx = unsafe { &*raw.cast::<Context<'_>>() };
        let Some(handle) = ctx.file.handle else {
            return -1;
        };
        unsafe { (ctx.file.leaves.file_read)(handle.as_ptr(), bytes, length as u32) }
    }
    unsafe extern "C" fn write(raw: *mut c_void, bytes: *const u8, length: usize) -> usize {
        let ctx = unsafe { &*raw.cast::<Context<'_>>() };
        if unsafe { (ctx.sink)(ctx.sink_context, bytes, length) } != 0 {
            length
        } else {
            0
        }
    }
    let mut context = Context {
        file,
        sink,
        sink_context,
    };
    let status = unsafe {
        txt::stream(
            residual::txt_title(path.bytes()),
            tag(path.bytes()),
            &mut *input.as_ptr().cast::<[u8; txt::BUFFER_SIZE]>(),
            &mut *output.as_ptr().cast::<[u8; txt::BUFFER_SIZE]>(),
            (&mut context as *mut Context<'_>).cast(),
            read,
            write,
        )
    };
    match status {
        0 => true,
        -1 => {
            log::with_bytes(
                leaves,
                1,
                b"TXT",
                &[b"Read error while streaming TXT/MD: ", path.bytes()],
                format_args!(""),
            );
            false
        }
        _ => {
            log::message(
                leaves,
                1,
                b"TXT",
                format_args!("Failed to stream complete HTML (write error or disk full)"),
            );
            false
        }
    }
}

pub(super) fn invalidate(cache_path: &Text, leaves: &'static XpEpubLeaves) -> Result<(), ()> {
    for suffix in [b"/html".as_slice(), b"/sections"] {
        let mut path = Text::default();
        path.assign_parts(&[cache_path.bytes(), suffix])
            .map_err(|_| ())?;
        unsafe { (leaves.path_remove_tree)(path.wire()) };
    }
    Ok(())
}

pub(super) fn validate(
    path: &Text,
    cache_path: &Text,
    cached: u32,
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
) -> bool {
    struct Context<'a> {
        path: &'a Text,
        cache_path: &'a Text,
        html: Text,
        cached: u32,
        resources: &'static abi::Resources,
        leaves: &'static XpEpubLeaves,
        file: FileSlot,
    }
    unsafe extern "C" fn control(raw: *mut c_void, operation: u32, _: i32, _: i32) -> bool {
        use txt::ValidateAction as A;
        let ctx = unsafe { &mut *raw.cast::<Context<'_>>() };
        match operation {
            x if x == A::Exists as u32 => unsafe {
                (ctx.resources.path_exists)(ctx.html.wire()) != 0
            },
            x if x == A::OpenHtml as u32 => ctx.file.open(ctx.leaves, &ctx.html, false, c"TXT"),
            x if x == A::OpenRaw as u32 => ctx.file.open(ctx.leaves, ctx.path, false, c"TXT"),
            x if x == A::CloseHtml as u32 || x == A::CloseRaw as u32 => {
                ctx.file.close();
                true
            }
            x if x == A::Invalidate as u32 => invalidate(ctx.cache_path, ctx.leaves).is_ok(),
            _ => false,
        }
    }
    unsafe extern "C" fn read(raw: *mut c_void, bytes: *mut u8, length: usize) -> i32 {
        unsafe { (&mut *raw.cast::<Context<'_>>()).file.read(bytes, length) }
    }
    unsafe extern "C" fn size(raw: *mut c_void) -> usize {
        unsafe { (&*raw.cast::<Context<'_>>()).file.size() as usize }
    }
    unsafe extern "C" fn diagnostic(raw: *mut c_void, event: u32, size: usize) {
        use txt::FileDiagnostic as D;
        let ctx = unsafe { &*raw.cast::<Context<'_>>() };
        match event {
            x if x == D::VersionMismatch as u32 => log::with_bytes(
                ctx.leaves,
                0,
                b"TXT",
                &[
                    b"HTML cache version mismatch or missing, invalidating: ",
                    ctx.html.bytes(),
                ],
                format_args!(""),
            ),
            x if x == D::SizeChanged as u32 => log::message(
                ctx.leaves,
                0,
                b"TXT",
                format_args!(
                    "File size changed ({} vs cached {}), invalidating cache",
                    size as u32, ctx.cached
                ),
            ),
            x if x == D::InvalidCache as u32 => log::with_bytes(
                ctx.leaves,
                0,
                b"TXT",
                &[
                    b"Cache invalid for ",
                    ctx.path.bytes(),
                    b", wiping html and sections",
                ],
                format_args!(""),
            ),
            _ => (),
        }
    }
    let mut html = Text::default();
    if html
        .assign_parts(&[cache_path.bytes(), b"/html/0.html"])
        .is_err()
    {
        return false;
    }
    let mut context = Context {
        path,
        cache_path,
        html,
        cached,
        resources,
        leaves,
        file: FileSlot::new(),
    };
    let services = txt::FileServices {
        context: (&mut context as *mut Context<'_>).cast(),
        control: Some(control),
        read: Some(read),
        write: None,
        size: Some(size),
        diagnostic: Some(diagnostic),
    };
    unsafe { txt::validate(&services, cached as usize, tag(path.bytes())) }
}
