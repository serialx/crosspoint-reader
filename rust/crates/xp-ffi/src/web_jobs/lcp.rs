use core::{ffi::c_void, slice};
#[repr(C)]
pub struct Parse {
    pending: [u8; 16],
    id: [u8; 64],
    len: usize,
}
impl Parse {
    fn new() -> Self {
        Self {
            pending: [0; 16],
            id: [0; 64],
            len: 0,
        }
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_lcp_json(state: *mut Parse, key: bool, data: *const u8, len: usize) {
    let state = &mut *state;
    let input = slice::from_raw_parts(data, len);
    if key {
        let n = len.min(15);
        state.pending[..n].copy_from_slice(&input[..n]);
        state.pending[n] = 0;
    } else if &state.pending[..state.pending.iter().position(|&b| b == 0).unwrap_or(16)]
        == b"device_id"
    {
        state.len = len.min(64);
        state.id[..state.len].copy_from_slice(&input[..state.len]);
    }
}
#[repr(C)]
pub struct Services {
    ctx: *mut c_void,
    operation: unsafe extern "C" fn(*mut c_void, u32, usize, *mut u8) -> usize,
    parse: unsafe extern "C" fn(*mut c_void, *mut Parse) -> bool,
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32)>,
    loaded: *mut bool,
    release: *const u8,
    release_len: usize,
}
#[repr(u32)]
enum Op {
    Load,
    IdLen,
    PrivateLen,
    ExportId,
    Keygen,
    AppendRequest,
    AppendPublic,
    Post,
    SetId,
    SetPrivate,
    Save,
    Forget,
    Private,
    Decode,
    Unwrap,
}
impl Services {
    unsafe fn op(&self, op: Op, n: usize, p: *mut u8) -> usize {
        (self.operation)(self.ctx, op as u32, n, p)
    }
    unsafe fn simple(&self, op: Op) -> usize {
        self.op(op, 0, core::ptr::null_mut())
    }
    unsafe fn append(&self, s: &[u8]) {
        self.op(Op::AppendRequest, s.len(), s.as_ptr().cast_mut());
    }
    unsafe fn log(&self, event: u32) {
        if let Some(f) = self.diagnostic {
            f(self.ctx, event);
        }
    }
    unsafe fn load(&self) {
        if !*self.loaded {
            self.simple(Op::Load);
            *self.loaded = true;
        }
    }
    unsafe fn valid(&self) -> bool {
        self.simple(Op::IdLen) > 0 && self.simple(Op::PrivateLen) == 64
    }
}
unsafe fn cached(s: &Services) -> bool {
    s.load();
    if s.valid() {
        s.simple(Op::ExportId);
        true
    } else {
        false
    }
}
// Builds without an enrollment key need only cached-identity policy.
#[no_mangle]
pub unsafe extern "C" fn xp_lcp_existing(s: *const Services, not_enrollable: *mut bool) -> bool {
    let found = cached(&*s);
    *not_enrollable = !found;
    found
}
#[no_mangle]
pub unsafe extern "C" fn xp_lcp_ensure(s: *const Services, not_enrollable: *mut bool) -> bool {
    let s = &*s;
    *not_enrollable = false;
    if cached(s) {
        return true;
    }
    if s.release_len == 0 {
        *not_enrollable = true;
        return false;
    }
    let mut keys = [0; 64];
    if s.op(Op::Keygen, 0, keys.as_mut_ptr()) == 0 {
        s.log(0);
        return false;
    }
    s.append(b"{\"release_key\":\"");
    s.append(slice::from_raw_parts(s.release, s.release_len));
    s.append(b"\",\"pubkey\":\"");
    s.op(Op::AppendPublic, 32, keys.as_mut_ptr().add(32));
    s.append(b"\"}");
    if s.simple(Op::Post) == 0 {
        s.log(1);
        return false;
    }
    let mut parsed = Parse::new();
    if !(s.parse)(s.ctx, &mut parsed) || parsed.len == 0 {
        s.log(1);
        return false;
    }
    s.op(Op::SetId, parsed.len, parsed.id.as_mut_ptr());
    let mut hex = [0; 64];
    const DIGITS: &[u8; 16] = b"0123456789abcdef";
    for (pair, &b) in hex.chunks_exact_mut(2).zip(keys[..32].iter()) {
        pair[0] = DIGITS[(b >> 4) as usize];
        pair[1] = DIGITS[(b & 15) as usize];
    }
    s.op(Op::SetPrivate, 64, hex.as_mut_ptr());
    if s.simple(Op::Save) == 0 {
        s.log(2);
        return false;
    }
    s.log(3);
    true
}
fn digit(c: u8) -> Option<u8> {
    match c {
        b'0'..=b'9' => Some(c - b'0'),
        b'a'..=b'f' => Some(c - b'a' + 10),
        b'A'..=b'F' => Some(c - b'A' + 10),
        _ => None,
    }
}
fn hex32(input: &[u8], out: &mut [u8]) -> bool {
    if input.len() != 64 {
        return false;
    }
    for (i, dest) in out.iter_mut().enumerate() {
        let mut start = i * 2;
        while start < input.len()
            && matches!(input[start], b' ' | b'\t' | b'\n' | b'\r' | 0x0b | 0x0c)
        {
            start += 1;
        }
        let end = (start + 2).min(input.len());
        let mut negative = false;
        if start < end && matches!(input[start], b'+' | b'-') {
            negative = input[start] == b'-';
            start += 1;
        }
        let mut value = 0u8;
        let mut found = false;
        while start < end {
            if let Some(d) = digit(input[start]) {
                value = value.wrapping_mul(16).wrapping_add(d);
                found = true;
                start += 1;
            } else {
                break;
            }
        }
        if !found {
            return false;
        }
        *dest = if negative {
            value.wrapping_neg()
        } else {
            value
        };
    }
    true
}
#[no_mangle]
pub unsafe extern "C" fn xp_lcp_forget(s: *const Services) {
    let s = &*s;
    s.load();
    s.simple(Op::Forget);
    s.simple(Op::Save);
}
#[no_mangle]
pub unsafe extern "C" fn xp_lcp_unwrap(s: *const Services, out: *mut u8) -> bool {
    let s = &*s;
    let mut decoded = [0; 92];
    for (i, (offset, len)) in [(0, 32), (32, 12), (44, 32), (76, 16)]
        .into_iter()
        .enumerate()
    {
        if s.op(Op::Decode, i, decoded.as_mut_ptr().add(offset)) != len {
            return false;
        }
    }
    s.load();
    if !s.valid() {
        return false;
    }
    let mut hex = [0; 64];
    s.op(Op::Private, 64, hex.as_mut_ptr());
    let mut private = [0; 32];
    if !hex32(&hex, &mut private) {
        return false;
    }
    // The adapter lends these four slices synchronously to the SDK crypto primitive.
    let pointers = [private.as_mut_ptr(), decoded.as_mut_ptr(), out];
    s.op(Op::Unwrap, 0, pointers.as_ptr().cast::<u8>().cast_mut()) != 0
}
