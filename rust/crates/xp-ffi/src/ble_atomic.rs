//! ESP32-C3 lacks Rust target atomics; use the platform compiler's atomic ABI.
use core::cell::UnsafeCell;
use core::sync::atomic::Ordering;
extern "C" {
    fn xp_ble_atomic_load8(value: *const u8, order: i32) -> u8;
    fn xp_ble_atomic_store8(value: *mut u8, desired: u8, order: i32);
    fn xp_ble_atomic_swap8(value: *mut u8, desired: u8, order: i32) -> u8;
    fn xp_ble_atomic_cas8(
        value: *mut u8,
        expected: *mut u8,
        desired: u8,
        success: i32,
        failure: i32,
    ) -> bool;
}
fn order(o: Ordering) -> i32 {
    match o {
        Ordering::Relaxed => 0,
        Ordering::Acquire => 2,
        Ordering::Release => 3,
        Ordering::AcqRel => 4,
        _ => 5,
    }
}
pub struct AtomicU8(UnsafeCell<u8>);
// Every access goes through the platform atomic builtins, including loads/stores.
unsafe impl Sync for AtomicU8 {}
impl AtomicU8 {
    pub const fn new(value: u8) -> Self {
        Self(UnsafeCell::new(value))
    }
    pub fn load(&self, o: Ordering) -> u8 {
        unsafe { xp_ble_atomic_load8(self.0.get(), order(o)) }
    }
    pub fn store(&self, value: u8, o: Ordering) {
        unsafe {
            xp_ble_atomic_store8(self.0.get(), value, order(o));
        }
    }
}
pub struct AtomicBool(AtomicU8);
impl AtomicBool {
    pub const fn new(value: bool) -> Self {
        Self(AtomicU8::new(value as u8))
    }
    pub fn load(&self, o: Ordering) -> bool {
        self.0.load(o) != 0
    }
    pub fn store(&self, value: bool, o: Ordering) {
        self.0.store(u8::from(value), o);
    }
    pub fn swap(&self, value: bool, o: Ordering) -> bool {
        unsafe { xp_ble_atomic_swap8(self.0 .0.get(), u8::from(value), order(o)) != 0 }
    }
    pub fn compare_exchange(
        &self,
        current: bool,
        new: bool,
        success: Ordering,
        failure: Ordering,
    ) -> Result<bool, bool> {
        let mut expected = u8::from(current);
        if unsafe {
            xp_ble_atomic_cas8(
                self.0 .0.get(),
                &mut expected,
                u8::from(new),
                order(success),
                order(failure),
            )
        } {
            Ok(current)
        } else {
            Err(expected != 0)
        }
    }
}
