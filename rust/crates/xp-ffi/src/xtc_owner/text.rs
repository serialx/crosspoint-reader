use super::abi::XpXtcLeaves;
use core::{mem, ptr};

#[repr(C)]
union Storage {
    local: [u8; 16],
    capacity: usize,
}
#[repr(C)]
pub(super) struct Text {
    pub data: *mut u8,
    pub length: usize,
    storage: Storage,
}

// Text is initialized only at its final address. Methods use raw projections:
// allocation/log leaves may inspect the owner while a buffer changes.
impl Text {
    unsafe fn local(text: *mut Self) -> *mut u8 {
        ptr::addr_of_mut!((*text).storage.local).cast()
    }
    unsafe fn capacity(text: *mut Self) -> usize {
        if (*text).data == Self::local(text) {
            15
        } else {
            (*text).storage.capacity
        }
    }
    pub unsafe fn init(text: *mut Self) {
        ptr::addr_of_mut!((*text).storage).write(Storage { local: [0; 16] });
        ptr::addr_of_mut!((*text).length).write(0);
        ptr::addr_of_mut!((*text).data).write(Self::local(text));
    }
    pub unsafe fn clear(text: *mut Self) {
        (*text).length = 0;
        (*text).data.write(0);
    }
    pub unsafe fn assign(text: *mut Self, bytes: *const u8, length: usize, leaves: &XpXtcLeaves) {
        let capacity = Self::capacity(text);
        if length > capacity {
            let next = length.max(capacity.saturating_mul(2));
            let data = (leaves.allocate)(next + 1, false);
            // Required native allocation either succeeds or terminates; no null result.
            if length != 0 {
                ptr::copy_nonoverlapping(bytes, data, length);
            }
            data.add(length).write(0);
            Self::release(text, leaves);
            (*text).data = data;
            (*text).storage.capacity = next;
        } else {
            if length != 0 {
                ptr::copy(bytes, (*text).data, length);
            }
            (*text).data.add(length).write(0);
        }
        (*text).length = length;
    }
    pub unsafe fn shrink(text: *mut Self, leaves: &XpXtcLeaves) {
        let capacity = Self::capacity(text);
        if capacity == 15 {
            return;
        }
        let length = (*text).length;
        if length <= 15 {
            let old = (*text).data;
            ptr::copy_nonoverlapping(old, Self::local(text), length + 1);
            (*text).data = Self::local(text);
            (leaves.release)(old, capacity + 1);
        } else if length < capacity {
            // The C3 -fno-exceptions std::string implementation skips this attempt.
            let data = (leaves.allocate)(length + 1, true);
            if !data.is_null() {
                ptr::copy_nonoverlapping((*text).data, data, length + 1);
                let old = mem::replace(&mut (*text).data, data);
                (*text).storage.capacity = length;
                (leaves.release)(old, capacity + 1);
            }
        }
    }
    pub unsafe fn release(text: *mut Self, leaves: &XpXtcLeaves) {
        if (*text).data != Self::local(text) {
            let data = (*text).data;
            let bytes = Self::capacity(text) + 1;
            // Publish an empty live loan before an inspecting release callback runs.
            Self::init(text);
            (leaves.release)(data, bytes);
        }
    }
}
