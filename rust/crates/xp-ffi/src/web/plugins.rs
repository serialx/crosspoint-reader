//! Browser plugin routes over the shared transport and existing plugin services.
use super::{
    c_bytes, component, config::Asset, Field, Io, Request, Result, Span, Text, Transfer, Transfers,
};
use core::ffi::{c_char, c_void};

unsafe extern "C" {
    fn xp_plugin_path_candidate(path: *const u8, len: usize) -> bool;
}
#[repr(C)]
struct Entry {
    name: Span,
    dir: Span,
    js: bool,
    manifest: bool,
}
#[repr(C)]
pub struct Upload {
    bytes: usize,
    error: *const c_char,
    status: u16,
    started: bool,
}
#[repr(C)]
pub struct Plugins {
    transfers: *const Transfers,
    parse: unsafe extern "C" fn(*mut c_void, Span) -> Span,
    json_string: unsafe extern "C" fn(*mut c_void, *const c_char) -> Span,
    expires: unsafe extern "C" fn(*mut c_void) -> i64,
    scan: unsafe extern "C" fn(*mut c_void) -> usize,
    entry: unsafe extern "C" fn(*mut c_void, usize, *mut Entry),
    find: unsafe extern "C" fn(*mut c_void, *const c_char) -> Span,
    read: unsafe extern "C" fn(*mut c_void, *const c_char) -> Span,
    decode: unsafe extern "C" fn(Span, *mut u8, usize) -> i32,
    store: unsafe extern "C" fn(*const c_char, *const u8, usize, i64) -> bool,
    page: Asset,
    diagnostic: unsafe extern "C" fn(*mut c_void, u32, Span, Span),
}
const EMPTY: Span = Span {
    data: core::ptr::null(),
    len: 0,
};
struct PluginRequest<'a> {
    t: Transfer<'a>,
    p: &'a Plugins,
}
impl PluginRequest<'_> {
    fn r(&self) -> &Request<'_> {
        &self.t.r
    }
    fn event(&self, event: u32, a: &[u8], b: &[u8]) {
        unsafe { (self.p.diagnostic)(self.r().ctx, event, Span::new(a), Span::new(b)) }
    }
    fn json(&self, code: u16, body: &[u8]) {
        unsafe {
            (self.r().services.send)(
                self.r().ctx,
                code,
                c"application/json".as_ptr(),
                Span::new(body),
            )
        }
    }
    fn disabled(&self) -> bool {
        if self.r().flags() & 32 != 0 {
            return false;
        }
        self.r().send(403, b"plugin system disabled");
        true
    }
    fn value(&self, key: &core::ffi::CStr) -> Span {
        unsafe { (self.p.json_string)(self.r().ctx, key.as_ptr()) }
    }
    fn plugin_path(&self, path: &[u8]) -> bool {
        (unsafe { xp_plugin_path_candidate(path.as_ptr(), path.len()) })
            && !unsafe { (self.r().services.sensitive)(self.r().ctx, path.as_ptr().cast()) }
    }
    fn list(&self) -> Result<()> {
        let count = unsafe { (self.p.scan)(self.r().ctx) };
        let mut out = Text::new(b"[")?;
        let mut comma = false;
        for index in 0..count {
            let mut entry = Entry {
                name: EMPTY,
                dir: EMPTY,
                js: false,
                manifest: false,
            };
            unsafe {
                (self.p.entry)(self.r().ctx, index, &mut entry);
            }
            if !entry.js {
                continue;
            }
            let name = unsafe { entry.name.bytes() };
            let dir = unsafe { entry.dir.bytes() };
            let mut title = entry.name;
            let mut mount = Span::new(b"settings");
            if entry.manifest {
                let mut path = Text::new(dir)?;
                path.append(b"/manifest.json")?;
                let manifest = unsafe { (self.p.read)(self.r().ctx, path.ptr()) };
                if !manifest.data.is_null()
                    && unsafe { (self.p.parse)(self.r().ctx, manifest) }
                        .data
                        .is_null()
                {
                    let value = self.value(c"title");
                    if !value.data.is_null() {
                        title = value;
                    }
                    let value = self.value(c"mount");
                    if !value.data.is_null() {
                        mount = value;
                    }
                }
            }
            if comma {
                out.append(b",")?;
            }
            comma = true;
            out.append(b"{\"name\":")?;
            out.quoted(name)?;
            out.append(b",\"dir\":")?;
            out.quoted(dir)?;
            out.append(b",\"title\":")?;
            out.quoted(unsafe { title.bytes() })?;
            out.append(b",\"mount\":")?;
            out.quoted(unsafe { mount.bytes() })?;
            out.append(b"}")?;
            unsafe {
                (self.p.read)(self.r().ctx, core::ptr::null());
            }
        }
        out.append(b"]")?;
        self.json(200, out.bytes());
        Ok(())
    }
    fn file(&self) -> Result<()> {
        let name = self.r().arg_or(b"name\0", b"")?;
        let file = self.r().arg_or(b"file\0", b"")?;
        if !component(name.bytes()) || !component(file.bytes()) {
            self.r().send(400, b"bad plugin path");
            return Ok(());
        }
        let dir = unsafe { (self.p.find)(self.r().ctx, name.ptr()) };
        if dir.len == 0 {
            self.r().send(404, b"not found");
            return Ok(());
        }
        let mut path = Text::new(unsafe { dir.bytes() })?;
        path.append(b"/")?;
        path.append(c_bytes(file.bytes()))?;
        if self.r().fs(Io::Open, 0, &path) == 0
            || self.r().file(Io::IsOpen, 0) == 0
            || self.r().file(Io::IsDir, 0) != 0
        {
            self.r().send(404, b"not found");
            return Ok(());
        }
        let mime = if file.bytes().ends_with(b".js") {
            c"application/javascript"
        } else if file.bytes().ends_with(b".css") {
            c"text/css"
        } else if file.bytes().ends_with(b".html") {
            c"text/html"
        } else if file.bytes().ends_with(b".json") {
            c"application/json"
        } else if file.bytes().ends_with(b".svg") {
            c"image/svg+xml"
        } else {
            c"application/octet-stream"
        };
        unsafe {
            (self.r().services.length)(self.r().ctx, self.r().file(Io::Size, 0));
            (self.r().services.send)(self.r().ctx, 200, mime.as_ptr(), EMPTY);
        }
        self.r().stream();
        Ok(())
    }
    fn key(&self) -> Result<()> {
        if self.disabled() {
            return Ok(());
        }
        let Some(body) = self.r().arg(b"plain\0")? else {
            self.json(400, b"{\"error\":\"missing body\"}");
            return Ok(());
        };
        if !unsafe { (self.p.parse)(self.r().ctx, Span::new(body.bytes())) }
            .data
            .is_null()
        {
            self.json(400, b"{\"error\":\"bad json\"}");
            return Ok(());
        }
        drop(body);
        let path = Text::new(c_bytes(unsafe { self.value(c"path").bytes() }))?;
        let key = self.value(c"key");
        let key = Span::new(c_bytes(unsafe { key.bytes() }));
        let expires = unsafe { (self.p.expires)(self.r().ctx) };
        let mut decoded = [0; 32];
        let len = unsafe { (self.p.decode)(key, decoded.as_mut_ptr(), decoded.len()) };
        if !self.plugin_path(path.bytes()) || !matches!(len, 16 | 32) || expires < 0 {
            self.json(400, b"{\"error\":\"bad path/key\"}");
        } else if !unsafe { (self.p.store)(path.ptr(), decoded.as_ptr(), len as usize, expires) } {
            self.json(500, b"{\"error\":\"cannot store key\"}");
        } else {
            self.json(200, b"{\"ok\":true}");
        }
        Ok(())
    }
    fn fail(&self, state: &mut Upload, status: u16, error: &'static core::ffi::CStr) {
        self.r().file(Io::Close, 0);
        let tmp = self.t.get(Field::PluginTmp);
        if !tmp.is_empty() {
            self.r()
                .io(Io::Remove, 0, tmp.as_ptr().cast(), core::ptr::null_mut(), 0);
        }
        state.status = status;
        state.error = error.as_ptr();
    }
    fn upload(&self, s: &mut Upload, event: u32, data: &[u8]) -> Result<()> {
        match event {
            0 => {
                self.r().file(Io::Close, 0);
                self.t.set(Field::PluginTmp, b"");
                s.bytes = 0;
                s.started = true;
                s.status = 0;
                s.error = core::ptr::null();
                let path = self.r().arg_or(b"path\0", b"")?;
                self.t.set(Field::PluginPath, c_bytes(path.bytes()));
                if self.r().flags() & 32 == 0 {
                    self.fail(s, 403, c"plugin system disabled");
                    return Ok(());
                }
                let plugin = self.r().arg_or(b"plugin\0", b"")?;
                let path = self.t.get(Field::PluginPath);
                if !component(plugin.bytes()) || !self.plugin_path(path) {
                    self.event(0, c_bytes(plugin.bytes()), c_bytes(path));
                    self.fail(s, 400, c"bad path");
                    return Ok(());
                }
                if let Some(slash) = path.iter().rposition(|&b| b == b'/').filter(|&n| n > 0) {
                    let parent = Text::new(path.get(..slash).unwrap_or(&[]))?;
                    self.r().fs(Io::EnsureDir, 0, &parent);
                }
                let mut tmp = Text::new(path)?;
                tmp.append(b".tmp")?;
                self.t.set(Field::PluginTmp, tmp.bytes());
                self.r().fs(Io::Remove, 0, &tmp);
                if self.r().fs(Io::Create, 0, &tmp) == 0 {
                    self.fail(s, 500, c"cannot write");
                }
            }
            1 => {
                if s.status != 0 {
                    return Ok(());
                }
                if data.len() > (256usize * 1024).saturating_sub(s.bytes) {
                    self.fail(s, 413, c"too large");
                    return Ok(());
                }
                self.r().watchdog();
                if self.r().io(
                    Io::Write,
                    0,
                    core::ptr::null(),
                    data.as_ptr().cast_mut(),
                    data.len(),
                ) != data.len()
                {
                    self.fail(s, 500, c"sd write failed");
                    return Ok(());
                }
                s.bytes += data.len();
            }
            2 => {
                if s.status != 0 {
                    return Ok(());
                }
                self.r().file(Io::Close, 0);
                let tmp = self.t.get(Field::PluginTmp);
                let path = self.t.get(Field::PluginPath);
                if s.bytes == 0 {
                    self.fail(s, 400, c"empty body");
                } else if self.r().io(
                    Io::Replace,
                    0,
                    tmp.as_ptr().cast(),
                    path.as_ptr().cast_mut(),
                    0,
                ) == 0
                {
                    self.fail(s, 500, c"sd write failed");
                } else {
                    unsafe {
                        (self.r().services.book)(
                            self.r().ctx,
                            3,
                            path.as_ptr().cast(),
                            core::ptr::null(),
                        );
                    }
                }
            }
            3 => self.fail(s, 400, c"upload aborted"),
            4 => {
                if !s.started && self.disabled() {
                    return Ok(());
                }
                if !s.started {
                    self.json(400, b"{\"error\":\"missing file part\"}");
                } else if s.status != 0 {
                    let mut bytes = [0; 64];
                    let mut out = super::Output::new(&mut bytes);
                    out.add(b"{\"error\":\"");
                    out.add(unsafe { core::ffi::CStr::from_ptr(s.error).to_bytes() });
                    out.add(b"\"}");
                    self.json(s.status, out.bytes());
                } else {
                    let mut bytes = [0; 48];
                    let mut out = super::Output::new(&mut bytes);
                    out.add(b"{\"ok\":true,\"bytes\":");
                    out.number(s.bytes);
                    out.add(b"}");
                    self.json(200, out.bytes());
                }
                s.started = false;
            }
            _ => {}
        }
        Ok(())
    }
}
/// # Safety
/// Descriptors and context are exclusive synchronous loans as specified in xp_web.h.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn xp_web_plugins(ctx: *mut c_void, plugins: *const Plugins, route: u32) {
    let p = unsafe { &*plugins };
    let api = unsafe { &*p.transfers };
    let r = PluginRequest {
        t: Transfer {
            r: Request {
                ctx,
                services: unsafe { &*api.files },
            },
            t: api,
        },
        p,
    };
    let result = match route {
        0 => r.list(),
        1 => r.file(),
        2 => r.key(),
        3 => {
            if !r.disabled() {
                r.r().page(&p.page);
                r.event(1, b"", b"");
            }
            Ok(())
        }
        _ => return,
    };
    if result.is_err() {
        r.event(2, b"", b"");
        r.json(503, b"{\"error\":\"out of memory\"}");
    }
}
/// # Safety
/// State and payload are exclusive loans; response event 4 needs no multipart data.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn xp_web_plugin_upload(
    ctx: *mut c_void,
    plugins: *const Plugins,
    state: *mut Upload,
    event: u32,
    data: *const u8,
    len: usize,
) {
    let p = unsafe { &*plugins };
    let api = unsafe { &*p.transfers };
    let r = PluginRequest {
        t: Transfer {
            r: Request {
                ctx,
                services: unsafe { &*api.files },
            },
            t: api,
        },
        p,
    };
    let s = unsafe { &mut *state };
    let data = unsafe { Span { data, len }.bytes() };
    if r.upload(s, event, data).is_err() {
        r.fail(s, 503, c"out of memory");
    }
}
