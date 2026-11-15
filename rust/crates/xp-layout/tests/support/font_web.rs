//! Native-layout descriptor oracle for the activated production web font entry.
use super::*;
use core::ffi::{c_char, CStr};
#[repr(C)]
#[derive(Clone, Copy)]
struct Span {
    data: *const u8,
    len: usize,
}
impl Span {
    const EMPTY: Self = Self {
        data: ptr::null(),
        len: 0,
    };
    unsafe fn bytes(self) -> &'static [u8] {
        if self.len == 0 {
            &[]
        } else {
            core::slice::from_raw_parts(self.data, self.len)
        }
    }
}
#[repr(C)]
struct Services {
    argument: unsafe extern "C" fn(*mut c_void, *const c_char) -> Span,
    send: unsafe extern "C" fn(*mut c_void, u16, *const c_char, Span),
    header: unsafe extern "C" fn(*mut c_void, *const c_char, Span),
    chunk: unsafe extern "C" fn(*mut c_void, Span),
    length: unsafe extern "C" fn(*mut c_void, usize),
    io: unsafe extern "C" fn(*mut c_void, u32, u32, *const c_char, *mut u8, usize) -> usize,
    sensitive: unsafe extern "C" fn(*mut c_void, *const c_char) -> bool,
    book: unsafe extern "C" fn(*mut c_void, u32, *const c_char, *const c_char) -> bool,
    buffer: unsafe extern "C" fn(*mut c_void, bool) -> *mut u8,
    watchdog: unsafe extern "C" fn(*mut c_void),
    yield_task: unsafe extern "C" fn(*mut c_void),
    flags: unsafe extern "C" fn(*mut c_void) -> u32,
    paths: unsafe extern "C" fn(*mut c_void, bool) -> isize,
    path: unsafe extern "C" fn(*mut c_void, usize) -> Span,
    diagnostic: unsafe extern "C" fn(*mut c_void, u32, Span, Span),
    memory_error: unsafe extern "C" fn(*mut c_void) -> *const c_char,
}
#[repr(C)]
struct Transfers {
    files: *const Services,
    get: unsafe extern "C" fn(*mut c_void, u32) -> Span,
    set: unsafe extern "C" fn(*mut c_void, u32, Span),
    joined_path: unsafe extern "C" fn(*mut c_void, bool) -> Span,
    time: unsafe extern "C" fn(*mut c_void) -> u32,
    cancel: unsafe extern "C" fn(*mut c_void) -> bool,
    diagnostic: unsafe extern "C" fn(*mut c_void, u32, Span, Span, usize, usize, usize),
    ws_send: unsafe extern "C" fn(*mut c_void, u8, Span) -> bool,
    ws_memory_error: unsafe extern "C" fn(*mut c_void, u8) -> bool,
}
#[repr(C)]
struct Asset {
    request_header: unsafe extern "C" fn(*mut c_void, *const c_char) -> Span,
    send_asset: unsafe extern "C" fn(*mut c_void, Span, *const c_char),
    asset: Span,
    etag: Span,
    content_type: *const c_char,
}
#[repr(C)]
struct InstallRequest {
    family: *const c_char,
    filename: *const c_char,
    root: *const c_char,
}
#[repr(C)]
struct Fonts {
    transfers: *const Transfers,
    parse: unsafe extern "C" fn(*mut c_void, Span) -> Span,
    json_string: unsafe extern "C" fn(*mut c_void, *const c_char) -> Span,
    registry: unsafe extern "C" fn(*mut c_void, *mut bool) -> *const Owner,
    install: unsafe extern "C" fn(*mut c_void, u32, *const InstallRequest) -> bool,
    page: Asset,
    diagnostic: unsafe extern "C" fn(*mut c_void, u32, Span, Span, usize, bool),
}
unsafe extern "C" {
    fn xp_web_fonts(ctx: *mut c_void, fonts: *const Fonts, route: u32);
}
// Mirrors XpWebIo in rust/crates/xp-ffi/include/xp_web.h; keep the order in sync.
#[repr(u32)]
#[derive(Clone, Copy)]
enum Io {
    Exists,
    Open,
    Close,
    IsOpen,
    IsDir,
    Size,
    Next,
    Name,
    Read,
    Write,
    Mkdir,
    Remove,
    Rmdir,
    ClientWrite,
    ClientClear,
    Create,
    Available,
    OpenRead,
    EnsureDir,
    Replace,
    OpenAppend,
    Flush,
    CreatePlugin,
}
const CLOSE: u32 = Io::Close as u32;
const IS_OPEN: u32 = Io::IsOpen as u32;
const SIZE: u32 = Io::Size as u32;
const CREATE: u32 = Io::Create as u32;
const OPEN_READ: u32 = Io::OpenRead as u32;
// The web server owns one HalFile per slot.
const SLOTS: usize = 3;
struct Context {
    owner: *const Owner,
    response: Vec<u8>,
    paths: Vec<Vec<u8>>,
    events: Vec<u32>,
    // Production indexes a separate HalFile by slot: an open attempt installs the impl, close
    // only ends I/O, so the impl survives repeated close and never-opened slots have none.
    fail_open: bool,
    impls: [bool; SLOTS],
    open: [bool; SLOTS],
}
fn slot_index(slot: u32) -> usize {
    assert!((slot as usize) < SLOTS, "unexpected slot {slot}");
    slot as usize
}
fn require_impl(c: &Context, slot: u32, operation: &str) {
    assert!(
        c.impls[slot_index(slot)],
        "{operation} on a slot with no HAL handle"
    );
}
fn slot_io(c: &mut Context, op: u32, slot: u32, path: *const c_char) -> usize {
    if slot as usize >= SLOTS {
        return 0;
    }
    match op {
        OPEN_READ => {
            // HalStorage installs the impl for every open attempt.
            c.paths
                .push(unsafe { CStr::from_ptr(path) }.to_bytes().to_vec());
            c.impls[slot_index(slot)] = true;
            c.open[slot_index(slot)] = !c.fail_open && !unsafe { CStr::from_ptr(path) }.is_empty();
            usize::from(c.open[slot_index(slot)])
        }
        SIZE => {
            require_impl(c, slot, "Size");
            if c.open[slot_index(slot)] {
                73
            } else {
                0
            }
        }
        CLOSE => {
            require_impl(c, slot, "Close");
            c.open[slot_index(slot)] = false;
            0
        }
        _ => panic!("unexpected I/O {op}"),
    }
}
fn upload_slot_io(c: &mut Context, op: u32, slot: u32, path: *const c_char) -> usize {
    if slot as usize >= SLOTS {
        return 0;
    }
    match op {
        IS_OPEN => usize::from(c.open[slot_index(slot)]),
        CREATE => {
            // Create opens the upload target, so the slot holds an impl from here on.
            c.paths
                .push(unsafe { CStr::from_ptr(path) }.to_bytes().to_vec());
            c.impls[slot_index(slot)] = true;
            c.open[slot_index(slot)] = !c.fail_open && !unsafe { CStr::from_ptr(path) }.is_empty();
            usize::from(c.open[slot_index(slot)])
        }
        CLOSE => {
            require_impl(c, slot, "Close");
            c.open[slot_index(slot)] = false;
            0
        }
        _ => panic!("I/O {op}"),
    }
}
unsafe extern "C" fn span(_: *mut c_void, _: *const c_char) -> Span {
    Span::EMPTY
}
unsafe extern "C" fn send(ctx: *mut c_void, code: u16, kind: *const c_char, body: Span) {
    assert_eq!(code, 200);
    assert_eq!(CStr::from_ptr(kind), c"application/json");
    (*ctx.cast::<Context>()).response = body.bytes().to_vec();
}
unsafe extern "C" fn io(
    ctx: *mut c_void,
    op: u32,
    slot: u32,
    path: *const c_char,
    _: *mut u8,
    _: usize,
) -> usize {
    slot_io(&mut *ctx.cast::<Context>(), op, slot, path)
}
unsafe extern "C" fn owner(ctx: *mut c_void, vectors: *mut bool) -> *const Owner {
    *vectors = false;
    (*ctx.cast::<Context>()).owner
}
unsafe extern "C" fn event(ctx: *mut c_void, op: u32, _: Span, _: Span, _: usize, _: bool) {
    (*ctx.cast::<Context>()).events.push(op);
}
macro_rules! unused {
    ($name:ident($($arg:ident: $t:ty),* $(,)?) $(-> $out:ty)?) => {
        unsafe extern "C" fn $name($(_: $t),*) $(-> $out)? { panic!(stringify!($name)) }
    }
}
unused!(header(ctx: *mut c_void, key: *const c_char, value: Span));
unused!(chunk(ctx: *mut c_void, bytes: Span));
unused!(length(ctx: *mut c_void, n: usize));
unused!(sensitive(ctx: *mut c_void, path: *const c_char) -> bool);
unused!(book(ctx: *mut c_void, op: u32, a: *const c_char, b: *const c_char) -> bool);
unused!(buffer(ctx: *mut c_void, allocate: bool) -> *mut u8);
unused!(task(ctx: *mut c_void));
unused!(flags(ctx: *mut c_void) -> u32);
unused!(paths(ctx: *mut c_void, dirs: bool) -> isize);
unused!(path(ctx: *mut c_void, i: usize) -> Span);
unused!(diagnostic(ctx: *mut c_void, op: u32, a: Span, b: Span));
unused!(memory(ctx: *mut c_void) -> *const c_char);
unused!(get(ctx: *mut c_void, field: u32) -> Span);
unused!(set(ctx: *mut c_void, field: u32, text: Span));
unused!(joined(ctx: *mut c_void, tmp: bool) -> Span);
unused!(time(ctx: *mut c_void) -> u32);
unused!(cancel(ctx: *mut c_void) -> bool);
unused!(transfer_event(ctx: *mut c_void, op: u32, a: Span, b: Span, n: usize, m: usize, t: usize));
unused!(ws_send(ctx: *mut c_void, client: u8, bytes: Span) -> bool);
unused!(ws_memory(ctx: *mut c_void, client: u8) -> bool);
unused!(parse(ctx: *mut c_void, text: Span) -> Span);
unused!(install(ctx: *mut c_void, op: u32, request: *const InstallRequest) -> bool);
unused!(asset(ctx: *mut c_void, text: Span, kind: *const c_char));

#[test]
fn activated_font_list_matches_native_response_and_file_operation_order() {
    unsafe {
        catalog_cases::reset(0, 0);
        let generation = AtomicU32::new(1);
        let mut storage = catalog_cases::new_owner(&generation);
        let owner = storage.as_mut_ptr();
        font_sd::catalog::xp_sd_catalog_dirty(owner, 2, false);
        let services = Services {
            argument: span,
            send,
            header,
            chunk,
            length,
            io,
            sensitive,
            book,
            buffer,
            watchdog: task,
            yield_task: task,
            flags,
            paths,
            path,
            diagnostic,
            memory_error: memory,
        };
        let transfers = Transfers {
            files: &services,
            get,
            set,
            joined_path: joined,
            time,
            cancel,
            diagnostic: transfer_event,
            ws_send,
            ws_memory_error: ws_memory,
        };
        let fonts = Fonts {
            transfers: &transfers,
            parse,
            json_string: span,
            registry: self::owner,
            install,
            page: Asset {
                request_header: span,
                send_asset: asset,
                asset: Span::EMPTY,
                etag: Span::EMPTY,
                content_type: c"text/html".as_ptr(),
            },
            diagnostic: event,
        };
        let mut context = Context {
            owner,
            response: Vec::new(),
            paths: Vec::new(),
            events: Vec::new(),
            fail_open: false,
            impls: [false; SLOTS],
            open: [false; SLOTS],
        };
        xp_web_fonts((&mut context as *mut Context).cast(), &fonts, 0);
        assert_eq!(
            String::from_utf8(context.response).unwrap(),
            r#"{"families":[{"name":"ShortFont","sizes":[14,18],"files":[{"name":"ShortFont_14.cpfont","size":73},{"name":"ShortFont_18.cpfont","size":73}]}],"maxFamilies":128,"vectorFonts":false}"#
        );
        assert_eq!(
            context.paths,
            [
                b"/.fonts/ShortFont/ShortFont_14.cpfont".to_vec(),
                b"/.fonts/ShortFont/ShortFont_18.cpfont".to_vec()
            ]
        );
        assert!(context.events.is_empty());
        font_registry::xp_font_registry_drop(owner);
    }
}

#[repr(C)]
struct Setting {
    key: Span,
    name: Span,
    category: Span,
    text: Span,
    kind: u32,
    options: u32,
    value: i32,
    min: i32,
    max: i32,
    step: i32,
    hidden: bool,
    has_value: bool,
    obfuscated: bool,
    off_on: bool,
}
// Unused callbacks remain valid function addresses; this route never invokes them.
#[repr(C)]
struct Config {
    files: *const Services,
    parse: unsafe extern "C" fn(),
    json_value: unsafe extern "C" fn(),
    count: unsafe extern "C" fn(
        *mut c_void,
        u32,
        web::FontChoicesStage,
        *mut font_sd::catalog::ChoiceView,
    ) -> usize,
    setting: unsafe extern "C" fn(*mut c_void, usize, bool, *mut Setting),
    option: unsafe extern "C" fn(*mut c_void, usize, usize) -> Span,
    set_setting: unsafe extern "C" fn(),
    save_settings: unsafe extern "C" fn(),
    opds: unsafe extern "C" fn(),
    put_opds: unsafe extern "C" fn(),
    remove_opds: unsafe extern "C" fn(),
    wifi: unsafe extern "C" fn(),
    put_wifi: unsafe extern "C" fn(),
    remove_wifi: unsafe extern "C" fn(),
    page: Asset,
    diagnostic: unsafe extern "C" fn(*mut c_void, u32, usize, Span),
}
unsafe extern "C" {
    fn xp_web_config(ctx: *mut c_void, config: *const Config, route: u32);
}
unsafe extern "C" fn not_used() {
    panic!("unexpected configuration callback");
}
unsafe extern "C" fn config_count(
    ctx: *mut c_void,
    route: u32,
    stage: web::FontChoicesStage,
    choices: *mut font_sd::catalog::ChoiceView,
) -> usize {
    assert_eq!(route, 0);
    let stage = stage as u32;
    let c = &mut *ctx.cast::<Context>();
    c.events.push(stage);
    if stage == 0 {
        (*choices).owner = c.owner;
        (*choices).family = c"ShortFont".as_ptr().cast();
        (*choices).length = 9;
        0
    } else {
        assert_eq!((*choices).count, 1);
        assert_eq!((*choices).sizes[0], (1 << 14) | (1 << 18));
        assert_eq!((*choices).reserve, 0);
        2
    }
}
fn text(s: &'static [u8]) -> Span {
    Span {
        data: s.as_ptr(),
        len: s.len(),
    }
}
unsafe extern "C" fn config_setting(_: *mut c_void, index: usize, read: bool, out: *mut Setting) {
    assert!(read);
    *out = Setting {
        key: text(if index == 0 {
            b"fontFamily"
        } else {
            b"fontSize"
        }),
        name: text(if index == 0 {
            b"Font Family"
        } else {
            b"Font Size"
        }),
        category: text(b"Reader"),
        text: Span::EMPTY,
        kind: 1,
        options: 2,
        value: if index == 0 { 1 } else { 0 },
        min: 0,
        max: 0,
        step: 0,
        hidden: false,
        has_value: true,
        obfuscated: false,
        off_on: false,
    };
}
unsafe extern "C" fn option(_: *mut c_void, index: usize, option: usize) -> Span {
    text(match (index, option) {
        (0, 0) => b"Noto Sans",
        (0, 1) => b"ShortFont",
        (1, 0) => b"14 pt",
        (1, 1) => b"18 pt",
        _ => panic!("option"),
    })
}
unsafe extern "C" fn append(ctx: *mut c_void, bytes: Span) {
    (*ctx.cast::<Context>())
        .response
        .extend_from_slice(bytes.bytes());
}
unsafe extern "C" fn config_length(_: *mut c_void, n: usize) {
    assert_eq!(n, usize::MAX);
}
unsafe extern "C" fn config_task(_: *mut c_void) {}
unsafe extern "C" fn config_event(ctx: *mut c_void, op: u32, count: usize, _: Span) {
    assert_eq!((op, count), (1, 0));
    (*ctx.cast::<Context>()).events.push(100);
}
#[test]
fn activated_config_borrows_then_copies_rust_choices_and_keeps_response_bytes() {
    unsafe {
        catalog_cases::reset(0, 0);
        let generation = AtomicU32::new(1);
        let mut storage = catalog_cases::new_owner(&generation);
        let owner = storage.as_mut_ptr();
        font_sd::catalog::xp_sd_catalog_dirty(owner, 2, false);
        let services = Services {
            argument: span,
            send,
            header,
            chunk: append,
            length: config_length,
            io,
            sensitive,
            book,
            buffer,
            watchdog: config_task,
            yield_task: config_task,
            flags,
            paths,
            path,
            diagnostic,
            memory_error: memory,
        };
        let config = Config {
            files: &services,
            parse: not_used,
            json_value: not_used,
            count: config_count,
            setting: config_setting,
            option,
            set_setting: not_used,
            save_settings: not_used,
            opds: not_used,
            put_opds: not_used,
            remove_opds: not_used,
            wifi: not_used,
            put_wifi: not_used,
            remove_wifi: not_used,
            page: Asset {
                request_header: span,
                send_asset: asset,
                asset: Span::EMPTY,
                etag: Span::EMPTY,
                content_type: c"text/html".as_ptr(),
            },
            diagnostic: config_event,
        };
        let mut context = Context {
            owner,
            response: Vec::new(),
            paths: Vec::new(),
            events: Vec::new(),
            fail_open: false,
            impls: [false; SLOTS],
            open: [false; SLOTS],
        };
        xp_web_config((&mut context as *mut Context).cast(), &config, 0);
        assert_eq!(
            String::from_utf8(context.response).unwrap(),
            r#"[{"key":"fontFamily","name":"Font Family","category":"Reader","type":"enum","value":1,"options":["Noto Sans","ShortFont"]},{"key":"fontSize","name":"Font Size","category":"Reader","type":"enum","value":0,"options":["14 pt","18 pt"]}]"#
        );
        assert_eq!(context.events, [0, 1, 100]);
        assert!(context.paths.is_empty());
        font_registry::xp_font_registry_drop(owner);
    }
}

#[repr(C)]
struct UploadContext {
    base: Context,
    family: Vec<u8>,
    path: Vec<u8>,
    buffer: [u8; 4096],
}
#[repr(C)]
struct FontUpload {
    bytes: usize,
    position: usize,
    buffer: *mut u8,
    valid: bool,
    magic: bool,
    vector: bool,
}
unsafe extern "C" {
    fn xp_web_font_upload(
        ctx: *mut c_void,
        fonts: *const Fonts,
        state: *mut FontUpload,
        event: u32,
        name: Span,
        data: *const u8,
        length: usize,
    );
}
unsafe extern "C" fn upload_argument(_: *mut c_void, key: *const c_char) -> Span {
    if key.is_null() {
        return Span::EMPTY;
    }
    assert_eq!(CStr::from_ptr(key), c"family");
    text(b"ShortFont")
}
unsafe extern "C" fn upload_set(ctx: *mut c_void, field: u32, bytes: Span) {
    let c = ctx.cast::<UploadContext>();
    let target = match field {
        6 => &mut (*c).family,
        7 => &mut (*c).path,
        _ => panic!("field {field}"),
    };
    *target = bytes.bytes().to_vec();
    target.push(0);
}
unsafe extern "C" fn upload_get(ctx: *mut c_void, field: u32) -> Span {
    let c = ctx.cast::<UploadContext>();
    let value = match field {
        6 => &(*c).family,
        7 => &(*c).path,
        _ => panic!("field {field}"),
    };
    Span {
        data: value.as_ptr(),
        len: value.len().saturating_sub(1),
    }
}
unsafe extern "C" fn upload_buffer(ctx: *mut c_void, allocate: bool) -> *mut u8 {
    if allocate {
        ptr::addr_of_mut!((*ctx.cast::<UploadContext>()).buffer).cast()
    } else {
        ptr::null_mut()
    }
}
unsafe extern "C" fn upload_io(
    ctx: *mut c_void,
    op: u32,
    slot: u32,
    path: *const c_char,
    _: *mut u8,
    _: usize,
) -> usize {
    upload_slot_io(&mut *ctx.cast::<Context>(), op, slot, path)
}
unsafe extern "C" fn upload_install(
    ctx: *mut c_void,
    op: u32,
    request: *const InstallRequest,
) -> bool {
    let r = &*request;
    if op == 0 || op == 3 {
        assert_eq!(CStr::from_ptr(r.family), c"ShortFont");
    }
    match op {
        0 => {
            assert!(r.root.is_null());
            assert!(r.filename.is_null());
        }
        1 => {
            assert!(r.root.is_null());
            assert_eq!(CStr::from_ptr(r.filename), c"ShortFont_14.cpfont");
        }
        3 => {
            assert_eq!(CStr::from_ptr(r.root), c"/.fonts");
            assert!(r.filename.is_null());
        }
        _ => panic!("unexpected native policy operation {op}"),
    }
    (*ctx.cast::<Context>()).events.push(10 + op);
    true
}
#[test]
fn activated_upload_selects_root_and_path_before_native_storage_effect() {
    unsafe {
        catalog_cases::reset(0, 0);
        let generation = AtomicU32::new(1);
        let mut storage = catalog_cases::new_owner(&generation);
        let owner = storage.as_mut_ptr();
        let services = Services {
            argument: upload_argument,
            send,
            header,
            chunk,
            length,
            io: upload_io,
            sensitive,
            book,
            buffer: upload_buffer,
            watchdog: config_task,
            yield_task: config_task,
            flags,
            paths,
            path,
            diagnostic,
            memory_error: memory,
        };
        let transfers = Transfers {
            files: &services,
            get: upload_get,
            set: upload_set,
            joined_path: joined,
            time,
            cancel,
            diagnostic: transfer_event,
            ws_send,
            ws_memory_error: ws_memory,
        };
        let fonts = Fonts {
            transfers: &transfers,
            parse,
            json_string: span,
            registry: self::owner,
            install: upload_install,
            page: Asset {
                request_header: span,
                send_asset: asset,
                asset: Span::EMPTY,
                etag: Span::EMPTY,
                content_type: c"text/html".as_ptr(),
            },
            diagnostic: event,
        };
        let mut context = Box::new(UploadContext {
            base: Context {
                owner,
                response: Vec::new(),
                paths: Vec::new(),
                events: Vec::new(),
                fail_open: false,
                impls: [false; SLOTS],
                open: [false; SLOTS],
            },
            family: Vec::new(),
            path: Vec::new(),
            buffer: [0; 4096],
        });
        let raw = (&mut *context as *mut UploadContext).cast();
        let mut state = FontUpload {
            bytes: 0,
            position: 0,
            buffer: ptr::null_mut(),
            valid: false,
            magic: false,
            vector: false,
        };
        xp_web_font_upload(
            raw,
            &fonts,
            &mut state,
            0,
            text(b"ShortFont_14.cpfont"),
            ptr::null(),
            0,
        );
        assert!(state.valid);
        assert!(!state.vector);
        assert_eq!(context.family, b"ShortFont\0");
        assert_eq!(context.path, b"/.fonts/ShortFont/ShortFont_14.cpfont\0");
        assert_eq!(context.base.events, [10, 11, 13, 6]);
        assert_eq!(
            context.base.paths,
            [b"/.fonts/ShortFont/ShortFont_14.cpfont".to_vec()]
        );
        font_registry::xp_font_registry_drop(owner);
    }
}

fn slot_context() -> Context {
    Context {
        owner: ptr::null(),
        response: Vec::new(),
        paths: Vec::new(),
        events: Vec::new(),
        fail_open: false,
        impls: [false; SLOTS],
        open: [false; SLOTS],
    }
}

// Production indexes one HalFile per slot: open installs the impl, close only ends I/O, and
// a slot that no open touched has none. These pin both directions of that contract.
#[test]
fn listen_open_close_close_and_size_after_close_are_valid() {
    let mut c = slot_context();
    assert_eq!(slot_io(&mut c, OPEN_READ, 0, c"a.cpfont".as_ptr()), 1);
    assert_eq!(slot_io(&mut c, CLOSE, 0, ptr::null()), 0);
    assert_eq!(slot_io(&mut c, CLOSE, 0, ptr::null()), 0); // close keeps the impl
    assert_eq!(slot_io(&mut c, SIZE, 0, ptr::null()), 0); // closed file size
}

#[test]
#[should_panic(expected = "Close on a slot with no HAL handle")]
fn close_on_a_never_opened_slot_is_rejected() {
    let mut c = slot_context();
    slot_io(&mut c, CLOSE, 1, ptr::null());
}

#[test]
#[should_panic(expected = "Close on a slot with no HAL handle")]
fn close_on_a_slot_other_than_the_opened_one_is_rejected() {
    let mut c = slot_context();
    assert_eq!(slot_io(&mut c, OPEN_READ, 0, c"a.cpfont".as_ptr()), 1);
    slot_io(&mut c, CLOSE, 1, ptr::null());
}

#[test]
#[should_panic(expected = "Size on a slot with no HAL handle")]
fn size_on_a_never_opened_slot_is_rejected() {
    let mut c = slot_context();
    slot_io(&mut c, SIZE, 0, ptr::null());
}

#[test]
fn upload_create_then_is_open_then_repeated_close_is_valid() {
    let mut c = slot_context();
    assert_eq!(upload_slot_io(&mut c, CREATE, 0, c"a.cpfont".as_ptr()), 1);
    assert_eq!(upload_slot_io(&mut c, IS_OPEN, 0, ptr::null()), 1);
    assert_eq!(upload_slot_io(&mut c, CLOSE, 0, ptr::null()), 0);
    assert_eq!(upload_slot_io(&mut c, IS_OPEN, 0, ptr::null()), 0);
    assert_eq!(upload_slot_io(&mut c, CLOSE, 0, ptr::null()), 0);
}

#[test]
#[should_panic(expected = "Close on a slot with no HAL handle")]
fn upload_close_without_create_is_rejected() {
    let mut c = slot_context();
    upload_slot_io(&mut c, CLOSE, 0, ptr::null());
}

#[test]
fn all_native_slots_and_out_of_range_indices() {
    for slot in 0..3 {
        let mut c = slot_context();
        assert_eq!(upload_slot_io(&mut c, IS_OPEN, slot, ptr::null()), 0);
        assert_eq!(slot_io(&mut c, OPEN_READ, slot, c"a.cpfont".as_ptr()), 1);
        assert_eq!(slot_io(&mut c, SIZE, slot, ptr::null()), 73);
        slot_io(&mut c, CLOSE, slot, ptr::null());
        assert_eq!(
            upload_slot_io(&mut c, CREATE, slot, c"a.cpfont".as_ptr()),
            1
        );
        assert_eq!(upload_slot_io(&mut c, IS_OPEN, slot, ptr::null()), 1);
        upload_slot_io(&mut c, CLOSE, slot, ptr::null());
        upload_slot_io(&mut c, CLOSE, slot, ptr::null());
        assert_eq!(upload_slot_io(&mut c, IS_OPEN, slot, ptr::null()), 0);
    }
    for slot in [3, u32::MAX] {
        let mut c = slot_context();
        for op in [OPEN_READ, SIZE, CLOSE] {
            assert_eq!(slot_io(&mut c, op, slot, ptr::null()), 0);
        }
        for op in [CREATE, IS_OPEN, CLOSE] {
            assert_eq!(upload_slot_io(&mut c, op, slot, ptr::null()), 0);
        }
        assert!(c.paths.is_empty());
    }
}

#[test]
fn every_slot_preserves_failed_open_cleanup_and_its_own_guards() {
    use std::panic::{catch_unwind, AssertUnwindSafe};
    for slot in 0..3 {
        for op in [SIZE, CLOSE] {
            let mut c = slot_context();
            assert!(
                catch_unwind(AssertUnwindSafe(|| slot_io(&mut c, op, slot, ptr::null()))).is_err()
            );
        }
        let mut c = slot_context();
        assert!(catch_unwind(AssertUnwindSafe(|| upload_slot_io(
            &mut c,
            CLOSE,
            slot,
            ptr::null()
        )))
        .is_err());
        for empty in [false, true] {
            let path = if empty { c"" } else { c"missing" };
            c.fail_open = !empty;
            assert_eq!(slot_io(&mut c, OPEN_READ, slot, path.as_ptr()), 0);
            assert_eq!(slot_io(&mut c, SIZE, slot, ptr::null()), 0);
            slot_io(&mut c, CLOSE, slot, ptr::null());
            slot_io(&mut c, CLOSE, slot, ptr::null());
            assert_eq!(upload_slot_io(&mut c, CREATE, slot, path.as_ptr()), 0);
            assert_eq!(upload_slot_io(&mut c, IS_OPEN, slot, ptr::null()), 0);
            upload_slot_io(&mut c, CLOSE, slot, ptr::null());
            upload_slot_io(&mut c, CLOSE, slot, ptr::null());
        }
    }
}
