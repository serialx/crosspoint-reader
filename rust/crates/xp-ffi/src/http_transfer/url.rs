use super::{
    abi::*,
    text::{self, Pool, Text},
};

fn find(bytes: &[u8], needle: &[u8]) -> Option<usize> {
    bytes.windows(needle.len()).position(|p| p == needle)
}
fn span(bytes: &[u8]) -> Bytes {
    Bytes::from_slice(bytes)
}

#[inline(never)]
pub unsafe fn origin<'a>(pool: &'a Pool<'a>, url: Bytes) -> Text<'a> {
    let slots = text::slots::<2>();
    let scratch = pool.scope(&slots);
    let url = url.slice();
    let Some(end) = find(url, b"://") else {
        return pool.empty();
    };
    let mut scheme = scratch.copy(span(&url[..end]));
    let host_start = end + 3;
    let host_end = url[host_start..]
        .iter()
        .position(|c| b"/?#".contains(c))
        .map_or(url.len(), |i| i + host_start);
    let mut authority = scratch.copy(span(&url[host_start..host_end]));
    if let Some(at) = authority.slice().iter().rposition(|c| *c == b'@') {
        authority.remove_prefix(at + 1);
    }
    scheme.lower();
    authority.lower();
    let bracket = authority
        .slice()
        .iter()
        .rposition(|c| *c == b']')
        .unwrap_or(0);
    if !authority.slice()[bracket..].contains(&b':') {
        authority.append(span(if scheme.slice() == b"https" {
            b":443"
        } else {
            b":80"
        }));
    }
    let mut result = pool.concat(scheme.bytes(), span(b"://"));
    result.append(authority.bytes());
    result
}

// Field order preserves the reverse destruction order of the native locals.
struct Parsed<'a> {
    path: Text<'a>,
    host: Text<'a>,
    scheme: Text<'a>,
    port: u16,
}
fn port_number(bytes: &[u8]) -> u16 {
    let mut i = 0;
    // atoi uses C whitespace, including vertical tab.
    while i < bytes.len() && matches!(bytes[i], b' ' | b'\t'..=b'\r') {
        i += 1;
    }
    let negative = bytes.get(i) == Some(&b'-');
    if matches!(bytes.get(i), Some(b'+' | b'-')) {
        i += 1;
    }
    let limit = if negative {
        2_147_483_648u32
    } else {
        2_147_483_647
    };
    let mut value = 0u32;
    while let Some(c @ b'0'..=b'9') = bytes.get(i) {
        value = value
            .saturating_mul(10)
            .saturating_add(u32::from(c - b'0'))
            .min(limit);
        i += 1;
    }
    if negative {
        value.wrapping_neg() as u16
    } else {
        value as u16
    }
}
unsafe fn parse<'a>(pool: &'a Pool<'a>, url: Bytes) -> Option<Parsed<'a>> {
    let bytes = url.slice();
    let end = find(bytes, b"://")?;
    let mut scheme = pool.copy(span(&bytes[..end]));
    scheme.lower();
    let start = end + 3;
    let slash = bytes[start..]
        .iter()
        .position(|c| *c == b'/')
        .map(|i| start + i);
    let host_port = pool.copy(span(&bytes[start..slash.unwrap_or(bytes.len())]));
    let path = pool.copy(span(slash.map_or(b"/".as_slice(), |i| &bytes[i..])));
    let (host, port) = if let Some(colon) = host_port.slice().iter().rposition(|c| *c == b':') {
        let host = pool.copy(span(&host_port.slice()[..colon]));
        let port_text = pool.copy(span(&host_port.slice()[colon + 1..]));
        (host, port_number(port_text.slice()))
    } else {
        let mut host = pool.empty();
        host.assign(host_port.bytes());
        (host, if scheme.slice() == b"https" { 443 } else { 80 })
    };
    let valid = !host.slice().is_empty() && matches!(scheme.slice(), b"http" | b"https");
    drop(host_port);
    let parsed = Parsed {
        path,
        host,
        scheme,
        port,
    };
    valid.then_some(parsed)
}
pub fn decimal(mut value: usize, buffer: &mut [u8; 20]) -> &[u8] {
    let mut at = buffer.len();
    loop {
        at -= 1;
        buffer[at] = b'0' + (value % 10) as u8;
        value /= 10;
        if value == 0 {
            return &buffer[at..];
        }
    }
}

#[inline(never)]
pub unsafe fn resolve<'a>(pool: &'a Pool<'a>, current: &mut Text<'a>, location: Bytes) -> bool {
    if find(location.slice(), b"://").is_some() {
        current.assign(location);
        return true;
    }
    let slots = text::slots::<6>();
    let scratch = pool.scope(&slots);
    let Some(parsed) = parse(&scratch, current.bytes()) else {
        return false;
    };
    let default = if parsed.scheme.slice() == b"https" {
        443
    } else {
        80
    };
    let authority = if parsed.port == 0 || parsed.port == default {
        scratch.copy(parsed.host.bytes())
    } else {
        let mut value = scratch.concat(parsed.host.bytes(), span(b":"));
        let mut buffer = [0; 20];
        value.append(span(decimal(usize::from(parsed.port), &mut buffer)));
        value
    };
    if location.slice().first() == Some(&b'/') {
        let mut value = pool.concat(parsed.scheme.bytes(), span(b"://"));
        value.append(authority.bytes());
        value.append(location);
        current.assign_move(value);
        return true;
    }
    let mut parent = scratch.copy(parsed.path.bytes());
    if let Some(slash) = parent.slice().iter().rposition(|c| *c == b'/') {
        let next = scratch.copy(span(&parent.slice()[..slash + 1]));
        parent.assign_move(next);
    } else {
        parent.assign(span(b"/"));
    }
    let mut value = pool.concat(parsed.scheme.bytes(), span(b"://"));
    value.append(authority.bytes());
    value.append(parent.bytes());
    value.append(location);
    current.assign_move(value);
    true
}

unsafe fn protocol<'a>(pool: &'a Pool<'a>, url: Bytes) -> Text<'a> {
    if find(url.slice(), b"://").is_some() {
        pool.copy(url)
    } else {
        pool.concat(span(b"http://"), url)
    }
}
fn host(url: Bytes) -> Bytes {
    unsafe {
        let bytes = url.slice();
        let start = find(bytes, b"://").map_or(0, |i| i + 3);
        let end = bytes[start..]
            .iter()
            .position(|c| *c == b'/')
            .map_or(bytes.len(), |i| start + i);
        span(&bytes[..end])
    }
}
#[inline(never)]
unsafe fn encode(url: Bytes, out: *mut MutableText, leaves: &TextLeaves) {
    text::initialize(leaves, out, 0);
    (leaves.resize)(out, url.length);
    (leaves.resize)(out, 0);
    let bytes = url.slice();
    let mut i = 0;
    while i < bytes.len() {
        let c = bytes[i];
        if c == b'%'
            && i + 2 < bytes.len()
            && bytes[i + 1].is_ascii_hexdigit()
            && bytes[i + 2].is_ascii_hexdigit()
        {
            for j in 0..3 {
                text::output_append(leaves, out, span(&bytes[i + j..i + j + 1]));
            }
            i += 3;
            continue;
        }
        if c == b'%' || c <= 0x20 || c >= 0x7f || b"\"<>\\^`{|}".contains(&c) {
            let hex = b"0123456789ABCDEF";
            let encoded = [b'%', hex[(c >> 4) as usize], hex[(c & 15) as usize]];
            text::output_append(leaves, out, span(&encoded));
        } else {
            text::output_append(leaves, out, span(&bytes[i..i + 1]));
        }
        i += 1;
    }
}
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_url_build_retire_c2(
    base: NativeText,
    path: NativeText,
    out: *mut MutableText,
    leaves: *const TextLeaves,
) {
    if find(path.bytes.slice(), b"://").is_some() {
        encode(path.bytes, out, &*leaves);
        return;
    }
    let slots = text::slots::<3>();
    let pool = Pool::new(&*leaves, &slots);
    let normalized = protocol(&pool, base.bytes);
    if path.bytes.length == 0 {
        encode(normalized.bytes(), out, &*leaves);
        return;
    }
    if path.bytes.slice()[0] == b'/' {
        let mut target = pool.copy(host(normalized.bytes()));
        target.append(path.bytes);
        encode(target.bytes(), out, &*leaves);
        return;
    }
    let mut parent = pool.copy(normalized.bytes());
    if let Some(query) = parent.slice().iter().position(|c| *c == b'?') {
        parent.resize(query);
    }
    let target = if parent.slice().last() == Some(&b'/') {
        pool.concat(parent.bytes(), path.bytes)
    } else {
        let mut target = pool.concat(parent.bytes(), span(b"/"));
        target.append(path.bytes);
        target
    };
    encode(target.bytes(), out, &*leaves);
}
fn trim(bytes: &[u8]) -> &[u8] {
    let white = |c: &u8| crate::book_io::xp_book_ascii_space(*c);
    let start = bytes.iter().position(|c| !white(c)).unwrap_or(bytes.len());
    let end = bytes
        .iter()
        .rposition(|c| !white(c))
        .map_or(start, |i| i + 1);
    &bytes[start..end]
}
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_http_header_parse_retire_c1_s2(
    line: Bytes,
    name: *mut MutableText,
    value: *mut MutableText,
    leaves: *const TextLeaves,
) {
    let bytes = line.slice();
    let colon = bytes.iter().position(|c| *c == b':');
    text::assign(
        &*leaves,
        &mut *name,
        span(trim(&bytes[..colon.unwrap_or(bytes.len())])),
    );
    text::assign(
        &*leaves,
        &mut *value,
        span(colon.map_or(&[], |i| trim(&bytes[i + 1..]))),
    );
}
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_http_header_format_retire_c1_s2(
    name: NativeText,
    value: NativeText,
    masked: bool,
    out: *mut MutableText,
    leaves: *const TextLeaves,
) {
    if name.bytes.length == 0 {
        text::initialize(&*leaves, out, 0);
        return;
    }
    if value.bytes.length == 0 {
        text::output_copy(&*leaves, out, name.bytes);
        return;
    }
    text::initialize(&*leaves, out, 0);
    ((*leaves).resize)(out, name.bytes.length + 2);
    ((*leaves).resize)(out, 0);
    text::output_append(&*leaves, out, name.bytes);
    text::output_append(&*leaves, out, span(b": "));
    let slots = text::slots::<1>();
    let pool = Pool::new(&*leaves, &slots);
    let copy = pool.copy(if masked { span(b"******") } else { value.bytes });
    text::output_append(&*leaves, out, copy.bytes());
}
