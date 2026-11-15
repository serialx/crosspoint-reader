use core::{ffi::c_void, ptr::NonNull};
#[repr(C)]
pub struct ReadServices {
    ctx: *mut c_void,
    open: unsafe extern "C" fn(*mut c_void) -> bool,
    is_directory: unsafe extern "C" fn(*mut c_void) -> bool,
    size: unsafe extern "C" fn(*mut c_void) -> usize,
    largest_block: unsafe extern "C" fn(*mut c_void) -> usize,
    read_string: unsafe extern "C" fn(*mut c_void, usize) -> i32,
}
// The facade retains ownership and closes the HalFile under storageMutex.
struct BorrowedFile<'a>(&'a ReadServices);
impl<'a> BorrowedFile<'a> {
    unsafe fn open(s: &'a ReadServices) -> Option<Self> {
        if (s.open)(s.ctx) {
            Some(Self(s))
        } else {
            None
        }
    }
    unsafe fn size(&self) -> usize {
        (self.0.size)(self.0.ctx)
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_read_string(services: *const ReadServices, cap: usize) -> u32 {
    let Some(s) = services.as_ref() else { return 0 };
    let Some(file) = BorrowedFile::open(s) else {
        return 0;
    };
    if (s.is_directory)(s.ctx) {
        return 0;
    }
    let size = file.size();
    if size == 0 || size > cap {
        return 0;
    }
    if (s.largest_block)(s.ctx) < size.wrapping_add(512) {
        return 2;
    }
    u32::from((s.read_string)(s.ctx, size) == size as i32)
}
#[repr(C)]
pub struct BlockDevice {
    _opaque: [u8; 0],
}
#[repr(C)]
pub struct UsbServices {
    ctx: *mut c_void,
    detach: unsafe extern "C" fn(*mut c_void) -> *mut BlockDevice,
    begin: unsafe extern "C" fn(*mut c_void, *mut BlockDevice) -> bool,
    remount: unsafe extern "C" fn(*mut c_void) -> bool,
    diagnostic: super::Diagnostic,
}
impl UsbServices {
    unsafe fn diag(&self, event: u32) {
        if let Some(d) = self.diagnostic {
            d(self.ctx, event, 0, 0, 0, 0);
        }
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_usb_begin(services: *const UsbServices) -> bool {
    let Some(s) = services.as_ref() else {
        return false;
    };
    let Some(device) = NonNull::new((s.detach)(s.ctx)) else {
        s.diag(0);
        return false;
    };
    if !(s.begin)(s.ctx, device.as_ptr()) {
        s.diag(1);
        if !(s.remount)(s.ctx) {
            s.diag(2);
        }
        return false;
    }
    true
}
