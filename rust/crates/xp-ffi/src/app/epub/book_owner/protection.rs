use super::{
    app_abi, log, package, zip::Archive, OpenError, Protection, ProtectionLoan, Text, XpEpubLeaves,
};
use crate::hal::secret;
use core::{
    ffi::c_void,
    mem::MaybeUninit,
    ptr::{self, NonNull},
};
use xp_core::{
    book::io::Sink,
    book_io::key,
    font::pressure::{self, Pressure},
};

#[repr(u8)]
enum IndexStage {
    Construct,
    Scan,
    ProbeOpen,
    ProbeClose,
}
#[repr(u8)]
enum FinishStage {
    InstallKey,
    Take,
    Discard,
}
#[repr(u8)]
enum StreamStage {
    Prepare,
    Read,
}
#[repr(u8)]
enum KeyOperation {
    Begin,
    End,
    BorrowSecret,
    ReleaseSecret,
}

struct Caches(*const c_void);
impl Pressure for Caches {
    fn free_default(&mut self) -> usize {
        0
    }
    fn render(&mut self) -> usize {
        unsafe { crate::font_runtime::release(self.0.cast(), 2) }
    }
    fn sd(&mut self) -> usize {
        unsafe { crate::font_runtime::release(self.0.cast(), 3) }
    }
    fn vector(&mut self) -> usize {
        #[cfg(feature = "vector-fonts")]
        return unsafe { crate::font_runtime::release(self.0.cast(), 4) };
        #[cfg(not(feature = "vector-fonts"))]
        0
    }
}
fn reclaim(resources: &app_abi::Resources, leaves: &XpEpubLeaves) {
    unsafe extern "C" {
        fn heap_caps_get_largest_free_block(caps: u32) -> usize;
    }
    let before = unsafe { heap_caps_get_largest_free_block(1 << 2) };
    if before >= 64 * 1024 {
        return;
    }
    pressure::clear_all(&mut Caches(unsafe { resources.font_owner.get().cast() }));
    let mut heap = super::super::abi::XpEpubHeap {
        free_bytes: 0,
        largest_block: 0,
    };
    unsafe { (leaves.heap_snapshot)(&mut heap) };
    log::message(
        leaves,
        0,
        b"CPRO",
        format_args!(
            "Cache reclaim: max_block={} -> {}, free={}",
            before, heap.largest_block, heap.free_bytes
        ),
    );
}

fn error(message: &[u8]) -> OpenError {
    let mut text = Text::default();
    if text.assign(message).is_err() {
        OpenError::OutOfMemory
    } else {
        OpenError::Message(text)
    }
}
fn probe(
    resources: &'static app_abi::Resources,
    leaves: &'static XpEpubLeaves,
    path: &Text,
) -> Result<bool, ()> {
    const MANIFEST: &[u8] = b"META-INF/encryption.xml";
    // Keep the native inline-file lifetime; lookup needs no remembered name.
    let mut storage = [0usize; if size_of::<usize>() == 4 { 3 } else { 8 }];
    let handle = NonNull::from(&mut storage).cast();
    let opened = unsafe {
        (leaves.protection_index)(handle.as_ptr(), IndexStage::ProbeOpen as u8, path.wire())
    } != 0;
    let file = core::mem::ManuallyDrop::new(super::File {
        handle: Some(handle),
        leaves,
    });
    let found = opened && Archive::contains_in_file(path, resources, leaves, &file, MANIFEST);
    unsafe {
        (leaves.protection_index)(handle.as_ptr(), IndexStage::ProbeClose as u8, path.wire());
    }
    if !found {
        return Ok(false);
    }
    let mut document =
        package::Document::new(leaves, package::Kind::Encryption, b"", 0, false, None)?;
    unsafe extern "C" fn write(raw: *mut c_void, bytes: *const u8, length: usize) -> u8 {
        let doc = unsafe { &mut *raw.cast::<package::Document<'_, '_>>() };
        let diagnostics = package::diagnostics::services(doc.leaves);
        u8::from(
            doc.write(
                unsafe { core::slice::from_raw_parts(bytes, length) },
                &diagnostics,
            ) == length,
        )
    }
    let ok = unsafe {
        Archive::new(path, resources, leaves).stream(
            MANIFEST,
            512,
            false,
            write,
            (&mut document as *mut package::Document<'_, '_>).cast(),
        )
    };
    if !ok || !document.finish(&package::diagnostics::services(leaves)) {
        return Err(());
    }
    Ok(document.needs_protection())
}
struct Index {
    storage: [usize; 24],
    leaves: &'static XpEpubLeaves,
    live: bool,
}
impl Drop for Index {
    fn drop(&mut self) {
        if self.live {
            unsafe {
                (self.leaves.protection_finish)(
                    self.storage.as_mut_ptr().cast(),
                    FinishStage::Discard as u8,
                    ptr::null(),
                    0,
                );
            }
        }
    }
}
struct Key([u8; 32]);
impl Drop for Key {
    fn drop(&mut self) {
        for byte in &mut self.0 {
            unsafe { ptr::write_volatile(byte, 0) };
        }
    }
}
#[repr(C)]
struct Secret {
    state: *mut secret::Secret,
    services: secret::Services,
}
struct KeyPlatform<'a> {
    storage: [usize; 24],
    leaves: &'a XpEpubLeaves,
}
impl Drop for KeyPlatform<'_> {
    fn drop(&mut self) {
        unsafe {
            (self.leaves.key_platform)(
                self.storage.as_mut_ptr().cast(),
                KeyOperation::End as u8,
                app_abi::Text::from_bytes(b""),
                ptr::null_mut(),
            )
        };
    }
}
fn read_key(leaves: &XpEpubLeaves, path: &Text, key: &mut Key, expires: &mut i64) -> usize {
    let mut platform = KeyPlatform {
        storage: [0; 24],
        leaves,
    };
    let mut services = MaybeUninit::<key::Services>::uninit();
    unsafe {
        (leaves.key_platform)(
            platform.storage.as_mut_ptr().cast(),
            KeyOperation::Begin as u8,
            path.wire(),
            services.as_mut_ptr().cast(),
        )
    };
    let services = unsafe { services.assume_init() };
    let result = unsafe {
        key::read_with_secret(key.0.as_mut_ptr(), expires, &services, |out| {
            let mut secret = MaybeUninit::<Secret>::uninit();
            (leaves.key_platform)(
                platform.storage.as_mut_ptr().cast(),
                KeyOperation::BorrowSecret as u8,
                path.wire(),
                secret.as_mut_ptr().cast(),
            );
            let secret = secret.assume_init();
            let ok = secret::xp_hal_secret(secret.state, &secret.services, out);
            if !ok {
                log::message(
                    leaves,
                    1,
                    b"DSEC",
                    format_args!("Device secret unavailable"),
                );
            }
            (leaves.key_platform)(
                platform.storage.as_mut_ptr().cast(),
                KeyOperation::ReleaseSecret as u8,
                path.wire(),
                ptr::null_mut(),
            );
            ok
        })
    };
    if result < 0 {
        log::with_tail(
            leaves,
            1,
            b"BKEY",
            format_args!("Key not wrapped by this device or tampered: "),
            &[path.bytes()],
        );
        0
    } else {
        result as usize
    }
}

impl Protection {
    pub(super) fn open(
        resources: &'static app_abi::Resources,
        leaves: &'static XpEpubLeaves,
        path: &Text,
    ) -> Result<Self, OpenError> {
        let mut heap = super::super::abi::XpEpubHeap {
            free_bytes: 0,
            largest_block: 0,
        };
        unsafe { (leaves.heap_snapshot)(&mut heap) };
        log::message(
            leaves,
            2,
            b"CPRO",
            format_args!(
                "open: free={} max_block={}",
                heap.free_bytes, heap.largest_block
            ),
        );
        match probe(resources, leaves, path) {
            Ok(false) => return Ok(Self::Plain),
            Err(()) => {
                log::message(
                    leaves,
                    1,
                    b"CPRO",
                    format_args!("Cannot read encryption manifest"),
                );
                return Err(error(b"cannot read encryption manifest"));
            }
            Ok(true) => {}
        }
        let mut index = Index {
            storage: [0; 24],
            leaves,
            live: true,
        };
        unsafe {
            (leaves.protection_index)(
                index.storage.as_mut_ptr().cast(),
                IndexStage::Construct as u8,
                path.wire(),
            );
        }
        reclaim(resources, leaves);
        match unsafe {
            (leaves.protection_index)(
                index.storage.as_mut_ptr().cast(),
                IndexStage::Scan as u8,
                path.wire(),
            )
        } {
            1 => {}
            4 => return Ok(Self::Plain),
            2 => return Err(error(b"out of memory")),
            3 => return Err(error(b"cannot open protected content")),
            _ => return Err(error(b"cannot index protected content")),
        }
        let mut key = Key([0; 32]);
        let mut expires = 0;
        let length = read_key(leaves, path, &mut key, &mut expires);
        if length == 0 {
            return Err(error(b"no content key for this book"));
        }
        unsafe {
            (leaves.protection_finish)(
                index.storage.as_mut_ptr().cast(),
                FinishStage::InstallKey as u8,
                key.0.as_ptr(),
                length,
            );
        }
        unsafe extern "C" fn now(raw: *mut c_void) -> i64 {
            unsafe { super::super::persistence::now(&*raw.cast::<XpEpubLeaves>()) }
        }
        match unsafe {
            crate::book_io::xp_book_loan(
                expires,
                (leaves as *const XpEpubLeaves).cast_mut().cast(),
                now,
            )
        } {
            0 => {}
            1 => return Err(error(b"loan date unverified")),
            _ => return Err(error(b"access expired")),
        }
        let handle = unsafe {
            (leaves.protection_finish)(
                index.storage.as_mut_ptr().cast(),
                FinishStage::Take as u8,
                ptr::null(),
                0,
            )
        };
        index.live = false;
        let handle = NonNull::new(handle).ok_or_else(|| error(b"out of memory"))?;
        Ok(Self::Encrypted(ProtectionLoan { handle, leaves }))
    }
}

pub(super) unsafe fn stream(
    loan: &ProtectionLoan,
    resources: &app_abi::Resources,
    path: &[u8],
    sink: Sink,
    context: *mut c_void,
) -> bool {
    let mut storage = [0usize; 6];
    let stream = loan.leaves.protection_stream;
    let path = app_abi::Text::from_bytes(path);
    if unsafe {
        stream(
            loan.handle.as_ptr(),
            storage.as_mut_ptr().cast(),
            StreamStage::Prepare as u8,
            path,
            sink,
            context,
        )
    } == 0
    {
        return false;
    }
    reclaim(resources, loan.leaves);
    unsafe {
        stream(
            loan.handle.as_ptr(),
            storage.as_mut_ptr().cast(),
            StreamStage::Read as u8,
            path,
            sink,
            context,
        ) != 0
    }
}

#[cfg(feature = "chapter-test")]
#[path = "../test_support/protection.rs"]
mod test_support;
