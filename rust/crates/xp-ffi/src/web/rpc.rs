//! Relay/crypto validation and response policy over synchronous platform leaves.
use super::{c_bytes, component, Output, Request, Services, Span};
use core::ffi::{c_char, c_void, CStr};

#[repr(C)]
pub struct Rpc {
    pub(super) files: *const Services,
    parse: unsafe extern "C" fn(*mut c_void, Span) -> Span,
    string: unsafe extern "C" fn(*mut c_void, *const c_char) -> Span,
    send: unsafe extern "C" fn(*mut c_void, u16, *const c_char),
    pub(super) release: unsafe extern "C" fn(*mut c_void, bool),
}
#[repr(C)]
#[derive(Clone, Copy)]
struct Header {
    name: Span,
    value: Span,
}
#[repr(C)]
pub struct Relay {
    request: *const Rpc,
    prepare: unsafe extern "C" fn(*mut c_void, Span, Span, Span),
    suspend: unsafe extern "C" fn(*mut c_void, bool),
    request_tls: unsafe extern "C" fn(*mut c_void, usize) -> i32,
    header_count: unsafe extern "C" fn(*mut c_void) -> usize,
    header: unsafe extern "C" fn(*mut c_void, usize) -> Header,
    append_header: unsafe extern "C" fn(*mut c_void, Header),
    respond:
        unsafe extern "C" fn(*mut c_void, i32, *const c_char, *const c_char, u16, *const c_char),
}
pub(super) struct RpcRequest<'a> {
    pub(super) r: Request<'a>,
    pub(super) s: &'a Rpc,
}
impl RpcRequest<'_> {
    pub(super) fn begin(&self) -> bool {
        if self.r.flags() & 32 == 0 {
            self.r.send(403, b"plugin system disabled");
            return false;
        }
        self.r.json_body(self.s.parse, self.s.send)
    }
    pub(super) fn string(&self, name: &CStr) -> &[u8] {
        unsafe { (self.s.string)(self.r.ctx, name.as_ptr()).bytes() }
    }
    pub(super) fn send(&self, code: u16, body: &CStr) {
        unsafe { (self.s.send)(self.r.ctx, code, body.as_ptr()) }
    }
}
/// # Safety
/// The descriptor and its context remain valid for the call. Callbacks cannot reenter.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn xp_web_relay(ctx: *mut c_void, services: *const Relay) {
    let Some(s) = (unsafe { services.as_ref() }) else {
        return;
    };
    let Some(rpc) = (unsafe { s.request.as_ref() }) else {
        return;
    };
    let Some(files) = (unsafe { rpc.files.as_ref() }) else {
        return;
    };
    let r = RpcRequest {
        r: Request {
            ctx,
            services: files,
        },
        s: rpc,
    };
    if !r.begin() {
        return;
    }
    let plugin = r.string(c"plugin");
    let url = r.string(c"url");
    if !component(plugin) || url.is_empty() {
        r.send(400, c"{\"error\":\"missing plugin/url\"}");
        return;
    }
    let method = unsafe { (rpc.string)(ctx, c"method".as_ptr()) };
    let method = if method.data.is_null() {
        Span::new(b"GET")
    } else {
        method
    };
    unsafe {
        (s.prepare)(ctx, Span::new(url), method, Span::new(r.string(c"body")));
        // No parsed JSON, argument scratch or transport-owned body survives into TLS.
        (rpc.release)(ctx, true);
        (s.suspend)(ctx, true);
    }
    let status = unsafe { (s.request_tls)(ctx, 32 * 1024) };
    if status < 0 {
        r.send(
            502,
            c"{\"error\":\"relay failed; large bodies need /api/fetch\"}",
        );
    } else {
        for index in 0..unsafe { (s.header_count)(ctx) } {
            unsafe {
                (s.append_header)(ctx, (s.header)(ctx, index));
            }
        }
        unsafe {
            (s.respond)(
                ctx,
                status,
                c"X-Relay-Status".as_ptr(),
                c"X-Relay-Headers".as_ptr(),
                200,
                c"application/octet-stream".as_ptr(),
            );
        }
    }
    unsafe {
        (s.suspend)(ctx, false);
    }
}

#[repr(u32)]
#[derive(Clone, Copy)]
enum Op {
    Random,
    Sha1,
    Sha256,
    AesEncrypt,
    AesDecrypt,
    RsaPrivate,
    RsaGenerate,
    RsaPublic,
    RsaSign,
    Pkcs12,
}
#[repr(C)]
pub struct Crypto {
    request: *const Rpc,
    initialize: unsafe extern "C" fn(*mut c_void),
    integer: unsafe extern "C" fn(*mut c_void, *const c_char, i32) -> i32,
    allocate: unsafe extern "C" fn(*mut c_void, u32, usize) -> *mut u8,
    decode: unsafe extern "C" fn(Span, *mut u8, usize) -> i32,
    call: unsafe extern "C" fn(*mut c_void, u32, Span, Span, Span, *mut u8, usize) -> isize,
    output: unsafe extern "C" fn(*mut c_void, u32) -> Span,
    last_error: unsafe extern "C" fn(*mut c_void) -> Span,
    password: unsafe extern "C" fn(*mut c_void, Span),
    encode: unsafe extern "C" fn(*mut c_void, *const c_char, Span) -> bool,
    error: unsafe extern "C" fn(*mut c_void, *const Span, usize, bool),
    clear: unsafe extern "C" fn(*mut c_void),
    overflowed: unsafe extern "C" fn(*mut c_void) -> bool,
    send_response: unsafe extern "C" fn(*mut c_void),
    oom: unsafe extern "C" fn(*mut c_void, *const c_char),
}
const EMPTY: Span = Span {
    data: b"".as_ptr(),
    len: 0,
};
struct CryptoRequest<'a> {
    r: RpcRequest<'a>,
    s: &'a Crypto,
    encode_failed: bool,
}
type CryptoResult = core::result::Result<(), &'static CStr>;
impl CryptoRequest<'_> {
    fn oom(&self, reason: &CStr) {
        unsafe {
            (self.s.oom)(self.r.r.ctx, reason.as_ptr());
        }
        self.r.send(503, c"{\"error\":\"out of memory\"}");
    }

    fn decode(&self, field: &CStr, slot: u32, oom: &mut bool) -> Span {
        let bytes = c_bytes(self.r.string(field));
        if bytes.is_empty() || bytes.len() > 64 * 1024 {
            return EMPTY;
        }
        let capacity = bytes.len() * 3 / 4 + 3;
        let data = unsafe { (self.s.allocate)(self.r.r.ctx, slot, capacity) };
        if data.is_null() {
            *oom = true;
            return EMPTY;
        }
        let n = unsafe { (self.s.decode)(Span::new(bytes), data, capacity) };
        Span {
            data,
            len: if n < 0 { 0 } else { n as usize },
        }
    }
    fn allocate(
        &self,
        size: usize,
        reason: &'static CStr,
    ) -> core::result::Result<*mut u8, &'static CStr> {
        let data = unsafe { (self.s.allocate)(self.r.r.ctx, 3, size) };
        if data.is_null() && size != 0 {
            Err(reason)
        } else {
            Ok(data)
        }
    }
    fn call(&self, op: Op, a: Span, b: Span, data: Span, out: *mut u8, capacity: usize) -> isize {
        unsafe { (self.s.call)(self.r.r.ctx, op as u32, a, b, data, out, capacity) }
    }
    fn encoded(&mut self, key: &CStr, bytes: Span) {
        self.encode_failed |= !unsafe { (self.s.encode)(self.r.r.ctx, key.as_ptr(), bytes) };
    }
    fn error(&self, text: &'static CStr) {
        let parts = [Span::new(text.to_bytes())];
        unsafe { (self.s.error)(self.r.r.ctx, parts.as_ptr(), parts.len(), true) }
    }
    fn detail(&self, prefix: &'static [u8], suffix: &[u8]) {
        let parts = [
            Span::new(prefix),
            unsafe { (self.s.last_error)(self.r.r.ctx) },
            Span::new(suffix),
        ];
        unsafe { (self.s.error)(self.r.r.ctx, parts.as_ptr(), parts.len(), false) }
    }
    fn outputs(&mut self, first: &CStr, second: &CStr) {
        for (i, key) in [first, second].iter().enumerate() {
            self.encoded(key, unsafe { (self.s.output)(self.r.r.ctx, i as u32) });
        }
    }
    #[inline(never)]
    fn execute(&mut self) -> CryptoResult {
        let op = self.r.string(c"op");
        let mut oom = false;
        match op {
            b"random" => {
                let n = unsafe { (self.s.integer)(self.r.r.ctx, c"len".as_ptr(), 16) }
                    .clamp(0, 4096) as usize;
                let out = self.allocate(n, c"random output")?;
                if n != 0 {
                    self.call(Op::Random, EMPTY, EMPTY, EMPTY, out, n);
                }
                self.encoded(c"data", Span { data: out, len: n });
            }
            b"sha1" | b"sha256" => {
                let operation = if op == b"sha1" { Op::Sha1 } else { Op::Sha256 };
                let n = if op == b"sha1" { 20 } else { 32 };
                let data = self.decode(c"data", 0, &mut oom);
                if oom {
                    return Err(c"input");
                }
                let mut hash = [0; 32];
                self.call(operation, EMPTY, EMPTY, data, hash.as_mut_ptr(), n);
                self.encoded(c"data", Span::new(&hash[..n]));
            }
            b"aesenc" | b"aesdec" => {
                let encrypt = op == b"aesenc";
                let key = self.decode(c"key", 0, &mut oom);
                let iv = self.decode(c"iv", 1, &mut oom);
                let data = self.decode(c"data", 2, &mut oom);
                if oom {
                    return Err(c"input");
                }
                if key.len != 16 || iv.len != 16 {
                    self.error(c"key/iv must be 16 bytes");
                } else if encrypt {
                    let n = (data.len / 16 + 1) * 16;
                    // Both allocations are attempted before checking either, as in C++.
                    let out = unsafe { (self.s.allocate)(self.r.r.ctx, 3, n) };
                    if !out.is_null() {
                        unsafe {
                            core::ptr::copy_nonoverlapping(data.data, out, data.len);
                            core::ptr::write_bytes(
                                out.add(data.len),
                                (n - data.len) as u8,
                                n - data.len,
                            );
                        }
                    }
                    let result = self.call(Op::AesEncrypt, key, iv, data, out, n);
                    if result == -2 {
                        return Err(c"AES output");
                    }
                    if result >= 0 {
                        self.encoded(c"data", Span { data: out, len: n });
                    } else {
                        self.error(c"aesenc failed");
                    }
                } else if !data.len.is_multiple_of(16) {
                    self.error(c"data not block-aligned");
                } else {
                    let out = self.allocate(data.len, c"AES output")?;
                    let mut empty = 0;
                    let out = if data.len == 0 { &mut empty } else { out };
                    if self.call(Op::AesDecrypt, key, iv, data, out, data.len) >= 0 {
                        self.encoded(
                            c"data",
                            Span {
                                data: out,
                                len: data.len,
                            },
                        );
                    } else {
                        self.error(c"aesdec failed");
                    }
                }
            }
            b"rsadec" | b"pubencrypt" => {
                let private = op == b"rsadec";
                let key = self.decode(if private { c"private" } else { c"cert" }, 0, &mut oom);
                let data = self.decode(c"data", 1, &mut oom);
                if oom {
                    return Err(c"input");
                }
                let out = self.allocate(512, c"RSA output")?;
                let n = self.call(
                    if private {
                        Op::RsaPrivate
                    } else {
                        Op::RsaPublic
                    },
                    key,
                    EMPTY,
                    data,
                    out,
                    512,
                );
                if n > 0 || (!private && n == 0) {
                    self.encoded(
                        c"data",
                        Span {
                            data: out,
                            len: n as usize,
                        },
                    );
                } else if private {
                    self.error(c"rsadec failed");
                } else {
                    let mut bytes = [0; 64];
                    let mut suffix = Output::new(&mut bytes);
                    suffix.add(b" (cert ");
                    suffix.number(key.len);
                    suffix.add(b"B, data ");
                    suffix.number(data.len);
                    suffix.add(b"B)");
                    self.detail(b"pubencrypt failed: ", suffix.bytes());
                }
            }
            b"keygen" => {
                if self.call(
                    Op::RsaGenerate,
                    EMPTY,
                    EMPTY,
                    EMPTY,
                    core::ptr::null_mut(),
                    0,
                ) >= 0
                {
                    self.outputs(c"public", c"private");
                } else {
                    self.detail(b"keygen failed: ", b"");
                }
            }
            b"sign" => {
                let key = self.decode(c"private", 0, &mut oom);
                let hash = self.decode(c"hash", 1, &mut oom);
                if oom {
                    return Err(c"input");
                }
                if hash.len != 20 {
                    self.error(c"hash must be 20 bytes");
                } else {
                    self.sign(key, hash);
                }
            }
            b"pkcs12" => {
                let data = self.decode(c"data", 0, &mut oom);
                if oom {
                    return Err(c"input");
                }
                unsafe {
                    (self.s.password)(self.r.r.ctx, Span::new(self.r.string(c"password")));
                    (self.r.s.release)(self.r.r.ctx, false);
                }
                if data.len == 0 {
                    self.error(c"pkcs12 failed: bundle missing, invalid, or out of memory");
                } else if self.call(Op::Pkcs12, EMPTY, EMPTY, data, core::ptr::null_mut(), 0) >= 0 {
                    self.outputs(c"key", c"cert");
                } else {
                    self.detail(b"pkcs12 failed: ", b"");
                }
            }
            _ => self.error(c"unknown op"),
        }
        Ok(())
    }
    #[inline(never)]
    fn sign(&mut self, key: Span, hash: Span) {
        let mut signature = [0; 128];
        if self.call(
            Op::RsaSign,
            key,
            EMPTY,
            hash,
            signature.as_mut_ptr(),
            signature.len(),
        ) >= 0
        {
            self.encoded(c"data", Span::new(&signature));
        } else {
            self.error(c"sign failed");
        }
    }
}
/// # Safety
/// Services and context are exclusively loaned until return; no callback reentry.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn xp_web_crypto(ctx: *mut c_void, services: *const Crypto) {
    let Some(s) = (unsafe { services.as_ref() }) else {
        return;
    };
    let Some(rpc) = (unsafe { s.request.as_ref() }) else {
        return;
    };
    let Some(files) = (unsafe { rpc.files.as_ref() }) else {
        return;
    };
    let mut c = CryptoRequest {
        r: RpcRequest {
            r: Request {
                ctx,
                services: files,
            },
            s: rpc,
        },
        s,
        encode_failed: false,
    };
    if !c.r.begin() {
        return;
    }
    unsafe {
        (s.initialize)(ctx);
    }
    if let Err(reason) = c.execute() {
        c.oom(reason);
        return;
    }
    unsafe {
        (s.clear)(ctx);
    }
    if c.encode_failed || unsafe { (s.overflowed)(ctx) } {
        c.oom(c"response");
    } else {
        unsafe {
            (s.send_response)(ctx);
        }
    }
}
