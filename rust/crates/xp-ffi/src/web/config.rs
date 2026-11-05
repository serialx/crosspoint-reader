//! Web settings and credential policy. Store and descriptor owners remain C++.
use super::{Output, Request, Services, Span};
use core::ffi::{c_char, c_void};

const EMPTY: Span = Span {
    data: core::ptr::null(),
    len: 0,
};
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
impl Setting {
    const EMPTY: Self = Self {
        key: EMPTY,
        name: EMPTY,
        category: EMPTY,
        text: EMPTY,
        kind: 4,
        options: 0,
        value: 0,
        min: 0,
        max: 0,
        step: 0,
        hidden: false,
        has_value: false,
        obfuscated: false,
        off_on: false,
    };
}
#[repr(C)]
struct JsonValue {
    text: Span,
    integer: i32,
    present: bool,
    string: bool,
    is_int: bool,
    array: bool,
}
#[repr(C)]
#[derive(Clone, Copy)]
struct Header {
    name: Span,
    value: Span,
}
#[repr(C)]
#[derive(Clone, Copy)]
struct Opds {
    name: Span,
    url: Span,
    username: Span,
    password: Span,
    headers: [Header; 2],
}
impl Opds {
    const EMPTY: Self = Self {
        name: EMPTY,
        url: EMPTY,
        username: EMPTY,
        password: EMPTY,
        headers: [Header {
            name: EMPTY,
            value: EMPTY,
        }; 2],
    };
}
#[repr(C)]
struct Wifi {
    ssid: Span,
    password: Span,
    has_password: bool,
    last_connected: bool,
}
impl Wifi {
    const EMPTY: Self = Self {
        ssid: EMPTY,
        password: EMPTY,
        has_password: false,
        last_connected: false,
    };
}
#[repr(C)]
pub struct Config {
    files: *const Services,
    parse: unsafe extern "C" fn(*mut c_void, Span) -> Span,
    json_value: unsafe extern "C" fn(*mut c_void, *const c_char, i32, bool) -> JsonValue,
    count: unsafe extern "C" fn(*mut c_void, u32) -> usize,
    setting: unsafe extern "C" fn(*mut c_void, usize, bool, *mut Setting),
    option: unsafe extern "C" fn(*mut c_void, usize, usize) -> Span,
    set_setting: unsafe extern "C" fn(*mut c_void, usize, i32),
    save_settings: unsafe extern "C" fn(*mut c_void),
    opds: unsafe extern "C" fn(*mut c_void, usize, *mut Opds) -> bool,
    put_opds: unsafe extern "C" fn(*mut c_void, i32, *const Opds) -> bool,
    remove_opds: unsafe extern "C" fn(*mut c_void, usize),
    wifi: unsafe extern "C" fn(*mut c_void, usize, u32, *mut Wifi) -> bool,
    put_wifi: unsafe extern "C" fn(*mut c_void, Span, Span) -> bool,
    remove_wifi: unsafe extern "C" fn(*mut c_void, Span) -> bool,
    page: Asset,
    diagnostic: unsafe extern "C" fn(*mut c_void, u32, usize, Span),
}
#[repr(C)]
pub struct Asset {
    request_header: unsafe extern "C" fn(*mut c_void, *const c_char) -> Span,
    send_asset: unsafe extern "C" fn(*mut c_void, Span, *const c_char),
    asset: Span,
    etag: Span,
    content_type: *const c_char,
}
impl Request<'_> {
    pub(super) fn page(&self, page: &Asset) {
        let etag = unsafe { (page.request_header)(self.ctx, c"If-None-Match".as_ptr()) };
        // Arduino String == const char* compares through the first NUL.
        let cached = unsafe {
            etag.bytes().split(|&byte| byte == 0).next()
                == page.etag.bytes().split(|&byte| byte == 0).next()
        };
        unsafe { (self.services.argument)(self.ctx, core::ptr::null()) };
        if !cached {
            self.header(b"Content-Encoding\0", b"gzip");
        }
        self.header(b"ETag\0", unsafe { page.etag.bytes() });
        self.header(b"Cache-Control\0", b"no-cache");
        unsafe {
            if cached {
                (self.services.send)(self.ctx, 304, c"text/html".as_ptr(), EMPTY);
            } else {
                (page.send_asset)(self.ctx, page.asset, page.content_type);
            }
        }
    }
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum Event {
    Page,
    Settings,
    Skipped,
    Applied,
    OpdsList,
    OpdsUpdated,
    OpdsAdded,
    OpdsDeleted,
    WifiList,
    WifiUpdated,
    WifiAdded,
    WifiDeleted,
}
struct ConfigRequest<'a> {
    r: Request<'a>,
    c: &'a Config,
}
impl Output<'_> {
    fn boolean(&mut self, value: bool) {
        self.add(if value { b"true" } else { b"false" });
    }
    fn signed(&mut self, value: i32) {
        if value < 0 {
            self.add(b"-");
        }
        self.number(value.unsigned_abs() as usize);
    }
    fn field(&mut self, key: &[u8], value: Span) {
        self.add(key);
        self.quoted(unsafe { value.bytes() });
    }
}
impl ConfigRequest<'_> {
    fn event(&self, event: Event, count: usize, text: Span) {
        unsafe {
            (self.c.diagnostic)(self.r.ctx, event as u32, count, text);
        }
    }
    fn value(&self, key: &core::ffi::CStr) -> JsonValue {
        unsafe { (self.c.json_value)(self.r.ctx, key.as_ptr(), -1, false) }
    }
    fn parse(&self) -> bool {
        let body = unsafe { (self.r.services.argument)(self.r.ctx, c"plain".as_ptr()) };
        if body.data.is_null() {
            self.r.send(400, b"Missing JSON body");
            return false;
        }
        let error = unsafe { (self.c.parse)(self.r.ctx, body) };
        unsafe {
            (self.r.services.argument)(self.r.ctx, core::ptr::null());
        }
        if error.len != 0 {
            let mut bytes = [0u8; 96];
            let mut out = Output::new(&mut bytes);
            out.add(b"Invalid JSON: ");
            out.add(unsafe { error.bytes() });
            self.r.send(400, out.bytes());
            return false;
        }
        true
    }
    fn count(&self, route: u32) -> usize {
        unsafe { (self.c.count)(self.r.ctx, route) }
    }
    fn begin_array(&self) {
        unsafe {
            (self.r.services.length)(self.r.ctx, usize::MAX);
            (self.r.services.send)(self.r.ctx, 200, c"application/json".as_ptr(), EMPTY);
        }
        self.r.chunk(b"[");
    }
    fn end_array(&self, event: Event, count: usize) {
        self.r.chunk(b"]");
        self.r.chunk(b"");
        self.event(event, count, EMPTY);
    }
    fn item(&self, out: &Output<'_>, comma: bool) {
        if comma {
            self.r.chunk(b",");
        }
        self.r.chunk(out.bytes());
        unsafe {
            (self.r.services.yield_task)(self.r.ctx);
        }
        self.r.watchdog();
    }
    #[inline(never)]
    fn settings_get(&self) {
        let count = self.count(0);
        self.begin_array();
        let mut seen = false;
        for index in 0..count {
            let mut s = Setting::EMPTY;
            unsafe {
                (self.c.setting)(self.r.ctx, index, true, &mut s);
            }
            if s.key.data.is_null() || s.hidden || s.kind > 3 {
                continue;
            }
            let mut bytes = [0u8; 512];
            let mut out = Output::new(&mut bytes);
            out.field(b"{\"key\":", s.key);
            out.field(b",\"name\":", s.name);
            out.field(b",\"category\":", s.category);
            out.add(b",\"type\":");
            out.quoted(match s.kind {
                0 => b"toggle",
                1 if s.off_on => b"toggle",
                1 => b"enum",
                2 => b"value",
                _ => b"string",
            });
            if s.kind == 3 {
                if s.obfuscated {
                    out.add(b",\"value\":null,\"hasPassword\":");
                    out.boolean(s.text.len != 0);
                } else if s.has_value {
                    out.field(b",\"value\":", s.text);
                }
            } else {
                if s.has_value {
                    out.add(b",\"value\":");
                    out.signed(s.value);
                }
                if s.kind == 1 && !s.off_on {
                    out.add(b",\"options\":[");
                    for option in 0..s.options as usize {
                        if option != 0 {
                            out.add(b",");
                        }
                        let value = unsafe { (self.c.option)(self.r.ctx, index, option) };
                        out.quoted(unsafe { value.bytes() });
                    }
                    out.add(b"]");
                } else if s.kind == 2 {
                    out.add(b",\"min\":");
                    out.signed(s.min);
                    out.add(b",\"max\":");
                    out.signed(s.max);
                    out.add(b",\"step\":");
                    out.signed(s.step);
                }
            }
            out.add(b"}");
            if out.len >= 512 {
                self.event(Event::Skipped, 0, s.key);
            } else {
                self.item(&out, seen);
                seen = true;
            }
        }
        self.end_array(Event::Settings, 0);
    }
    fn settings_post(&self) {
        if !self.parse() {
            return;
        }
        let count = self.count(1);
        let mut applied = 0;
        for index in 0..count {
            let mut s = Setting::EMPTY;
            unsafe {
                (self.c.setting)(self.r.ctx, index, false, &mut s);
            }
            if s.key.data.is_null() || s.hidden || s.kind > 3 {
                continue;
            }
            let value =
                unsafe { (self.c.json_value)(self.r.ctx, s.key.data.cast(), -1, s.kind == 3) };
            if !value.present {
                continue;
            }
            let number = value.integer;
            let number = match s.kind {
                0 => i32::from(number != 0),
                1 if number < 0 || number as u32 >= s.options => continue,
                2 if number < s.min || number > s.max => continue,
                _ => number,
            };
            unsafe {
                (self.c.set_setting)(self.r.ctx, index, number);
            }
            applied += 1;
        }
        unsafe {
            (self.c.save_settings)(self.r.ctx);
        }
        self.event(Event::Applied, applied, EMPTY);
        let mut bytes = [0u8; 64];
        let mut out = Output::new(&mut bytes);
        out.add(b"Applied ");
        out.number(applied);
        out.add(b" setting(s)");
        self.r.send(200, out.bytes());
    }
    #[inline(never)]
    fn credentials_get(&self, wifi: bool) {
        let count = self.count(if wifi { 5 } else { 2 });
        self.begin_array();
        for index in 0..count {
            let mut bytes = [0u8; 512];
            let capacity = if wifi { 320 } else { 512 };
            let mut out = Output::new(&mut bytes);
            out.add(b"{\"index\":");
            out.number(index);
            if wifi {
                let mut value = Wifi::EMPTY;
                if !unsafe { (self.c.wifi)(self.r.ctx, index, 0, &mut value) } {
                    continue;
                }
                out.field(b",\"ssid\":", value.ssid);
                out.add(b",\"hasPassword\":");
                out.boolean(value.has_password);
                out.add(b",\"isLastConnected\":");
                out.boolean(value.last_connected);
            } else {
                let mut value = Opds::EMPTY;
                if !unsafe { (self.c.opds)(self.r.ctx, index, &mut value) } {
                    continue;
                }
                out.field(b",\"name\":", value.name);
                out.field(b",\"url\":", value.url);
                out.field(b",\"username\":", value.username);
                out.add(b",\"hasPassword\":");
                out.boolean(value.password.len != 0);
                out.add(b",\"headers\":[");
                for (i, h) in value.headers.iter().enumerate() {
                    if i != 0 {
                        out.add(b",");
                    }
                    out.field(b"{\"name\":", h.name);
                    out.add(b",\"hasValue\":");
                    out.boolean(h.value.len != 0);
                    out.add(b"}");
                }
                out.add(b"]");
            }
            out.add(b"}");
            // Preserve the original index-based comma even if a prior item was skipped.
            if out.len < capacity {
                self.item(&out, index != 0);
            }
        }
        self.end_array(
            if wifi {
                Event::WifiList
            } else {
                Event::OpdsList
            },
            count,
        );
    }
    fn opds_post(&self) {
        if !self.parse() {
            return;
        }
        let mut value = Opds::EMPTY;
        value.name = self.value(c"name").text;
        value.url = self.value(c"url").text;
        value.username = self.value(c"username").text;
        let password = self.value(c"password");
        value.password = password.text;
        let headers = self.value(c"headers").array;
        let index = self.value(c"index");
        let mut old = Opds::EMPTY;
        let update = index.is_int;
        if update {
            if index.integer < 0 || index.integer as usize >= self.count(3) {
                self.r.send(400, b"Invalid server index");
                return;
            }
            unsafe {
                (self.c.opds)(self.r.ctx, index.integer as usize, &mut old);
            }
            if !password.string {
                value.password = old.password;
            }
        }
        for (i, h) in value.headers.iter_mut().enumerate() {
            if headers {
                let name =
                    unsafe { (self.c.json_value)(self.r.ctx, c"name".as_ptr(), i as i32, false) };
                let val =
                    unsafe { (self.c.json_value)(self.r.ctx, c"value".as_ptr(), i as i32, false) };
                h.name = name.text;
                h.value = if update && name.text.len == 0 {
                    EMPTY
                } else if !update || val.string {
                    val.text
                } else {
                    old.headers.get(i).map_or(EMPTY, |h| h.value)
                };
            } else if update {
                *h = old.headers.get(i).copied().unwrap_or(Header {
                    name: EMPTY,
                    value: EMPTY,
                });
            }
        }
        if !unsafe {
            (self.c.put_opds)(self.r.ctx, if update { index.integer } else { -1 }, &value)
        } {
            self.r.send(400, b"Cannot add server (limit reached)");
            return;
        }
        self.event(
            if update {
                Event::OpdsUpdated
            } else {
                Event::OpdsAdded
            },
            index.integer as usize,
            value.name,
        );
        self.r.send(200, b"OK");
    }
    fn wifi_post(&self) {
        if !self.parse() {
            return;
        }
        let ssid = self.value(c"ssid").text;
        if ssid.len == 0 {
            self.r.send(400, b"SSID is required");
            return;
        }
        let password = self.value(c"password");
        let index = self.value(c"index");
        let mut pass = password.text;
        if index.is_int {
            let mut old = Wifi::EMPTY;
            if index.integer < 0
                || !unsafe { (self.c.wifi)(self.r.ctx, index.integer as usize, 1, &mut old) }
            {
                self.r.send(400, b"Invalid network index");
                return;
            }
            if !password.string {
                pass = old.password;
            }
            let renamed = unsafe { old.ssid.bytes() != ssid.bytes() };
            let removed = !renamed || unsafe { (self.c.remove_wifi)(self.r.ctx, old.ssid) };
            if !removed || !unsafe { (self.c.put_wifi)(self.r.ctx, ssid, pass) } {
                self.r.send(400, b"Failed to update Wi-Fi network");
                return;
            }
            self.event(Event::WifiUpdated, index.integer as usize, ssid);
        } else {
            if !unsafe { (self.c.put_wifi)(self.r.ctx, ssid, pass) } {
                self.r.send(400, b"Cannot add network (limit reached)");
                return;
            }
            self.event(Event::WifiAdded, 0, ssid);
        }
        self.r.send(200, b"OK");
    }
    fn delete(&self, wifi: bool) {
        if !self.parse() {
            return;
        }
        let index = self.value(c"index");
        if !index.is_int {
            self.r.send(400, b"Missing index");
            return;
        }
        let mut old = Wifi::EMPTY;
        let valid = index.integer >= 0
            && if wifi {
                unsafe { (self.c.wifi)(self.r.ctx, index.integer as usize, 2, &mut old) }
            } else {
                (index.integer as usize) < self.count(4)
            };
        if !valid {
            self.r.send(
                400,
                if wifi {
                    b"Invalid network index"
                } else {
                    b"Invalid server index"
                },
            );
            return;
        }
        if wifi {
            if !unsafe { (self.c.remove_wifi)(self.r.ctx, old.ssid) } {
                self.r.send(400, b"Failed to delete Wi-Fi network");
                return;
            }
            self.event(Event::WifiDeleted, index.integer as usize, old.ssid);
        } else {
            unsafe {
                (self.c.remove_opds)(self.r.ctx, index.integer as usize);
            }
            self.event(Event::OpdsDeleted, index.integer as usize, EMPTY);
        }
        self.r.send(200, b"OK");
    }
    fn page(&self) {
        self.r.page(&self.c.page);
        self.event(Event::Page, 0, EMPTY);
    }
}
/// # Safety
/// The context, descriptors, and callback loans must remain valid for the call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn xp_web_config(ctx: *mut c_void, config: *const Config, route: u32) {
    let Some(c) = (unsafe { config.as_ref() }) else {
        return;
    };
    let Some(services) = (unsafe { c.files.as_ref() }) else {
        return;
    };
    let request = ConfigRequest {
        r: Request { ctx, services },
        c,
    };
    match route {
        0 => request.settings_get(),
        1 => request.settings_post(),
        2 => request.credentials_get(false),
        3 => request.opds_post(),
        4 => request.delete(false),
        5 => request.credentials_get(true),
        6 => request.wifi_post(),
        7 => request.delete(true),
        8 => request.page(),
        _ => {}
    }
}
