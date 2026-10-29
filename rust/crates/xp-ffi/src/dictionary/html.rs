use super::bytes;
use core::ffi::c_void;
struct Writer {
    context: *mut c_void,
    write: unsafe extern "C" fn(*mut c_void, *const u8, usize) -> usize,
    data: [u8; 128],
    used: usize,
}
impl Writer {
    fn flush(&mut self) -> bool {
        if self.used == 0 {
            return true;
        }
        if unsafe { (self.write)(self.context, self.data.as_ptr(), self.used) } != self.used {
            return false;
        }
        self.used = 0;
        true
    }
    fn append(&mut self, mut data: &[u8]) -> bool {
        while !data.is_empty() {
            let n = data.len().min(self.data.len() - self.used);
            self.data[self.used..self.used + n].copy_from_slice(&data[..n]);
            self.used += n;
            data = &data[n..];
            if self.used == self.data.len() && !self.flush() {
                return false;
            }
        }
        true
    }
}
fn void(name: &[u8]) -> bool {
    const ELEMENTS: &[&[u8]] = &[
        b"area", b"base", b"br", b"col", b"embed", b"hr", b"img", b"input", b"link", b"meta",
        b"param", b"source", b"track", b"wbr",
    ];
    ELEMENTS.contains(&name)
}
fn entity(html: &[u8], pos: usize) -> Option<usize> {
    let mut j = pos + 1;
    if html.get(j) == Some(&b'#') {
        j += 1;
        if matches!(html.get(j), Some(b'x' | b'X')) {
            j += 1;
        }
        let digits = j;
        while html.get(j).is_some_and(u8::is_ascii_hexdigit) {
            j += 1;
        }
        if j == digits {
            return None;
        }
    } else {
        let letters = j;
        while html.get(j).is_some_and(u8::is_ascii_alphanumeric) {
            j += 1;
        }
        if j == letters {
            return None;
        }
    }
    (html.get(j) == Some(&b';')).then_some(j)
}
fn normalize(
    html: &[u8],
    context: *mut c_void,
    write: unsafe extern "C" fn(*mut c_void, *const u8, usize) -> usize,
) -> bool {
    let mut out = Writer {
        context,
        write,
        data: [0; 128],
        used: 0,
    };
    if !out.append(b"<html><body>") {
        return false;
    }
    let mut i = 0;
    while i < html.len() {
        let c = html[i];
        if c == b'<' && matches!(html.get(i + 1), Some(b'!' | b'?')) {
            let comment = html[i..].starts_with(b"<!--");
            let start = if comment { i + 4 } else { i };
            let needle: &[u8] = if comment { b"-->" } else { b">" };
            i = html[start..]
                .windows(needle.len())
                .position(|w| w == needle)
                .map_or(html.len(), |j| start + j + needle.len());
            continue;
        }
        if c == b'<'
            && html
                .get(i + 1)
                .is_some_and(|b| *b == b'/' || b.is_ascii_alphabetic())
        {
            let mut j = i + 1;
            let mut quote = 0;
            while j < html.len() {
                let d = html[j];
                if quote != 0 {
                    if d == quote {
                        quote = 0;
                    }
                } else if d == b'"' || d == b'\'' {
                    quote = d;
                } else if d == b'>' {
                    break;
                }
                j += 1;
            }
            if j == html.len() {
                if !out.append(b"&lt;") {
                    return false;
                }
                i += 1;
                continue;
            }
            let closing = html[i + 1] == b'/';
            let mut end = i + if closing { 2 } else { 1 };
            let mut name = [0; 16];
            let mut len = 0;
            while end < j && html[end].is_ascii_alphanumeric() {
                if len < 15 {
                    name[len] = html[end].to_ascii_lowercase();
                    len += 1;
                }
                end += 1;
            }
            let is_void = void(&name[..len]);
            if closing && is_void {
                i = j + 1;
                continue;
            }
            if !out.append(b"<")
                || (closing && !out.append(b"/"))
                || !out.append(&name[..len])
                || !out.append(&html[end..j])
                || (!closing && is_void && html[j - 1] != b'/' && !out.append(b"/"))
                || !out.append(b">")
            {
                return false;
            }
            i = j + 1;
            continue;
        }
        if c == b'<' {
            if !out.append(b"&lt;") {
                return false;
            }
            i += 1;
            continue;
        }
        if c == b'&' {
            if let Some(end) = entity(html, i) {
                if !out.append(&html[i..=end]) {
                    return false;
                }
                i = end + 1;
            } else {
                if !out.append(b"&amp;") {
                    return false;
                }
                i += 1;
            }
            continue;
        }
        if !out.append(&html[i..i + 1]) {
            return false;
        }
        i += 1;
    }
    out.append(b"</body></html>") && out.flush()
}
/// # Safety
/// Input and the file descriptor remain valid for the call; callbacks do not retain bytes.
#[no_mangle]
pub unsafe extern "C" fn xp_dictionary_html(
    data: *const u8,
    len: usize,
    context: *mut c_void,
    write: unsafe extern "C" fn(*mut c_void, *const u8, usize) -> usize,
) -> bool {
    normalize(bytes(data, len), context, write)
}
#[no_mangle]
pub extern "C" fn xp_dictionary_html_limit(
    pages: usize,
    elements: usize,
    added: usize,
    free: usize,
    largest: usize,
) -> u8 {
    if pages >= 64 {
        1
    } else if added > 512usize.saturating_sub(elements) {
        2
    } else if free < 40960 || largest < 32768 {
        3
    } else {
        0
    }
}
#[no_mangle]
pub extern "C" fn xp_dictionary_html_ready(free: usize, largest: usize) -> bool {
    free >= 65536 && largest >= 40960
}
