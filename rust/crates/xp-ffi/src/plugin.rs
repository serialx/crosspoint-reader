//! Plugin policy. Containers, JSON conversions and I/O remain synchronous leaves.
use core::ffi::{c_char, c_void};

#[repr(u32)]
enum SubscriptionDiagnostic {
    Full,
    Long,
    Unknown,
    Added,
}
#[repr(u32)]
enum AppendDiagnostic {
    Open,
    Cap,
    Short,
}
#[repr(u32)]
enum DrainDiagnostic {
    Rewrite,
    Finished,
}
#[repr(u32)]
enum ValueKind {
    Empty,
    String,
    Integer,
}
#[repr(u32)]
enum ManifestField {
    TokenFile,
    TokenPath,
    ConfigFile,
    AuthType,
    AuthTokenPath,
    Toast,
    Dest,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Span {
    data: *const u8,
    len: usize,
}
impl Span {
    fn new(s: &[u8]) -> Self {
        Self {
            data: s.as_ptr(),
            len: s.len(),
        }
    }
    unsafe fn bytes<'a>(self) -> &'a [u8] {
        if self.len == 0 {
            &[]
        } else {
            core::slice::from_raw_parts(self.data, self.len)
        }
    }
}
#[repr(C)]
pub struct Text {
    ctx: *mut c_void,
    view: unsafe extern "C" fn(*mut c_void) -> Span,
    replace: unsafe extern "C" fn(*mut c_void, usize, usize, Span),
}
#[repr(transparent)]
pub struct Roots([*const c_char; 3]);
// Every entry points to an immutable static C string.
unsafe impl Sync for Roots {}
#[no_mangle]
pub static xp_plugin_roots: Roots = Roots([
    c"/.crosspoint/plugins".as_ptr(),
    c"/plugins".as_ptr(),
    c"/.plugins".as_ptr(),
]);
const EVENTS: [&[u8]; 5] = [
    b"reader.open\0",
    b"reader.exit\0",
    b"reader.session\0",
    b"book.downloaded\0",
    b"sleep.enter\0",
];
fn version(v: &[u8], out: &mut [u32; 3]) -> bool {
    let mut p = 0;
    for (k, slot) in out.iter_mut().enumerate() {
        if k > 0 {
            if v.get(p) != Some(&b'.') {
                return false;
            }
            p += 1;
        }
        if !v.get(p).is_some_and(u8::is_ascii_digit) {
            return false;
        }
        let mut n = 0u32;
        while let Some(&b) = v.get(p).filter(|b| b.is_ascii_digit()) {
            let Some(next) = n
                .checked_mul(10)
                .and_then(|n| n.checked_add(u32::from(b - b'0')))
            else {
                return false;
            };
            n = next;
            p += 1;
        }
        *slot = n;
    }
    p == v.len()
}
/// # Safety
/// Input spans are readable for their lengths.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_root(s: Span) -> bool {
    xp_plugin_roots
        .0
        .iter()
        .any(|&root| core::ffi::CStr::from_ptr(root).to_bytes() == s.bytes())
}
/// # Safety
/// Input spans are readable for their lengths.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_reuse_dir(dir: Span, sub: Span) -> bool {
    !sub.bytes().is_empty() && !sub.bytes().contains(&b'/') && xp_plugin_root(dir)
}
/// # Safety
/// The input is readable and out points to three writable u32 values.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_version(s: Span, out: *mut [u32; 3]) -> bool {
    version(s.bytes(), &mut *out)
}
/// # Safety
/// Input spans are readable for their lengths.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_newer(c: Span, i: Span) -> bool {
    let (mut cv, mut iv) = ([0; 3], [0; 3]);
    version(c.bytes(), &mut cv) && (!version(i.bytes(), &mut iv) || cv > iv)
}
#[no_mangle]
pub extern "C" fn xp_plugin_classify(browse: bool, events: bool) -> u32 {
    if browse {
        1
    } else if events {
        2
    } else {
        0
    }
}
#[no_mangle]
pub extern "C" fn xp_plugin_picker(kind: u32, readme: bool) -> u32 {
    if kind == 1 {
        1
    } else if readme {
        2
    } else {
        0
    }
}
#[no_mangle]
pub extern "C" fn xp_plugin_discoverable(q: bool, js: bool, device: bool, manifest: bool) -> bool {
    !q && (js || device || manifest)
}
#[no_mangle]
pub extern "C" fn xp_plugin_subtitle(
    viewport: i32,
    lines: i32,
    label: i32,
    value: i32,
    subtitle: i32,
    padding: i32,
) -> i32 {
    let label = label.wrapping_mul(lines.clamp(1, 16));
    let available = viewport
        .wrapping_sub(label.max(value))
        .wrapping_sub(padding.max(0).wrapping_mul(2));
    if subtitle > 0 {
        (available / subtitle).clamp(1, 16)
    } else {
        1
    }
}
/// # Safety
/// Input span is readable for its length.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_split_count(s: Span) -> usize {
    s.bytes().iter().filter(|&&b| b == b'.').count() + 1
}
/// # Safety
/// Input span is readable and callback accepts borrowed segments synchronously.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_split(
    s: Span,
    ctx: *mut c_void,
    segment: unsafe extern "C" fn(*mut c_void, Span),
) {
    for part in s.bytes().split(|&b| b == b'.').filter(|s| !s.is_empty()) {
        segment(ctx, Span::new(part));
    }
}
/// # Safety
/// Text callbacks must remain valid and replace must not retain spans.
/// key must not alias the string; self_value indicates that value is the whole string.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_substitute(
    t: *const Text,
    key: Span,
    value: Span,
    self_value: bool,
) {
    let t = &*t;
    let key_bytes = key.bytes();
    // An empty key cannot advance the original replacement loop.
    if key_bytes.is_empty() {
        return;
    }
    let mut pos = 0;
    loop {
        let view = (t.view)(t.ctx).bytes();
        let Some(tail) = view.get(pos..) else {
            return;
        };
        let Some(found) = tail.windows(key_bytes.len()).position(|s| s == key_bytes) else {
            return;
        };
        pos += found;
        let replacement = if self_value { (t.view)(t.ctx) } else { value };
        (t.replace)(t.ctx, pos, key.len, replacement);
        let advance = if self_value {
            (t.view)(t.ctx).len
        } else {
            value.len
        };
        let Some(next) = pos.checked_add(advance) else {
            return;
        };
        pos = next;
    }
}
/// # Safety
/// Input span is readable; append consumes borrowed spans synchronously.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_encode(
    s: Span,
    ctx: *mut c_void,
    append: unsafe extern "C" fn(*mut c_void, Span),
) {
    const HEX: &[u8; 16] = b"0123456789ABCDEF";
    for &b in s.bytes() {
        if b.is_ascii_alphanumeric() || b"-_.~".contains(&b) {
            append(ctx, Span::new(&[b]));
        } else {
            append(
                ctx,
                Span::new(&[b'%', HEX[usize::from(b >> 4)], HEX[usize::from(b & 15)]]),
            );
        }
    }
}
/// # Safety
/// Input span is readable for its length.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_relative(s: Span) -> bool {
    !s.bytes().is_empty() && s.bytes()[0] != b'/'
}
/// # Safety
/// Input span is readable for its length.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_event(s: Span) -> i32 {
    EVENTS
        .iter()
        .position(|e| &e[..e.len() - 1] == s.bytes())
        .map_or(-1, |i| i as i32)
}
#[no_mangle]
pub extern "C" fn xp_plugin_event_name(e: u32) -> *const c_char {
    EVENTS
        .get(e as usize)
        .map_or(c"".as_ptr(), |e| e.as_ptr().cast())
}
#[no_mangle]
pub extern "C" fn xp_plugin_event_mask(e: u32) -> u8 {
    1u8.checked_shl(e).unwrap_or(0)
}
/// # Safety
/// reserved points to a writable capacity value.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_receive(
    size: usize,
    len: usize,
    max: usize,
    reserved: *mut usize,
) -> bool {
    if size > max || len > max - size {
        return false;
    }
    let needed = size + len;
    if needed > *reserved {
        *reserved = max.min(needed.max((*reserved).wrapping_mul(2)));
    }
    true
}
#[no_mangle]
pub extern "C" fn xp_plugin_http_ok(status: i32, failed: bool, complete: bool) -> bool {
    !failed && status >= 0 && complete
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Subscriber {
    name: [u8; 24],
    dir: [u8; 64],
    mask: u8,
    connect_mask: u8,
}
const EMPTY_SUB: Subscriber = Subscriber {
    name: [0; 24],
    dir: [0; 64],
    mask: 0,
    connect_mask: 0,
};
fn zbytes(s: &[u8]) -> &[u8] {
    &s[..s.iter().position(|&b| b == 0).unwrap_or(s.len())]
}
#[repr(C)]
pub struct Subscriptions {
    ctx: *mut c_void,
    count: unsafe extern "C" fn(*mut c_void) -> usize,
    device: unsafe extern "C" fn(*mut c_void, usize) -> bool,
    name: unsafe extern "C" fn(*mut c_void, usize) -> Span,
    dir: unsafe extern "C" fn(*mut c_void, usize) -> Span,
    parse: unsafe extern "C" fn(*mut c_void, usize) -> bool,
    event: unsafe extern "C" fn(*mut c_void, usize) -> Span,
    connect: unsafe extern "C" fn(*mut c_void, usize) -> bool,
    release: unsafe extern "C" fn(*mut c_void),
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, usize, usize)>,
}
/// # Safety
/// table points to eight writable subscribers. Callbacks borrow ctx synchronously;
/// name/dir spans remain valid through the current iteration.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_subscriptions(
    table: *mut [Subscriber; 8],
    ops: *const Subscriptions,
) {
    let s = &*ops;
    let table = &mut *table;
    table.fill(EMPTY_SUB);
    let mut slot = 0;
    for i in 0..(s.count)(s.ctx) {
        if !(s.device)(s.ctx, i) {
            continue;
        }
        if slot == table.len() {
            s.diagnostic(SubscriptionDiagnostic::Full as u32, i, 0);
            break;
        }
        let name = (s.name)(s.ctx, i).bytes();
        let dir = (s.dir)(s.ctx, i).bytes();
        if name.len() >= 24 || dir.len() >= 64 {
            s.diagnostic(SubscriptionDiagnostic::Long as u32, i, 0);
            continue;
        }
        if !(s.parse)(s.ctx, i) {
            (s.release)(s.ctx);
            continue;
        }
        let (mut mask, mut connect) = (0, 0);
        let mut n = 0;
        loop {
            let event = (s.event)(s.ctx, n);
            if event.data.is_null() {
                break;
            }
            let e = xp_plugin_event(event);
            if e < 0 {
                s.diagnostic(SubscriptionDiagnostic::Unknown as u32, i, n);
            } else {
                let bit = xp_plugin_event_mask(e as u32);
                mask |= bit;
                if (s.connect)(s.ctx, n) {
                    connect |= bit;
                }
            }
            n += 1;
        }
        (s.release)(s.ctx);
        if mask & 16 != 0 {
            connect |= 16;
        }
        if mask == 0 {
            continue;
        }
        let sub = &mut table[slot];
        sub.name[..name.len()].copy_from_slice(name);
        sub.dir[..dir.len()].copy_from_slice(dir);
        sub.mask = mask;
        sub.connect_mask = connect;
        slot += 1;
        s.diagnostic(SubscriptionDiagnostic::Added as u32, i, usize::from(mask));
    }
}
/// # Safety
/// table points to eight readable subscribers.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_any(table: *const [Subscriber; 8], e: u32) -> bool {
    let bit = xp_plugin_event_mask(e);
    (*table).iter().any(|s| s.name[0] != 0 && s.mask & bit != 0)
}
/// # Safety
/// table and input span are readable.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_mask(table: *const [Subscriber; 8], name: Span) -> u8 {
    (*table)
        .iter()
        .find(|s| s.name[0] != 0 && zbytes(&s.name) == name.bytes())
        .map_or(0, |s| s.mask)
}
#[repr(C)]
pub struct Connect {
    ctx: *mut c_void,
    read: unsafe extern "C" fn(*mut c_void, *const Subscriber) -> Span,
}
/// # Safety
/// Table, operations and context remain valid for this synchronous call.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_wants_connect(
    table: *const [Subscriber; 8],
    ops: *const Connect,
) -> bool {
    let s = &*ops;
    for sub in &*table {
        if sub.name[0] == 0 || sub.connect_mask == 0 {
            continue;
        }
        let raw = (s.read)(s.ctx, sub).bytes();
        for (i, event) in EVENTS.iter().enumerate() {
            if sub.connect_mask & (1 << i) == 0 {
                continue;
            }
            // Match the historical compact envelope, without parsing/reformatting it.
            let name = &event[..event.len() - 1];
            let needed = name.len() + 6;
            if raw.windows(needed).any(|w| {
                w.starts_with(b"\"e\":\"") && &w[5..needed - 1] == name && w[needed - 1] == b'"'
            }) {
                return true;
            }
        }
    }
    false
}
#[repr(C)]
pub struct Append {
    ctx: *mut c_void,
    open: unsafe extern "C" fn(*mut c_void) -> bool,
    close: unsafe extern "C" fn(*mut c_void),
    remove: unsafe extern "C" fn(*mut c_void),
    size: unsafe extern "C" fn(*mut c_void) -> u64,
    write: unsafe extern "C" fn(*mut c_void) -> usize,
    truncate: unsafe extern "C" fn(*mut c_void, u64),
    flush: unsafe extern "C" fn(*mut c_void),
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32)>,
}
/// # Safety
/// Callbacks operate on one outbox and a serialized line for the call.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_append(ops: *const Append, line_size: usize) {
    let s = &*ops;
    if !(s.open)(s.ctx) {
        s.diagnostic(AppendDiagnostic::Open as u32);
        return;
    }
    // The cap used the HAL's 32-bit fileSize; rollback uses fileSize64.
    if (s.size)(s.ctx) as u32 > 4096 {
        (s.close)(s.ctx);
        (s.remove)(s.ctx);
        if !(s.open)(s.ctx) {
            return;
        }
        s.diagnostic(AppendDiagnostic::Cap as u32);
    }
    let before = (s.size)(s.ctx);
    if (s.write)(s.ctx) != line_size {
        s.diagnostic(AppendDiagnostic::Short as u32);
        (s.truncate)(s.ctx, before);
    }
    (s.flush)(s.ctx);
}
#[repr(C)]
pub struct Drain {
    ctx: *mut c_void,
    deliver: unsafe extern "C" fn(*mut c_void, Span) -> bool,
    remove: unsafe extern "C" fn(*mut c_void),
    rewrite: unsafe extern "C" fn(*mut c_void, usize) -> bool,
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, bool)>,
}
/// # Safety
/// raw remains readable while callbacks run, budget is exclusively writable.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_drain_lines(raw: Span, budget: *mut usize, ops: *const Drain) {
    let s = &*ops;
    let raw = raw.bytes();
    let mut pos = 0;
    let mut stalled = false;
    while pos < raw.len() && *budget > 0 {
        let line = &raw[pos..];
        let len = line.iter().position(|&b| b == b'\n').unwrap_or(line.len());
        if len != 0 {
            if !(s.deliver)(s.ctx, Span::new(&line[..len])) {
                stalled = true;
                break;
            }
            *budget -= 1;
        }
        pos = pos.saturating_add(len).saturating_add(1);
    }
    if pos >= raw.len() && !stalled {
        (s.remove)(s.ctx);
    } else if pos > 0 && !(s.rewrite)(s.ctx, pos) {
        s.diagnostic(DrainDiagnostic::Rewrite as u32, stalled);
    }
    s.diagnostic(DrainDiagnostic::Finished as u32, stalled);
}
#[repr(C)]
pub struct Install {
    ctx: *mut c_void,
    exists: unsafe extern "C" fn(*mut c_void) -> bool,
    remove_backup: unsafe extern "C" fn(*mut c_void),
    backup: unsafe extern "C" fn(*mut c_void) -> bool,
    install: unsafe extern "C" fn(*mut c_void) -> bool,
    restore: unsafe extern "C" fn(*mut c_void) -> bool,
    remove_temp: unsafe extern "C" fn(*mut c_void),
    committed: unsafe extern "C" fn(*mut c_void),
    restore_failed: unsafe extern "C" fn(*mut c_void),
}
/// # Safety
/// Callbacks borrow one download transaction synchronously.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_install_download(status: i32, ops: *const Install) -> i32 {
    let s = &*ops;
    if !(200..300).contains(&status) {
        (s.remove_temp)(s.ctx);
        return status;
    }
    (s.remove_backup)(s.ctx);
    let had = (s.exists)(s.ctx);
    if had && !(s.backup)(s.ctx) {
        (s.remove_temp)(s.ctx);
        return -1;
    }
    if !(s.install)(s.ctx) {
        (s.remove_temp)(s.ctx);
        if had && !(s.restore)(s.ctx) {
            (s.restore_failed)(s.ctx);
        }
        return -1;
    }
    (s.remove_backup)(s.ctx);
    (s.committed)(s.ctx);
    status
}
#[repr(C)]
pub struct Delivery {
    ctx: *mut c_void,
    run: unsafe extern "C" fn(*mut c_void) -> i32,
    mint: unsafe extern "C" fn(*mut c_void) -> bool,
    save: unsafe extern "C" fn(*mut c_void),
    accept_token: unsafe extern "C" fn(*mut c_void),
    toast: unsafe extern "C" fn(*mut c_void),
}
/// # Safety
/// Callbacks borrow one delivery and its mutable token synchronously.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_deliver(
    password: bool,
    show_toast: bool,
    ops: *const Delivery,
) -> bool {
    let s = &*ops;
    let mut status = (s.run)(s.ctx);
    if matches!(status, 401 | 403) && password && (s.mint)(s.ctx) {
        (s.save)(s.ctx);
        (s.accept_token)(s.ctx);
        status = (s.run)(s.ctx);
    }
    if !(200..300).contains(&status) {
        return false;
    }
    if show_toast {
        (s.toast)(s.ctx);
    }
    true
}

#[repr(C)]
pub struct Run {
    ctx: *mut c_void,
    allowed: unsafe extern "C" fn(*mut c_void) -> bool,
    download: unsafe extern "C" fn(*mut c_void) -> i32,
    response: unsafe extern "C" fn(*mut c_void) -> i32,
    rejected: unsafe extern "C" fn(*mut c_void),
}
/// # Safety
/// Callbacks borrow one prepared request and destination synchronously.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_run(download: bool, ops: *const Run) -> i32 {
    let s = &*ops;
    if !download {
        return (s.response)(s.ctx);
    }
    if !(s.allowed)(s.ctx) {
        (s.rejected)(s.ctx);
        // An unsafe manifest destination cannot be repaired by retrying.
        return 200;
    }
    (s.download)(s.ctx)
}

#[repr(C)]
pub struct JsonVtable {
    select: unsafe extern "C" fn(*mut c_void, u32, u32, Span, usize),
    text: unsafe extern "C" fn(*mut c_void, u32) -> Span,
    integer: unsafe extern "C" fn(*mut c_void, u32) -> bool,
    null: unsafe extern "C" fn(*mut c_void, u32) -> bool,
    pair: unsafe extern "C" fn(*mut c_void, u32, u32, usize, *mut Span) -> bool,
    index: unsafe extern "C" fn(*mut c_void, Span) -> usize,
}
#[repr(C)]
pub struct Json {
    ctx: *mut c_void,
    vtable: *const JsonVtable,
}
impl Json {
    unsafe fn field(&self, from: u32, to: u32, key: &[u8]) {
        ((*self.vtable).select)(self.ctx, from, to, Span::new(key), 0);
    }
    unsafe fn text(&self, slot: u32, default: &[u8]) -> Span {
        let value = ((*self.vtable).text)(self.ctx, slot);
        if value.data.is_null() {
            Span::new(default)
        } else {
            value
        }
    }
}
/// # Safety
/// The JSON adapter owns slot zero and accepts synchronous reads/selections.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_resolve(json: *const Json, path: Span) {
    let j = &*json;
    let v = &*j.vtable;
    for part in path.bytes().split(|&b| b == b'.').filter(|p| !p.is_empty()) {
        if (v.null)(j.ctx, 0) {
            break;
        }
        if part[0].is_ascii_digit() {
            let n = (v.index)(j.ctx, Span::new(part));
            (v.select)(
                j.ctx,
                0,
                0,
                Span {
                    data: core::ptr::null(),
                    len: 0,
                },
                n,
            );
        } else {
            j.field(0, 0, part);
        }
    }
}
#[repr(C)]
pub struct Request {
    ctx: *mut c_void,
    text: Option<unsafe extern "C" fn(*mut c_void, u32, Span)>,
    header: unsafe extern "C" fn(*mut c_void, Span, Span),
    reserve: unsafe extern "C" fn(*mut c_void, usize),
}
unsafe fn read_headers(j: &Json, slot: u32, out: &Request) {
    let v = &*j.vtable;
    let mut key = Span::new(&[]);
    let mut count = 0;
    while (v.pair)(j.ctx, slot, 3, count, &mut key) {
        count += 1;
    }
    (out.reserve)(out.ctx, count);
    for i in 0..count {
        (v.pair)(j.ctx, slot, 3, i, &mut key);
        (out.header)(out.ctx, key, j.text(3, b""));
    }
}
/// # Safety
/// JSON slot zero is the request object; callbacks copy borrowed fields.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_read_request(
    json: *const Json,
    default: Span,
    out: *const Request,
) {
    let j = &*json;
    let out = &*out;
    let Some(text) = out.text else { return };
    for (i, key) in [b"url".as_slice(), b"method", b"body"].iter().enumerate() {
        j.field(0, 1, key);
        let value = j.text(1, if i == 1 { default.bytes() } else { b"" });
        text(out.ctx, i as u32, value);
    }
    j.field(0, 1, b"headers");
    read_headers(j, 1, out);
}
/// # Safety
/// JSON slot zero is the headers object; callbacks copy borrowed fields.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_read_headers(json: *const Json, out: *const Request) {
    read_headers(&*json, 0, &*out);
}
/// # Safety
/// JSON slot zero is readable. Result: empty, string, integral.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_value_kind(json: *const Json) -> u32 {
    let j = &*json;
    let v = &*j.vtable;
    if (v.null)(j.ctx, 0) {
        return ValueKind::Empty as u32;
    }
    if !(v.text)(j.ctx, 0).data.is_null() {
        return ValueKind::String as u32;
    }
    if (v.integer)(j.ctx, 0) {
        ValueKind::Integer as u32
    } else {
        ValueKind::Empty as u32
    }
}
/// # Safety
/// Input spans are readable for their lengths.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_password(kind: Span, url: Span) -> bool {
    kind.bytes() == b"password" && !url.bytes().is_empty()
}
#[no_mangle]
pub extern "C" fn xp_plugin_mint_status(status: i32) -> bool {
    (200..300).contains(&status)
}
#[repr(C)]
pub struct Locations {
    ctx: *mut c_void,
    root: unsafe extern "C" fn(*mut c_void, usize) -> bool,
    next: unsafe extern "C" fn(*mut c_void) -> bool,
    directory: unsafe extern "C" fn(*mut c_void) -> bool,
    name: unsafe extern "C" fn(*mut c_void) -> Span,
    seen: unsafe extern "C" fn(*mut c_void) -> bool,
    claim: unsafe extern "C" fn(*mut c_void),
    path: unsafe extern "C" fn(*mut c_void, usize),
    marker: unsafe extern "C" fn(*mut c_void, *const c_char) -> bool,
    append: unsafe extern "C" fn(*mut c_void, bool, bool, bool),
}
/// # Safety
/// The callbacks own the current directory iterator and output containers.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_scan(ops: *const Locations, existence: bool) -> bool {
    let s = &*ops;
    for root in 0..xp_plugin_roots.0.len() {
        if !(s.root)(s.ctx, root) {
            continue;
        }
        while (s.next)(s.ctx) {
            if !(s.directory)(s.ctx) {
                continue;
            }
            let name = (s.name)(s.ctx).bytes();
            if name.is_empty() || name[0] == b'.' {
                continue;
            }
            if !existence {
                if (s.seen)(s.ctx) {
                    continue;
                }
                (s.claim)(s.ctx);
            }
            (s.path)(s.ctx, root);
            let js = (s.marker)(s.ctx, c"/plugin.js".as_ptr());
            if existence && js {
                return true;
            }
            let device = (s.marker)(s.ctx, c"/device.json".as_ptr());
            if existence && device {
                return true;
            }
            let manifest = (s.marker)(s.ctx, c"/manifest.json".as_ptr());
            if existence && manifest {
                return true;
            }
            if js || device || manifest {
                (s.append)(s.ctx, js, device, manifest);
            }
        }
    }
    false
}
#[repr(C)]
pub struct Find {
    ctx: *mut c_void,
    candidate: unsafe extern "C" fn(*mut c_void, usize) -> bool,
    quarantined: unsafe extern "C" fn(*mut c_void) -> bool,
    clear: unsafe extern "C" fn(*mut c_void),
}
/// # Safety
/// The callbacks own one output path and borrow the requested plugin name.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_find(ops: *const Find, enabled: bool, quarantine: bool) {
    let s = &*ops;
    if enabled {
        for root in 0..xp_plugin_roots.0.len() {
            if (s.candidate)(s.ctx, root) {
                if quarantine && (s.quarantined)(s.ctx) {
                    (s.clear)(s.ctx);
                }
                return;
            }
        }
    }
    (s.clear)(s.ctx);
}
#[repr(C)]
pub struct Token {
    ctx: *mut c_void,
    allowed: unsafe extern "C" fn(*mut c_void) -> bool,
    read_parse: Option<unsafe extern "C" fn(*mut c_void) -> bool>,
    extract: Option<unsafe extern "C" fn(*mut c_void) -> bool>,
    segment: Option<unsafe extern "C" fn(*mut c_void, Span, bool)>,
    serialize: Option<unsafe extern "C" fn(*mut c_void) -> usize>,
    open: Option<unsafe extern "C" fn(*mut c_void) -> bool>,
    write: Option<unsafe extern "C" fn(*mut c_void) -> usize>,
    flush: Option<unsafe extern "C" fn(*mut c_void)>,
    close: Option<unsafe extern "C" fn(*mut c_void)>,
    remove: Option<unsafe extern "C" fn(*mut c_void)>,
    short_write: Option<unsafe extern "C" fn(*mut c_void, usize, usize)>,
}
/// # Safety
/// Callbacks borrow the token path, JSON document and output for this call.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_load_token(empty: bool, ops: *const Token) -> bool {
    let s = &*ops;
    if empty {
        return true;
    }
    let (Some(read), Some(extract)) = (s.read_parse, s.extract) else {
        return false;
    };
    (s.allowed)(s.ctx) && read(s.ctx) && extract(s.ctx)
}
/// # Safety
/// Callbacks borrow the token path, JSON document and value for this call.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_save_token(empty: bool, path: Span, ops: *const Token) -> bool {
    let s = &*ops;
    let (
        Some(segment),
        Some(serialize),
        Some(open),
        Some(write),
        Some(flush),
        Some(close),
        Some(remove),
        Some(short_write),
    ) = (
        s.segment,
        s.serialize,
        s.open,
        s.write,
        s.flush,
        s.close,
        s.remove,
        s.short_write,
    )
    else {
        return false;
    };
    if empty || !(s.allowed)(s.ctx) {
        return false;
    }
    let mut parts = path
        .bytes()
        .split(|&b| b == b'.')
        .filter(|s| !s.is_empty())
        .peekable();
    if parts.peek().is_none() {
        return false;
    }
    while let Some(part) = parts.next() {
        segment(s.ctx, Span::new(part), parts.peek().is_none());
    }
    let size = serialize(s.ctx);
    if !open(s.ctx) {
        return false;
    }
    let written = write(s.ctx);
    flush(s.ctx);
    if written != size {
        short_write(s.ctx, written, size);
        close(s.ctx);
        remove(s.ctx);
        return false;
    }
    true
}
#[repr(C)]
pub struct Manifest {
    ctx: *mut c_void,
    text: unsafe extern "C" fn(*mut c_void, u32, Span),
    request: unsafe extern "C" fn(*mut c_void, u32, u32, *const c_char),
    reserve: unsafe extern "C" fn(*mut c_void, usize),
    begin_handler: unsafe extern "C" fn(*mut c_void, i32),
    end_handler: unsafe extern "C" fn(*mut c_void),
}
/// # Safety
/// JSON slots borrow the manifest; callbacks copy fields before slot reuse.
/// Fields: token file/path, config file, auth type/token path, toast, destination.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_manifest(json: *const Json, ops: *const Manifest) {
    let j = &*json;
    let s = &*ops;
    let v = &*j.vtable;
    j.field(0, 1, b"token");
    j.field(1, 2, b"file");
    (s.text)(s.ctx, ManifestField::TokenFile as u32, j.text(2, b""));
    j.field(1, 2, b"path");
    (s.text)(s.ctx, ManifestField::TokenPath as u32, j.text(2, b"token"));
    j.field(0, 1, b"config");
    j.field(1, 2, b"file");
    (s.text)(s.ctx, ManifestField::ConfigFile as u32, j.text(2, b""));
    j.field(0, 1, b"auth");
    j.field(1, 2, b"type");
    (s.text)(
        s.ctx,
        ManifestField::AuthType as u32,
        j.text(2, b"device_code"),
    );
    j.field(1, 2, b"request");
    (s.request)(s.ctx, 0, 2, c"POST".as_ptr());
    j.field(1, 2, b"token_path");
    (s.text)(
        s.ctx,
        ManifestField::AuthTokenPath as u32,
        j.text(2, b"access_token"),
    );
    (s.reserve)(s.ctx, 2);
    j.field(0, 1, b"events");
    let mut i = 0;
    let mut key = Span::new(&[]);
    while (v.pair)(j.ctx, 1, 2, i, &mut key) {
        i += 1;
        let e = xp_plugin_event(key);
        if e < 0 {
            continue;
        }
        (s.begin_handler)(s.ctx, e);
        j.field(2, 3, b"download");
        // Use slot zero for the transient field: root is no longer needed.
        j.field(3, 0, b"url");
        let download = !(v.text)(j.ctx, 0).data.is_null();
        if download {
            (s.request)(s.ctx, 1, 3, c"GET".as_ptr());
            j.field(3, 0, b"dest");
            let dest = j.text(0, b"");
            (s.text)(s.ctx, ManifestField::Dest as u32, dest);
            if dest.len == 0 {
                continue;
            }
        } else {
            j.field(2, 3, b"request");
            (s.request)(s.ctx, 1, 3, c"POST".as_ptr());
        }
        j.field(2, 0, b"toast");
        (s.text)(s.ctx, ManifestField::Toast as u32, j.text(0, b""));
        // Re-select URL after reading toast; request's source node stays in slot 3.
        j.field(3, 0, b"url");
        if j.text(0, b"").len != 0 {
            (s.end_handler)(s.ctx);
        }
    }
}
#[repr(C)]
pub struct Var {
    key: *const c_char,
    value: *const c_char,
}
#[repr(C)]
pub struct Sequence {
    sequence: u32,
}
#[repr(C)]
pub struct Emit {
    ctx: *mut c_void,
    random: unsafe extern "C" fn(*mut c_void) -> u32,
    time: unsafe extern "C" fn(*mut c_void) -> i64,
    text: unsafe extern "C" fn(*mut c_void, *const c_char, *const c_char, bool),
    number: unsafe extern "C" fn(*mut c_void, *const c_char, i64),
    object: unsafe extern "C" fn(*mut c_void, *const c_char),
    serialize: unsafe extern "C" fn(*mut c_void) -> usize,
    append: unsafe extern "C" fn(*mut c_void, *const Subscriber, usize),
    oversize: unsafe extern "C" fn(*mut c_void, usize),
}
/// # Safety
/// Table has eight entries, sequence is exclusively writable, vars has count entries.
/// Text callbacks copy values; the boolean marks static envelope keys.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_emit(
    table: *const [Subscriber; 8],
    sequence: *mut Sequence,
    event: u32,
    vars: *const Var,
    count: usize,
    ops: *const Emit,
) {
    if !xp_plugin_any(table, event) {
        return;
    }
    let s = &*ops;
    (s.text)(s.ctx, c"e".as_ptr(), xp_plugin_event_name(event), true);
    let seq = &mut *sequence;
    let nonce = (s.random)(s.ctx);
    seq.sequence = seq.sequence.wrapping_add(1);
    let mut id = [0u8; 24];
    const HEX: &[u8; 16] = b"0123456789abcdef";
    for (i, b) in id[..8].iter_mut().enumerate() {
        *b = HEX[((nonce >> ((7 - i) * 4)) & 15) as usize];
    }
    id[8] = b'-';
    let mut digits = [0u8; 10];
    let mut n = seq.sequence;
    let mut used = 0;
    loop {
        digits[used] = b'0' + (n % 10) as u8;
        used += 1;
        n /= 10;
        if n == 0 {
            break;
        }
    }
    for i in 0..used {
        id[9 + i] = digits[used - 1 - i];
    }
    (s.text)(s.ctx, c"id".as_ptr(), id.as_ptr().cast(), true);
    (s.number)(s.ctx, c"ts".as_ptr(), (s.time)(s.ctx));
    (s.object)(s.ctx, c"vars".as_ptr());
    for i in 0..count {
        let v = &*vars.add(i);
        (s.text)(s.ctx, v.key, v.value, false);
    }
    let len = (s.serialize)(s.ctx);
    if len > 512 {
        (s.oversize)(s.ctx, len);
        return;
    }
    let bit = xp_plugin_event_mask(event);
    for sub in &*table {
        if sub.name[0] != 0 && sub.mask & bit != 0 {
            (s.append)(s.ctx, sub, len);
        }
    }
}
#[repr(C)]
pub struct Substitution {
    ctx: *mut c_void,
    pair: unsafe extern "C" fn(*mut c_void, u32, usize, *mut Span, *mut Span) -> bool,
    replace: unsafe extern "C" fn(*mut c_void, Span, Span, bool),
}
/// # Safety
/// Input spans and callbacks remain valid for this synchronous template expansion.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_event_substitute(
    token: Span,
    ts: Span,
    id: Span,
    ops: *const Substitution,
) {
    let s = &*ops;
    (s.replace)(s.ctx, Span::new(b"{token}"), token, false);
    let (mut key, mut value) = (Span::new(&[]), Span::new(&[]));
    for group in 0..2 {
        let mut i = 0;
        while (s.pair)(s.ctx, group, i, &mut key, &mut value) {
            (s.replace)(s.ctx, key, value, false);
            i += 1;
        }
    }
    (s.replace)(s.ctx, Span::new(b"{event.ts}"), ts, false);
    (s.replace)(s.ctx, Span::new(b"{event.id}"), id, false);
    let mut i = 0;
    while (s.pair)(s.ctx, 2, i, &mut key, &mut value) {
        (s.replace)(s.ctx, key, value, true);
        i += 1;
    }
}
#[repr(C)]
pub struct DrainFile {
    ctx: *mut c_void,
    exists: unsafe extern "C" fn(*mut c_void) -> bool,
    read: unsafe extern "C" fn(*mut c_void) -> bool,
    manifest: unsafe extern "C" fn(*mut c_void) -> bool,
    handlers: unsafe extern "C" fn(*mut c_void) -> usize,
    remove: unsafe extern "C" fn(*mut c_void),
    prepare: unsafe extern "C" fn(*mut c_void),
    lines: unsafe extern "C" fn(*mut c_void, *mut usize),
}
/// # Safety
/// Callbacks borrow one outbox, manifest and token; budget is exclusively writable.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_drain_file(budget: *mut usize, ops: *const DrainFile) {
    let s = &*ops;
    if !(s.exists)(s.ctx) || !(s.read)(s.ctx) || !(s.manifest)(s.ctx) {
        return;
    }
    if (s.handlers)(s.ctx) == 0 {
        (s.remove)(s.ctx);
        return;
    }
    (s.prepare)(s.ctx);
    (s.lines)(s.ctx, budget);
}
/// # Safety
/// The table is readable and drain consumes one subscriber synchronously.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_drain_all(
    table: *const [Subscriber; 8],
    mut budget: usize,
    ctx: *mut c_void,
    drain: unsafe extern "C" fn(*mut c_void, *const Subscriber, *mut usize),
) {
    for sub in &*table {
        if budget == 0 {
            break;
        }
        if sub.name[0] != 0 {
            drain(ctx, sub, &mut budget);
        }
    }
}
/// # Safety
/// event_at accepts indices below count. usize::MAX means the line is discarded.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_pick_handler(
    parsed: bool,
    event: i32,
    count: usize,
    ctx: *mut c_void,
    event_at: unsafe extern "C" fn(*mut c_void, usize) -> i32,
) -> usize {
    if !parsed {
        return usize::MAX;
    }
    (0..count)
        .find(|&i| event_at(ctx, i) == event)
        .unwrap_or(usize::MAX)
}
#[repr(C)]
pub struct Config {
    ctx: *mut c_void,
    reserve: unsafe extern "C" fn(*mut c_void, usize),
    append: unsafe extern "C" fn(*mut c_void, Span, u32),
}
/// # Safety
/// JSON slots and the output container remain valid during the call.
#[no_mangle]
pub unsafe extern "C" fn xp_plugin_config_fields(json: *const Json, ops: *const Config) {
    let j = &*json;
    let s = &*ops;
    let v = &*j.vtable;
    let mut key = Span::new(&[]);
    let mut count = 0;
    while (v.pair)(j.ctx, 0, 1, count, &mut key) {
        count += 1;
    }
    (s.reserve)(s.ctx, count);
    for i in 0..count {
        (v.pair)(j.ctx, 0, 1, i, &mut key);
        (s.append)(s.ctx, key, 1);
    }
}

impl Subscriptions {
    unsafe fn diagnostic(&self, event: u32, index: usize, value: usize) {
        if let Some(callback) = self.diagnostic {
            callback(self.ctx, event, index, value);
        }
    }
}

impl Append {
    unsafe fn diagnostic(&self, event: u32) {
        if let Some(callback) = self.diagnostic {
            callback(self.ctx, event);
        }
    }
}

impl Drain {
    unsafe fn diagnostic(&self, event: u32, stalled: bool) {
        if let Some(callback) = self.diagnostic {
            callback(self.ctx, event, stalled);
        }
    }
}
