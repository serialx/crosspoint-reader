use super::super::{LoadError, ReaderBook};
use super::*;

// Test-only entry into the shipping gate; the regular app-test archive keeps its
// historical facade oracle. Production never enables chapter-test.
#[no_mangle]
pub unsafe extern "C" fn xp_app_test_protection_open(
    resources: &'static app_abi::Resources,
    leaves: &'static XpEpubLeaves,
    path: app_abi::Text,
    error_sink: app_abi::TextSink,
    context: *mut c_void,
) -> *mut c_void {
    let path = Text::copy(unsafe { path.bytes().unwrap() }).unwrap();
    match Protection::open(resources, leaves, &path) {
        Ok(Protection::Plain) => ptr::null_mut(),
        Ok(Protection::Encrypted(loan)) => {
            let handle = loan.handle.as_ptr();
            core::mem::forget(loan);
            handle
        }
        Err(value) => {
            let message = match &value {
                OpenError::OutOfMemory => b"out of memory".as_slice(),
                OpenError::Message(text) => text.bytes(),
            };
            unsafe {
                error_sink(context, app_abi::Text::from_bytes(message));
            }
            ptr::null_mut()
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn xp_app_test_protection_stream(
    resources: &'static app_abi::Resources,
    leaves: &'static XpEpubLeaves,
    handle: *mut c_void,
    path: app_abi::Text,
    sink: Sink,
    context: *mut c_void,
) -> bool {
    let loan = core::mem::ManuallyDrop::new(ProtectionLoan {
        handle: NonNull::new(handle).unwrap(),
        leaves,
    });
    unsafe { stream(&loan, resources, path.bytes().unwrap(), sink, context) }
}

#[no_mangle]
pub unsafe extern "C" fn xp_app_test_protection_key_wiped() -> bool {
    let mut storage = MaybeUninit::new(Key([0xa5; 32]));
    let address = storage.as_mut_ptr();
    unsafe { ptr::drop_in_place(address) };
    (0..32).all(|index| unsafe { ptr::read_volatile(address.cast::<u8>().add(index)) == 0 })
}

// Full reader load proves that a usable cache cannot bypass the protection gate.
#[no_mangle]
pub unsafe extern "C" fn xp_app_test_protection_load(
    resources: &'static app_abi::Resources,
    leaves: &'static XpEpubLeaves,
    path: app_abi::Text,
    cache: app_abi::Text,
    error_sink: app_abi::TextSink,
    context: *mut c_void,
) -> bool {
    let mut path = Text::copy(unsafe { path.bytes().unwrap() }).unwrap();
    let cache = Text::copy(unsafe { cache.bytes().unwrap() }).unwrap();
    unsafe extern "C" fn now() -> u32 {
        0
    }
    match ReaderBook::load(&mut path, cache, resources, leaves, now, false, true) {
        Ok(_) => true,
        Err(LoadError::Protection(error)) => {
            let message = match &error {
                OpenError::OutOfMemory => b"out of memory".as_slice(),
                OpenError::Message(text) => text.bytes(),
            };
            unsafe {
                error_sink(context, app_abi::Text::from_bytes(message));
            }
            false
        }
        Err(_) => false,
    }
}
