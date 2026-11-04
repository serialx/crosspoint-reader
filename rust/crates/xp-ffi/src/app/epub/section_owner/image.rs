//! Image scratch and framebuffer loans end before publishing the resulting paths.
extern crate alloc;
use super::super::{
    abi::XpEpubLeaves,
    book_owner::{log, queries, FileSlot},
    Text,
};
use crate::app::abi;
use alloc::vec::Vec;
use core::{cell::Cell, ffi::c_void, ptr::NonNull};
use xp_core::{
    chapter::assembly::{Error, ImageProbe},
    image::probe::Probe,
};

// Physical decoder and framebuffer services never borrow the build owner.
#[derive(Clone, Copy)]
#[repr(C)]
pub(super) struct Physical {
    pub supported: unsafe extern "C" fn(abi::Text) -> u8,
    pub dimensions: unsafe extern "C" fn(abi::Text, *mut u16, *mut u16) -> u8,
    pub flush: unsafe extern "C" fn(*mut c_void),
    pub lend: unsafe extern "C" fn() -> u8,
    pub restore: unsafe extern "C" fn(u8),
    pub delay: unsafe extern "C" fn(u32),
}

pub(in crate::app::epub) struct FramebufferLoan {
    token: Cell<u8>,
    restore: unsafe extern "C" fn(u8),
}
impl FramebufferLoan {
    pub(in crate::app::epub) fn new(
        lend: unsafe extern "C" fn() -> u8,
        restore: unsafe extern "C" fn(u8),
    ) -> Self {
        Self {
            token: Cell::new(unsafe { lend() }),
            restore,
        }
    }
    pub(in crate::app::epub) fn end(&self) {
        let token = self.token.replace(0);
        if token != 0 {
            unsafe { (self.restore)(token) };
        }
    }
}
impl Drop for FramebufferLoan {
    fn drop(&mut self) {
        self.end();
    }
}

#[derive(Default)]
pub(super) struct Paths {
    pub cache: Text,
    pub source: Text,
}

fn resolve(base: &Text, source: &[u8]) -> Result<Text, Error> {
    let mut combined = Text::default();
    combined
        .assign_parts(&[base.bytes(), source])
        .map_err(|_| Error::Allocation)?;
    let mut decoded = Vec::new();
    decoded
        .try_reserve_exact(combined.bytes().len())
        .map_err(|_| Error::Allocation)?;
    xp_core::epub::decode_href(&mut decoded, combined.bytes()).map_err(|_| Error::Allocation)?;
    drop(combined);
    let mut normalized = Vec::new();
    normalized
        .try_reserve_exact(decoded.len())
        .map_err(|_| Error::Allocation)?;
    xp_core::epub::normalize_href(&mut normalized, &decoded).map_err(|_| Error::Allocation)?;
    drop(decoded);
    Text::copy(&normalized).map_err(|_| Error::Allocation)
}

unsafe fn dimensions(book: NonNull<c_void>, path: &Text) -> Option<(u16, u16)> {
    unsafe extern "C" fn sink(raw: *mut c_void, data: *const u8, length: usize) -> u8 {
        let bytes = if length == 0 {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(data, length) }
        };
        (unsafe { (&mut *raw.cast::<Probe>()).write(bytes) } == length).into()
    }
    let mut probe = Probe::default();
    // A short probe write is the existing intentional early-stop path.
    unsafe {
        queries::stream(
            book.as_ptr(),
            path.wire(),
            1024,
            1,
            sink,
            (&mut probe as *mut Probe).cast(),
        )
    };
    probe
        .dimensions()
        .map(|(width, height)| (width as u16, height as u16))
}

#[allow(clippy::too_many_arguments)] // Borrows disjoint book, path, physical and popup owners.
pub(super) unsafe fn probe(
    book: NonNull<c_void>,
    leaves: &'static XpEpubLeaves,
    physical: &Physical,
    base: &Text,
    image_base: &Text,
    source: &[u8],
    counter: &mut i32,
    shown: &mut bool,
    popup: Option<&dyn Fn()>,
) -> Result<(ImageProbe<()>, Option<Paths>), Error> {
    log::with_bytes(
        leaves,
        0,
        b"EHP",
        &[b"Found image: src=", source],
        format_args!(""),
    );
    let resolved = resolve(base, source)?;
    if unsafe { (physical.supported)(resolved.wire()) } == 0 {
        return Ok((ImageProbe::Unsupported, None));
    }
    let extension = resolved
        .bytes()
        .iter()
        .rposition(|&byte| byte == b'.')
        .unwrap_or(resolved.bytes().len());
    let mut cached = Text::default();
    let mut digits = [0; 20];
    cached
        .assign_parts(&[
            image_base.bytes(),
            if *counter < 0 { b"-" } else { b"" },
            queries::decimal(u64::from(counter.unsigned_abs()), &mut digits),
            &resolved.bytes()[extension..],
        ])
        .map_err(|_| Error::Allocation)?;
    *counter = counter.wrapping_add(1);
    let mut size = unsafe { dimensions(book, &resolved) };
    if size.is_none() {
        let _loan = FramebufferLoan::new(physical.lend, physical.restore);
        size = unsafe { dimensions(book, &resolved) };
    }
    if size.is_none() {
        if let Some(popup) = popup.filter(|_| !*shown) {
            *shown = true;
            popup();
        }
        let mut file = FileSlot::new();
        let extracted = if file.open(leaves, &cached, true, c"EHP") {
            unsafe extern "C" fn sink(raw: *mut c_void, data: *const u8, length: usize) -> u8 {
                (unsafe { (&mut *raw.cast::<FileSlot>()).write(data, length) } == length).into()
            }
            let extracted = {
                let _loan = FramebufferLoan::new(physical.lend, physical.restore);
                unsafe {
                    queries::stream(
                        book.as_ptr(),
                        resolved.wire(),
                        4096,
                        0,
                        sink,
                        (&mut file as *mut FileSlot).cast(),
                    ) != 0
                }
            };
            file.flush(physical.flush);
            file.close();
            extracted
        } else {
            false
        };
        if extracted {
            let decoder = unsafe { (physical.supported)(cached.wire()) != 0 };
            let (mut width, mut height) = (0, 0);
            for attempt in 0..3 {
                if attempt > 0 {
                    unsafe { (physical.delay)(50) };
                }
                if decoder
                    && unsafe { (physical.dimensions)(cached.wire(), &mut width, &mut height) == 2 }
                {
                    size = Some((width, height));
                    break;
                }
            }
            // Failed decoders can still publish their dimensions in the parent.
            if size.is_none() {
                log::message(
                    leaves,
                    1,
                    b"EHP",
                    format_args!("Failed to get image dimensions"),
                );
                unsafe { (leaves.path_remove)(cached.wire()) };
                return Ok((ImageProbe::Failed { width, height }, None));
            }
        } else {
            log::message(leaves, 1, b"EHP", format_args!("Failed to extract image"));
        }
    }
    if let Some((width, height)) = size {
        log::message(
            leaves,
            0,
            b"EHP",
            format_args!("Image dimensions: {}x{}", width as i16, height as i16),
        );
        Ok((
            ImageProbe::Ready {
                resource: (),
                width,
                height,
            },
            Some(Paths {
                cache: cached,
                source: resolved,
            }),
        ))
    } else {
        log::message(
            leaves,
            1,
            b"EHP",
            format_args!("Failed to get image dimensions"),
        );
        unsafe { (leaves.path_remove)(cached.wire()) };
        Ok((
            ImageProbe::Failed {
                width: 0,
                height: 0,
            },
            None,
        ))
    }
}
