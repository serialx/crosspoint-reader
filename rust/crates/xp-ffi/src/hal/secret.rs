use core::{ffi::c_void, ptr};

#[repr(C)]
pub struct Secret {
    bytes: [u8; 32],
    loaded: bool,
}
#[repr(C)]
pub struct Services {
    ctx: *mut c_void,
    open: unsafe extern "C" fn(*mut c_void) -> bool,
    read: unsafe extern "C" fn(*mut c_void, *mut u8) -> u8,
    random: unsafe extern "C" fn(*mut c_void, *mut u8),
    write: unsafe extern "C" fn(*mut c_void, *const u8) -> bool,
    commit: unsafe extern "C" fn(*mut c_void) -> bool,
    close: unsafe extern "C" fn(*mut c_void),
}
// A successfully opened NVS handle is always closed, including every failure.
struct OpenNvs<'a>(&'a Services);
impl Drop for OpenNvs<'_> {
    fn drop(&mut self) {
        unsafe { (self.0.close)(self.0.ctx) }
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_secret(
    state: *mut Secret,
    services: *const Services,
    out: *mut u8,
) -> bool {
    let (Some(state), Some(s)) = (state.as_mut(), services.as_ref()) else {
        return false;
    };
    if out.is_null() {
        return false;
    }
    if !state.loaded && (s.open)(s.ctx) {
        let _handle = OpenNvs(s);
        state.loaded = match (s.read)(s.ctx, state.bytes.as_mut_ptr()) {
            0 => true,
            1 => {
                (s.random)(s.ctx, state.bytes.as_mut_ptr());
                (s.write)(s.ctx, state.bytes.as_ptr()) && (s.commit)(s.ctx)
            }
            _ => false,
        };
    }
    if !state.loaded {
        return false;
    }
    ptr::copy_nonoverlapping(state.bytes.as_ptr(), out, 32);
    true
}

const _: () = assert!(core::mem::size_of::<Secret>() == 33);
