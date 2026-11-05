use core::slice;

#[repr(C)]
pub struct Scanner {
    pub captured: [u8; 24],
    pub name_len: usize,
    pub magic_matched: usize,
    pub capturing: bool,
    pub mismatch: bool,
}
impl Scanner {
    pub const fn new() -> Self {
        Self {
            captured: [0; 24],
            name_len: 0,
            magic_matched: 0,
            capturing: false,
            mismatch: false,
        }
    }
    pub fn feed(&mut self, data: &[u8], tag: &[u8]) {
        if self.mismatch || tag.len() < 21 {
            return;
        }
        for &c in data {
            if self.capturing {
                if c == b';' {
                    self.capturing = false;
                    self.captured[self.name_len] = 0;
                    if self.name_len != tag.len() - 21
                        || self.captured[..self.name_len] != tag[20..tag.len() - 1]
                    {
                        self.mismatch = true;
                        return;
                    }
                } else if self.name_len < 23 && c > 0x20 && c < 0x7f {
                    self.captured[self.name_len] = c;
                    self.name_len += 1;
                } else {
                    self.capturing = false;
                }
                continue;
            }
            if c == tag[self.magic_matched] {
                self.magic_matched += 1;
                if self.magic_matched == 20 {
                    self.magic_matched = 0;
                    self.capturing = true;
                    self.name_len = 0;
                }
            } else {
                self.magic_matched = usize::from(c == tag[0]);
            }
        }
    }
}

// Scanner and slices are exclusive/live for this synchronous call; TAG is borrowed.
#[no_mangle]
pub unsafe extern "C" fn xp_board_scan(
    state: *mut Scanner,
    data: *const u8,
    len: usize,
    tag: *const u8,
    tag_len: usize,
) {
    let data = if len == 0 {
        &[]
    } else {
        slice::from_raw_parts(data, len)
    };
    (*state).feed(data, slice::from_raw_parts(tag, tag_len));
}
