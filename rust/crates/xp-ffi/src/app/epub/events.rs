//! Reader payload formatting; JSON and file resources stay native and synchronous.
use super::{abi::XpEpubSessionEvent, app_abi::Text as XpAppText};
use crate::plugin::{self, Emit, Event, Ops, Sequence, Subscriber, Var};
use core::{
    ffi::{c_char, c_void},
    marker::PhantomPinned,
    mem::MaybeUninit,
    pin::pin,
};

// Match xp_plugin.h; the native constructors assert their size/alignment fits.
#[repr(C, align(8))]
pub struct Storage<const WORDS: usize> {
    bytes: [MaybeUninit<usize>; WORDS],
    pinned: PhantomPinned,
}
impl<const WORDS: usize> Storage<WORDS> {
    fn new() -> Self {
        Self {
            bytes: [MaybeUninit::uninit(); WORDS],
            pinned: PhantomPinned,
        }
    }
}
pub type Codec = unsafe extern "C" fn(*mut Storage<24>, *mut Emit) -> *mut Sequence;
pub type Append = unsafe extern "C" fn(*mut Storage<12>, *mut c_void, *const Subscriber, *mut Ops);
#[repr(C)]
pub(super) struct Ports {
    pub subscribers: unsafe extern "C" fn() -> *const Subscriber,
    pub codec: Codec,
    pub append: Append,
}
struct Bridge {
    native: Emit,
    append: Append,
}

// The codec context stays native; only append crosses into the Rust file owner.
macro_rules! forward {
    ($name:ident($($arg:ident: $ty:ty),*) -> $ret:ty) => {
        unsafe extern "C" fn $name(ctx: *mut c_void, $($arg: $ty),*) -> $ret {
            let b = &*ctx.cast::<Bridge>();
            (b.native.$name)(b.native.ctx, $($arg),*)
        }
    };
}
forward!(random() -> u32);
forward!(time() -> i64);
forward!(text(key: *const c_char, value: *const c_char, envelope: bool) -> ());
forward!(number(key: *const c_char, value: i64) -> ());
forward!(object(key: *const c_char) -> ());
forward!(serialize() -> usize);
forward!(oversize(size: usize) -> ());
unsafe extern "C" fn append(ctx: *mut c_void, subscriber: *const Subscriber, size: usize) {
    let b = &*ctx.cast::<Bridge>();
    let mut storage = pin!(Storage::<12>::new());
    // Construct only after pinning; destroy before the backing storage leaves scope.
    let storage = storage.as_mut().get_unchecked_mut() as *mut _;
    let mut ops = MaybeUninit::uninit();
    (b.append)(storage, b.native.ctx, subscriber, ops.as_mut_ptr());
    plugin::xp_plugin_append(ops.as_ptr(), size);
    (b.append)(storage, b.native.ctx, subscriber, core::ptr::null_mut());
}

#[inline(never)]
unsafe fn emit(ports: &Ports, event: Event, vars: &[Var]) {
    let event = event as u32;
    let table = (ports.subscribers)().cast();
    if !plugin::xp_plugin_any(table, event) {
        return;
    }
    let mut storage = pin!(Storage::<24>::new());
    let storage = storage.as_mut().get_unchecked_mut() as *mut _;
    let mut native = MaybeUninit::<Emit>::uninit();
    let sequence = (ports.codec)(storage, native.as_mut_ptr());
    // Native deliberately leaves append null. Fill it before forming a Rust Emit.
    core::ptr::addr_of_mut!((*native.as_mut_ptr()).append).write(append);
    let mut bridge = Bridge {
        native: native.assume_init(),
        append: ports.append,
    };
    let ops = Emit {
        ctx: (&mut bridge as *mut Bridge).cast(),
        random,
        time,
        text,
        number,
        object,
        serialize,
        append,
        oversize,
    };
    plugin::xp_plugin_emit(table, sequence, event, vars.as_ptr(), vars.len(), &ops);
    (ports.codec)(storage, core::ptr::null_mut());
}

unsafe fn terminated(text: XpAppText) -> *const c_char {
    if text.len == 0 {
        c"".as_ptr()
    } else {
        text.data.cast()
    }
}
// Decimal strings include their terminator; sizes match the native payload buffers.
fn decimal<const N: usize>(value: i64) -> [u8; N] {
    let mut out = [0; N];
    let mut n = value.unsigned_abs();
    let mut used = 0;
    loop {
        out[used] = b'0' + (n % 10) as u8;
        used += 1;
        n /= 10;
        if n == 0 {
            break;
        }
    }
    if value < 0 {
        out[used] = b'-';
        used += 1;
    }
    out[..used].reverse();
    out
}
fn var(key: &core::ffi::CStr, value: *const c_char) -> Var {
    Var {
        key: key.as_ptr(),
        value,
    }
}
#[inline(never)]
pub(super) unsafe fn open(ports: &Ports, path: XpAppText) {
    emit(ports, Event::ReaderOpen, &[var(c"book", terminated(path))]);
}
// percent comes from export_percent and is in 0–100.
#[inline(never)]
pub(super) unsafe fn exit(ports: &Ports, path: XpAppText, percent: i32) {
    let percent = decimal::<8>(percent.into());
    emit(
        ports,
        Event::ReaderExit,
        &[
            var(c"book", terminated(path)),
            var(c"percent", percent.as_ptr().cast()),
        ],
    );
}
#[inline(never)]
pub(super) unsafe fn session(
    ports: &Ports,
    path: XpAppText,
    document: XpAppText,
    event: &XpEpubSessionEvent,
) {
    let start = decimal::<24>(event.start_time);
    let end = decimal::<24>(event.end_time);
    let duration = decimal::<16>(event.duration_seconds.into());
    let from = decimal::<8>(event.start_progress.into());
    let to = decimal::<8>(event.end_progress.into());
    emit(
        ports,
        Event::ReaderSession,
        &[
            var(c"book", terminated(path)),
            var(c"document", terminated(document)),
            var(c"start_time", start.as_ptr().cast()),
            var(c"end_time", end.as_ptr().cast()),
            var(c"duration_seconds", duration.as_ptr().cast()),
            var(c"start_progress_bp", from.as_ptr().cast()),
            var(c"end_progress_bp", to.as_ptr().cast()),
            var(c"progress_scale", c"10000".as_ptr()),
        ],
    );
}
