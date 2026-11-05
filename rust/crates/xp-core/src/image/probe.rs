//! Streaming image dimensions without retaining compressed input.
#[derive(Default)]
enum State {
    #[default]
    Sniff,
    Png,
    Gif,
    Soi,
    Ff,
    Marker,
    LenHi,
    LenLo,
    Skip,
    Sof,
    Done,
    Failed,
}

#[derive(Default)]
pub struct Probe {
    state: State,
    pos: u32,
    skip: u32,
    length: u16,
    sof: bool,
    buf: [u8; 5],
    fill: usize,
    width: u32,
    height: u32,
}

impl Probe {
    fn feed(&mut self, b: u8) -> bool {
        const PNG: &[u8; 8] = b"\x89PNG\r\n\x1a\n";
        match self.state {
            State::Sniff => {
                self.state = match b {
                    255 => State::Soi,
                    0x89 => State::Png,
                    b'G' => State::Gif,
                    _ => State::Failed,
                };
                self.pos = 1;
                !matches!(self.state, State::Failed)
            }
            State::Png => {
                if (self.pos < 8 && b != PNG[self.pos as usize])
                    || ((12..16).contains(&self.pos) && b != b"IHDR"[(self.pos - 12) as usize])
                {
                    self.state = State::Failed;
                    return false;
                }
                if (16..20).contains(&self.pos) {
                    self.width = (self.width << 8) | b as u32;
                }
                if (20..24).contains(&self.pos) {
                    self.height = (self.height << 8) | b as u32;
                }
                self.pos += 1;
                if self.pos == 24 {
                    self.state = State::Done;
                    return false;
                }
                true
            }
            State::Gif => {
                if (self.pos < 4 && b != b"GIF8"[self.pos as usize])
                    || (self.pos == 4 && b != b'7' && b != b'9')
                    || (self.pos == 5 && b != b'a')
                {
                    self.state = State::Failed;
                    return false;
                }
                if self.pos == 6 || self.pos == 7 {
                    self.width |= (b as u32) << ((self.pos - 6) * 8);
                }
                if self.pos == 8 || self.pos == 9 {
                    self.height |= (b as u32) << ((self.pos - 8) * 8);
                }
                self.pos += 1;
                if self.pos == 10 {
                    self.state = State::Done;
                    return false;
                }
                true
            }
            State::Soi => {
                self.state = if b == 0xd8 { State::Ff } else { State::Failed };
                b == 0xd8
            }
            State::Ff => {
                self.state = if b == 255 {
                    State::Marker
                } else {
                    State::Failed
                };
                b == 255
            }
            State::Marker => {
                if b == 255 {
                    return true;
                }
                if b == 1 || (0xd0..=0xd8).contains(&b) {
                    self.state = State::Ff;
                    return true;
                }
                if b == 0xd9 || b == 0xda {
                    self.state = State::Failed;
                    return false;
                }
                self.sof = (0xc0..=0xcf).contains(&b) && ![0xc4, 0xc8, 0xcc].contains(&b);
                self.state = State::LenHi;
                true
            }
            State::LenHi => {
                self.length = (b as u16) << 8;
                self.state = State::LenLo;
                true
            }
            State::LenLo => {
                self.length |= b as u16;
                if self.length < 2 || (self.sof && self.length < 7) {
                    self.state = State::Failed;
                    return false;
                }
                self.state = if self.sof {
                    self.fill = 0;
                    State::Sof
                } else if self.length == 2 {
                    State::Ff
                } else {
                    self.skip = self.length as u32 - 2;
                    State::Skip
                };
                true
            }
            State::Skip => {
                self.skip -= 1;
                if self.skip == 0 {
                    self.state = State::Ff;
                }
                true
            }
            State::Sof => {
                self.buf[self.fill] = b;
                self.fill += 1;
                if self.fill == 5 {
                    self.height = u16::from_be_bytes([self.buf[1], self.buf[2]]) as u32;
                    self.width = u16::from_be_bytes([self.buf[3], self.buf[4]]) as u32;
                    self.state = State::Done;
                    return false;
                }
                true
            }
            State::Done | State::Failed => false,
        }
    }
    pub fn write(&mut self, bytes: &[u8]) -> usize {
        for (i, &b) in bytes.iter().enumerate() {
            if !self.feed(b) {
                return i;
            }
        }
        bytes.len()
    }
    pub fn dimensions(&self) -> Option<(u32, u32)> {
        if matches!(self.state, State::Done)
            && self.width > 0
            && self.height > 0
            && self.width <= 32767
            && self.height <= 32767
            && self.width as u64 * self.height as u64 <= 8388608
        {
            Some((self.width, self.height))
        } else {
            None
        }
    }
}
