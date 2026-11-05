//! Font endpoint policy over the existing registry, installer and transfer leaves.
use super::{
    config::Asset, Field, Io, Request, Result, Span, Text, Transfer, Transfers, BUFFER_CAPACITY,
};
use core::ffi::{c_char, c_void};

const EMPTY: Span = Span {
    data: core::ptr::null(),
    len: 0,
};
#[repr(C)]
struct Family {
    name: Span,
    sizes: Span,
    files: usize,
}
#[repr(C)]
pub struct Upload {
    bytes: usize,
    buffer_pos: usize,
    buffer: *mut u8,
    valid: bool,
    magic_checked: bool,
    vector: bool,
}
#[repr(C)]
pub struct Fonts {
    transfers: *const Transfers,
    parse: unsafe extern "C" fn(*mut c_void, Span) -> Span,
    json_string: unsafe extern "C" fn(*mut c_void, *const c_char) -> Span,
    registry: unsafe extern "C" fn(*mut c_void, u32) -> usize,
    family: unsafe extern "C" fn(*mut c_void, usize, *mut Family),
    file: unsafe extern "C" fn(*mut c_void, usize, usize) -> Span,
    install: unsafe extern "C" fn(
        *mut c_void,
        u32,
        *const c_char,
        *const c_char,
        *mut u8,
        usize,
    ) -> bool,
    page: Asset,
    diagnostic: unsafe extern "C" fn(*mut c_void, u32, Span, Span, usize, bool),
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum Event {
    Served,
    BadFamily,
    BadName,
    DirFailed,
    Oom,
    OpenFailed,
    Started,
    BadMagic,
    WriteFailed,
    Ended,
    Aborted,
    Complete,
    Deleted,
    DeleteFailed,
    JsonOom,
}
#[repr(u32)]
enum Install {
    ValidFamily,
    ValidCpfont,
    ValidVector,
    EnsureDir,
    BuildPath,
    RemoveFamily,
}
struct FontRequest<'a> {
    transfer: Transfer<'a>,
    fonts: &'a Fonts,
}
impl FontRequest<'_> {
    fn r(&self) -> &Request<'_> {
        &self.transfer.r
    }
    fn registry(&self, op: u32) -> usize {
        unsafe { (self.fonts.registry)(self.r().ctx, op) }
    }
    fn event(&self, event: Event, a: &[u8], b: &[u8], bytes: usize, valid: bool) {
        unsafe {
            (self.fonts.diagnostic)(
                self.r().ctx,
                event as u32,
                Span::new(a),
                Span::new(b),
                bytes,
                valid,
            );
        }
    }
    fn json(&self, code: u16, body: &[u8]) {
        unsafe {
            (self.r().services.send)(
                self.r().ctx,
                code,
                c"application/json".as_ptr(),
                Span::new(body),
            );
        }
    }
    fn install(
        &self,
        op: Install,
        family: *const c_char,
        name: *const c_char,
        out: &mut [u8],
    ) -> bool {
        unsafe {
            (self.fonts.install)(
                self.r().ctx,
                op as u32,
                family,
                name,
                out.as_mut_ptr(),
                out.len(),
            )
        }
    }
    fn list(&self) -> Result<()> {
        let count = self.registry(0);
        let max = self.registry(1);
        let vectors = self.registry(2) != 0;
        let mut out = Text::new(b"{\"families\":[")?;
        for index in 0..count {
            let mut family = Family {
                name: EMPTY,
                sizes: EMPTY,
                files: 0,
            };
            unsafe {
                (self.fonts.family)(self.r().ctx, index, &mut family);
            }
            if index != 0 {
                out.append(b",")?;
            }
            out.append(b"{\"name\":")?;
            out.quoted(unsafe { family.name.bytes() })?;
            out.append(b",\"sizes\":[")?;
            for (i, &size) in unsafe { family.sizes.bytes() }.iter().enumerate() {
                if i != 0 {
                    out.append(b",")?;
                }
                out.number(size as usize)?;
            }
            out.append(b"],\"files\":[")?;
            let mut comma = false;
            for i in 0..family.files {
                let path = unsafe { (self.fonts.file)(self.r().ctx, index, i) };
                if path.data.is_null() {
                    continue;
                }
                if comma {
                    out.append(b",")?;
                }
                comma = true;
                out.append(b"{\"name\":")?;
                let name = unsafe { path.bytes() }
                    .rsplit(|&b| b == b'/')
                    .next()
                    .unwrap_or(&[]);
                out.quoted(name)?;
                out.append(b",\"size\":")?;
                let size =
                    if self
                        .r()
                        .io(Io::OpenRead, 0, path.data.cast(), core::ptr::null_mut(), 0)
                        != 0
                    {
                        let size = self.r().file(Io::Size, 0);
                        self.r().file(Io::Close, 0);
                        size
                    } else {
                        0
                    };
                out.number(size)?;
                out.append(b"}")?;
            }
            out.append(b"]}")?;
        }
        out.append(b"],\"maxFamilies\":")?;
        out.number(max)?;
        out.append(if vectors {
            b",\"vectorFonts\":true}"
        } else {
            b",\"vectorFonts\":false}"
        })?;
        self.json(200, out.bytes());
        Ok(())
    }
    fn delete(&self) {
        let body = unsafe { (self.r().services.argument)(self.r().ctx, c"plain".as_ptr()) };
        let err = unsafe { (self.fonts.parse)(self.r().ctx, body) };
        unsafe {
            (self.r().services.argument)(self.r().ctx, core::ptr::null());
        }
        let name = unsafe { (self.fonts.json_string)(self.r().ctx, c"family".as_ptr()) };
        if err.len != 0 || name.data.is_null() {
            self.json(400, b"{\"error\":\"Invalid request\"}");
            return;
        }
        let ok = self.install(
            Install::RemoveFamily,
            name.data.cast(),
            core::ptr::null(),
            &mut [],
        );
        if ok {
            self.registry(3);
            self.json(200, b"{\"ok\":true}");
        } else {
            self.json(500, b"{\"error\":\"Delete failed\"}");
        }
        self.event(
            if ok {
                Event::Deleted
            } else {
                Event::DeleteFailed
            },
            unsafe { name.bytes() },
            b"",
            0,
            false,
        );
    }
    fn path(&self) -> &[u8] {
        self.transfer.get(Field::FontPath)
    }
    fn release(&self, state: &mut Upload) {
        state.buffer_pos = 0;
        state.buffer = unsafe { (self.r().services.buffer)(self.r().ctx, false) };
    }
    fn remove(&self) {
        let path = self.path();
        if !path.is_empty() {
            self.r().io(
                Io::Remove,
                0,
                path.as_ptr().cast(),
                core::ptr::null_mut(),
                0,
            );
        }
    }
    fn start(&self, state: &mut Upload, name: &[u8]) -> Result<()> {
        self.r().watchdog();
        self.r().file(Io::Close, 0);
        self.transfer.set(Field::FontFamily, b"");
        self.transfer.set(Field::FontPath, b"");
        state.valid = false;
        state.magic_checked = false;
        state.vector = false;
        state.bytes = 0;
        self.release(state);
        let family = self.r().arg_or(b"family\0", b"")?;
        if !self.install(
            Install::ValidFamily,
            family.ptr(),
            core::ptr::null(),
            &mut [],
        ) {
            self.event(Event::BadFamily, family.bytes(), b"", 0, false);
            return Ok(());
        }
        let mut name = Text::new(name)?;
        for byte in &mut name.0 {
            if *byte == b' ' {
                *byte = b'_';
            }
        }
        state.vector = self.registry(2) != 0
            && self.install(Install::ValidVector, core::ptr::null(), name.ptr(), &mut []);
        if !state.vector
            && !self.install(Install::ValidCpfont, core::ptr::null(), name.ptr(), &mut [])
        {
            self.event(Event::BadName, name.bytes(), b"", 0, false);
            return Ok(());
        }
        self.transfer.set(
            Field::FontFamily,
            family
                .bytes()
                .split(|&byte| byte == 0)
                .next()
                .unwrap_or(&[]),
        );
        if !self.install(Install::EnsureDir, family.ptr(), core::ptr::null(), &mut []) {
            self.event(Event::DirFailed, b"", b"", 0, false);
            return Ok(());
        }
        state.buffer = unsafe { (self.r().services.buffer)(self.r().ctx, true) };
        if state.buffer.is_null() {
            self.event(Event::Oom, b"", b"", 0, false);
            return Ok(());
        }
        let mut path = [0; 128];
        self.install(Install::BuildPath, family.ptr(), name.ptr(), &mut path);
        let end = path
            .iter()
            .position(|&b| b == 0)
            .unwrap_or(path.len().saturating_sub(1));
        self.transfer
            .set(Field::FontPath, path.get(..end).unwrap_or(&[]));
        if self.r().io(
            Io::Create,
            0,
            path.as_ptr().cast(),
            core::ptr::null_mut(),
            0,
        ) == 0
        {
            self.event(Event::OpenFailed, self.path(), b"", 0, false);
            self.release(state);
            return Ok(());
        }
        state.valid = true;
        self.event(Event::Started, name.bytes(), self.path(), 0, false);
        Ok(())
    }
    fn flush(&self, state: &mut Upload) -> bool {
        if self.r().io(
            Io::Write,
            0,
            core::ptr::null(),
            state.buffer,
            state.buffer_pos,
        ) != state.buffer_pos
        {
            self.event(Event::WriteFailed, self.path(), b"", 0, false);
            state.valid = false;
            return false;
        }
        true
    }
    fn write(&self, state: &mut Upload, mut data: &[u8]) {
        if unsafe { (self.transfer.t.cancel)(self.r().ctx) } || !state.valid {
            return;
        }
        self.r().watchdog();
        if !state.magic_checked && data.len() >= 8 {
            let valid = if state.vector {
                data.starts_with(b"\0\x01\0\0")
                    || data.starts_with(b"OTTO")
                    || data.starts_with(b"true")
            } else {
                data.starts_with(b"CPFONT\0\0")
            };
            if !valid {
                self.event(Event::BadMagic, b"", b"", 0, false);
                state.valid = false;
                return;
            }
            state.magic_checked = true;
        }
        if state.buffer.is_null() || state.buffer_pos > BUFFER_CAPACITY {
            state.valid = false;
            return;
        }
        while !data.is_empty() {
            let chunk = data.len().min(BUFFER_CAPACITY - state.buffer_pos);
            unsafe {
                core::ptr::copy_nonoverlapping(
                    data.as_ptr(),
                    state.buffer.add(state.buffer_pos),
                    chunk,
                );
            }
            state.buffer_pos += chunk;
            data = data.get(chunk..).unwrap_or(&[]);
            if state.buffer_pos == BUFFER_CAPACITY {
                if !self.flush(state) {
                    break;
                }
                state.bytes = state.bytes.wrapping_add(state.buffer_pos);
                state.buffer_pos = 0;
                self.r().watchdog();
            }
        }
    }
    fn upload(&self, state: &mut Upload, event: u32, name: &[u8], data: &[u8]) -> Result<()> {
        match event {
            0 => self.start(state, name)?,
            1 => self.write(state, data),
            2 => {
                if state.valid && state.buffer_pos != 0 {
                    self.flush(state);
                    state.bytes = state.bytes.wrapping_add(state.buffer_pos);
                    state.buffer_pos = 0;
                }
                if self.r().file(Io::IsOpen, 0) != 0 {
                    self.r().file(Io::Close, 0);
                }
                self.release(state);
                if !state.valid {
                    self.remove();
                }
                self.event(Event::Ended, b"", b"", state.bytes, state.valid);
            }
            3 => {
                self.release(state);
                if self.r().file(Io::IsOpen, 0) != 0 {
                    self.r().file(Io::Close, 0);
                }
                self.remove();
                state.valid = false;
                self.event(Event::Aborted, b"", b"", 0, false);
            }
            4 => {
                if state.valid {
                    self.registry(3);
                    self.json(200, b"{\"ok\":true}");
                    self.event(Event::Complete, self.path(), b"", 0, false);
                } else {
                    self.json(400, b"{\"error\":\"Invalid font file\"}");
                }
            }
            _ => {}
        }
        Ok(())
    }
}
unsafe fn request<'a>(ctx: *mut c_void, fonts: *const Fonts) -> Option<FontRequest<'a>> {
    let fonts = unsafe { fonts.as_ref() }?;
    let t = unsafe { fonts.transfers.as_ref() }?;
    let services = unsafe { t.files.as_ref() }?;
    Some(FontRequest {
        transfer: Transfer {
            r: Request { ctx, services },
            t,
        },
        fonts,
    })
}
/// # Safety
/// All descriptors and callback loans must remain valid for this serialized call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn xp_web_fonts(ctx: *mut c_void, fonts: *const Fonts, route: u32) {
    let Some(r) = (unsafe { request(ctx, fonts) }) else {
        return;
    };
    match route {
        0 => {
            if r.list().is_err() {
                r.event(Event::JsonOom, b"", b"", 0, false);
                r.json(503, b"{\"error\":\"out of memory\"}");
            }
        }
        1 => r.delete(),
        2 => {
            r.r().page(&r.fonts.page);
            r.event(Event::Served, b"", b"", 0, false);
        }
        _ => {}
    }
}
/// # Safety
/// State and input loans must remain live and exclusive until this call returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn xp_web_font_upload(
    ctx: *mut c_void,
    fonts: *const Fonts,
    state: *mut Upload,
    event: u32,
    name: Span,
    data: *const u8,
    length: usize,
) {
    let Some(r) = (unsafe { request(ctx, fonts) }) else {
        return;
    };
    let Some(state) = (unsafe { state.as_mut() }) else {
        return;
    };
    let bytes = if event == 1 && length != 0 {
        unsafe { core::slice::from_raw_parts(data, length) }
    } else {
        &[]
    };
    if r.upload(state, event, unsafe { name.bytes() }, bytes)
        .is_err()
    {
        state.valid = false;
        r.event(Event::Oom, b"", b"", 0, false);
    }
}
