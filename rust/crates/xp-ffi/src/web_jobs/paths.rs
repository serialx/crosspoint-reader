use core::slice;
fn alias(s: &[u8]) -> bool {
    let Some(tilde) = s.iter().position(|&b| b == b'~') else {
        return false;
    };
    if !s.get(tilde + 1).is_some_and(u8::is_ascii_digit) {
        return false;
    }
    let dot = s.iter().position(|&b| b == b'.');
    dot.unwrap_or(s.len()) <= 8 && dot.map_or(0, |p| s.len() - p - 1) <= 3
}
fn sensitive(path: &[u8], scratch: &mut [u8]) -> bool {
    let mut written = 0;
    let mut depth = 0usize;
    for segment in path.split(|&b| b == b'/') {
        if segment == b".." {
            written = scratch[..written]
                .iter()
                .rposition(|&b| b == b'/')
                .unwrap_or(0);
            depth = depth.saturating_sub(1);
        } else {
            let mut first = 0;
            let mut last = segment.len();
            while first < last && segment[first] == b' ' {
                first += 1;
            }
            while last > first && matches!(segment[last - 1], b'.' | b' ') {
                last -= 1;
            }
            let s = &segment[first..last];
            if depth < 2 && alias(s) {
                return true;
            }
            if !s.is_empty() {
                scratch[written] = b'/';
                written += 1;
                scratch[written..written + s.len()].copy_from_slice(s);
                written += s.len();
                depth += 1;
            }
        }
    }
    let canonical = &scratch[..written];
    [
        b"/.crosspoint/wifi.json".as_slice(),
        b"/.crosspoint/opds.json",
        b"/.crosspoint/koreader.json",
    ]
    .iter()
    .any(|prefix| {
        canonical
            .get(..prefix.len())
            .is_some_and(|s| s.eq_ignore_ascii_case(prefix))
    })
}
// scratch has path.len()+1 writable bytes, disjoint from path. Neither is retained.
#[no_mangle]
pub unsafe extern "C" fn xp_sensitive_path(path: *const u8, len: usize, scratch: *mut u8) -> bool {
    sensitive(
        slice::from_raw_parts(path, len),
        slice::from_raw_parts_mut(scratch, len + 1),
    )
}
#[no_mangle]
pub unsafe extern "C" fn xp_short_alias(path: *const u8, len: usize) -> bool {
    alias(slice::from_raw_parts(path, len))
}

#[no_mangle]
pub unsafe extern "C" fn xp_plugin_path_candidate(path: *const u8, len: usize) -> bool {
    let path = slice::from_raw_parts(path, len);
    path.len() > 1 && path[0] == b'/' && !path.windows(2).any(|pair| pair == b"..")
}
