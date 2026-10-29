//! Status fields and device identity policy; JSON and crypto remain platform leaves.
use super::Span;
use core::ffi::{c_char, c_void, CStr};
#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Text {
    Version,
    Family,
    Capabilities,
    Plugin,
    Ip,
    Device,
}
#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Number {
    Api,
    Rssi,
    Heap,
    Time,
}
#[repr(C)]
pub struct Services {
    read: unsafe extern "C" fn(*mut c_void, Text, bool) -> Span,
    number: unsafe extern "C" fn(*mut c_void, Number) -> i32,
    put_text: unsafe extern "C" fn(*mut c_void, *const c_char, Span, bool),
    put_number: unsafe extern "C" fn(*mut c_void, *const c_char, i64),
    hash: unsafe extern "C" fn(*mut c_void, Span, *mut u8) -> bool,
    serial: unsafe extern "C" fn(*mut c_void, *mut u8) -> bool,
    send: unsafe extern "C" fn(*mut c_void),
}
struct Status<'a> {
    ctx: *mut c_void,
    s: &'a Services,
    ap: bool,
}
impl Status<'_> {
    fn read(&self, field: Text) -> Span {
        unsafe { (self.s.read)(self.ctx, field, self.ap) }
    }
    fn number(&self, field: Number) -> i32 {
        unsafe { (self.s.number)(self.ctx, field) }
    }
    fn text(&self, key: &CStr, value: Span, copy: bool) {
        unsafe { (self.s.put_text)(self.ctx, key.as_ptr(), value, copy) }
    }
    fn literal(&self, key: &CStr, value: &CStr) {
        self.text(key, Span::new(value.to_bytes()), false);
    }
    fn integer(&self, key: &CStr, value: i64) {
        unsafe { (self.s.put_number)(self.ctx, key.as_ptr(), value) }
    }
    #[inline(never)]
    fn identity(&self) {
        let plugin = unsafe { self.read(Text::Plugin).bytes() };
        if plugin.is_empty() || plugin.len() > 64 {
            return;
        }
        let mut normalized = [0u8; 64];
        let Some(out) = normalized.get_mut(..plugin.len()) else {
            return;
        };
        out.copy_from_slice(plugin);
        // Arduino String::toLowerCase stops at the first NUL, while hashing uses length.
        for byte in out.iter_mut().take_while(|b| **b != 0) {
            *byte = byte.to_ascii_lowercase();
        }
        let mut hash = [0u8; 32];
        if !unsafe { (self.s.hash)(self.ctx, Span::new(out), hash.as_mut_ptr()) } {
            return;
        }
        let mut hex = [0u8; 64];
        for (pair, byte) in hex.chunks_exact_mut(2).zip(hash) {
            for (out, n) in pair.iter_mut().zip([byte >> 4, byte & 15]) {
                *out = if n < 10 { b'0' + n } else { b'a' + n - 10 };
            }
        }
        self.text(c"deviceId", Span::new(&hex), true);
    }
    #[inline(never)]
    fn serial(&self) {
        let mut bytes = [0u8; 32];
        let read = unsafe { (self.s.serial)(self.ctx, bytes.as_mut_ptr()) };
        let len = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
        let serial = bytes.get(..len).unwrap_or(&[]);
        if read && !serial.is_empty() && serial.iter().all(|b| (32..=126).contains(b)) {
            self.text(c"serial", Span::new(serial), true);
        } else {
            self.literal(c"serial", c"Not found");
        }
    }
    fn send(&self) {
        // Preserve insertion order and the JSON allocator's partial-document behavior.
        self.text(c"version", self.read(Text::Version), false);
        self.text(c"firmwareFamily", self.read(Text::Family), true);
        self.integer(c"pluginApi", self.number(Number::Api) as i64);
        self.text(c"pluginCapabilities", self.read(Text::Capabilities), true);
        self.text(c"ip", self.read(Text::Ip), true);
        self.literal(c"mode", if self.ap { c"AP" } else { c"STA" });
        self.integer(
            c"rssi",
            if self.ap {
                0
            } else {
                self.number(Number::Rssi) as i64
            },
        );
        self.integer(c"freeHeap", self.number(Number::Heap) as u32 as i64);
        self.integer(c"uptime", (self.number(Number::Time) as u32 / 1000) as i64);
        self.identity();
        self.text(c"device", self.read(Text::Device), true);
        self.serial();
        unsafe { (self.s.send)(self.ctx) };
    }
}
/// # Safety
/// Services, context, and spans remain live for this synchronous call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn xp_web_status(ctx: *mut c_void, services: *const Services, ap: bool) {
    let Some(s) = (unsafe { services.as_ref() }) else {
        return;
    };
    Status { ctx, s, ap }.send();
}
