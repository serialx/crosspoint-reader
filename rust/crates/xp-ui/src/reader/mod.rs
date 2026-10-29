//! Reader lifecycle policy. Storage, clocks, locking and panel I/O remain borrowed leaves.
use core::ffi::c_void;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Session {
    pub anchor_ms: u32,
    pub anchor_epoch: i64,
    pub start_epoch: i64,
    pub duration: u32,
    pub anchor_progress: u16,
    pub start_progress: u16,
    pub end_progress: u16,
    pub has_anchor: u8,
    pub pending_forward: u8,
}
impl Session {
    pub fn note(&mut self, forward: bool, succeeded: bool) {
        self.pending_forward = u8::from(forward && succeeded);
    }
    pub fn render(&mut self, now: u32, epoch: i64, progress: i32) {
        let progress = progress.clamp(0, 10000) as u16;
        if self.pending_forward != 0 && self.has_anchor != 0 && self.anchor_epoch > 0 {
            let elapsed = now.wrapping_sub(self.anchor_ms) / 1000;
            if elapsed >= 2 {
                if self.duration == 0 {
                    self.start_epoch = self.anchor_epoch;
                    self.start_progress = self.anchor_progress;
                    self.end_progress = self.anchor_progress;
                }
                self.duration = self.duration.wrapping_add(elapsed.min(1800));
                self.end_progress = self.end_progress.max(progress);
            }
        }
        self.anchor_ms = now;
        self.anchor_epoch = epoch;
        self.anchor_progress = progress;
        self.has_anchor = 1;
        self.pending_forward = 0;
    }
    pub fn worthy(&self) -> bool {
        self.start_epoch > 0
            && self.duration >= 10
            && self.start_epoch.wrapping_add(i64::from(self.duration)) > self.start_epoch
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Progress {
    pub spine: u16,
    pub page: u16,
    pub count: u16,
    pub has_count: u8,
    pub has_offset: u8,
    pub offset: u32,
}

pub fn progress_encode(
    spine: i32,
    page: i32,
    count: i32,
    offset: Option<u32>,
    out: &mut [u8; 10],
) -> u32 {
    let (Ok(spine), Ok(page), Ok(count)) = (
        u16::try_from(spine),
        u16::try_from(page),
        u16::try_from(count),
    ) else {
        return 0;
    };
    out[..2].copy_from_slice(&spine.to_le_bytes());
    out[2..4].copy_from_slice(&page.to_le_bytes());
    out[4..6].copy_from_slice(&count.to_le_bytes());
    if let Some(offset) = offset {
        out[6..10].copy_from_slice(&offset.to_le_bytes());
        10
    } else {
        6
    }
}

pub fn progress_decode(data: &[u8]) -> Option<(Progress, bool)> {
    if !matches!(data.len(), 4 | 6 | 10) {
        return None;
    }
    let word = |at| u16::from_le_bytes([data[at], data[at + 1]]);
    let page = word(2);
    Some((
        Progress {
            spine: word(0),
            page: if page == u16::MAX { 0 } else { page },
            count: if data.len() >= 6 { word(4) } else { 0 },
            has_count: u8::from(data.len() >= 6),
            has_offset: u8::from(data.len() == 10),
            offset: if data.len() == 10 {
                u32::from_le_bytes([data[6], data[7], data[8], data[9]])
            } else {
                0
            },
        },
        page == u16::MAX,
    ))
}

pub fn remote_allowed(shown: bool, rendering: bool, input_over_page: bool) -> bool {
    shown && !rendering && !input_over_page
}
pub fn build_allowed(running: bool, stopping: bool, starting: bool) -> bool {
    !running && !stopping && !starting
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct BuildWait {
    pub context: *mut c_void,
    pub ready: Option<unsafe extern "C" fn(*mut c_void) -> u8>,
    pub now: Option<unsafe extern "C" fn(*mut c_void) -> u32>,
    pub sleep: Option<unsafe extern "C" fn(*mut c_void, u32)>,
}
/// # Safety
/// Callbacks and context must remain valid for this synchronous call and must not reenter it.
pub unsafe fn wait_build(services: BuildWait, wait_ms: u32, step_ms: u32) -> bool {
    let (Some(ready), Some(now), Some(sleep)) = (services.ready, services.now, services.sleep)
    else {
        return false;
    };
    if ready(services.context) != 0 {
        return true;
    }
    let started = now(services.context);
    loop {
        if now(services.context).wrapping_sub(started) >= wait_ms {
            return false;
        }
        sleep(services.context, step_ms);
        if ready(services.context) != 0 {
            return true;
        }
    }
}

pub fn refresh(remaining: &mut i32, frequency: i32) -> bool {
    let half = *remaining <= 1;
    *remaining = if half { frequency } else { *remaining - 1 };
    half
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct LinkNote {
    pub drawn_in_title: u8,
    pub drawn: u8,
}
impl LinkNote {
    pub fn draw(&mut self, shown: bool, note: u8) -> u8 {
        self.drawn_in_title = u8::from(shown);
        self.drawn = if shown { note } else { 0 };
        self.drawn
    }
    pub fn repaint(&mut self, note: u8) -> bool {
        if self.drawn_in_title == 0 || note == self.drawn {
            return false;
        }
        self.drawn_in_title = 0;
        true
    }
}

const _: () = {
    use core::mem::{align_of, offset_of, size_of};
    assert!(size_of::<Session>() == 40);
    assert!(align_of::<Session>() == 8);
    assert!(offset_of!(Session, anchor_ms) == 0);
    assert!(offset_of!(Session, anchor_epoch) == 8);
    assert!(offset_of!(Session, start_epoch) == 16);
    assert!(offset_of!(Session, duration) == 24);
    assert!(offset_of!(Session, anchor_progress) == 28);
    assert!(offset_of!(Session, start_progress) == 30);
    assert!(offset_of!(Session, end_progress) == 32);
    assert!(offset_of!(Session, has_anchor) == 34);
    assert!(offset_of!(Session, pending_forward) == 35);
    assert!(size_of::<Progress>() == 12);
    assert!(align_of::<Progress>() == 4);
    assert!(offset_of!(Progress, spine) == 0);
    assert!(offset_of!(Progress, page) == 2);
    assert!(offset_of!(Progress, count) == 4);
    assert!(offset_of!(Progress, has_count) == 6);
    assert!(offset_of!(Progress, has_offset) == 7);
    assert!(offset_of!(Progress, offset) == 8);
    assert!(size_of::<LinkNote>() == 2);
    assert!(align_of::<LinkNote>() == 1);
    assert!(offset_of!(LinkNote, drawn) == 1);
    assert!(size_of::<BuildWait>() == 4 * size_of::<usize>());
    assert!(align_of::<BuildWait>() == align_of::<usize>());
    assert!(offset_of!(BuildWait, context) == 0);
    assert!(offset_of!(BuildWait, ready) == size_of::<usize>());
    assert!(offset_of!(BuildWait, now) == 2 * size_of::<usize>());
    assert!(offset_of!(BuildWait, sleep) == 3 * size_of::<usize>());
};

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn progress_bytes_and_invalid_output() {
        let mut bytes = [0xa5; 10];
        assert_eq!(
            progress_encode(0x1234, 0x5678, 0x9abc, Some(0x89abcdef), &mut bytes),
            10
        );
        assert_eq!(
            bytes,
            [0x34, 0x12, 0x78, 0x56, 0xbc, 0x9a, 0xef, 0xcd, 0xab, 0x89]
        );
        let (decoded, stale) = progress_decode(&bytes).unwrap();
        assert!(!stale);
        assert_eq!(
            (decoded.spine, decoded.page, decoded.count, decoded.offset),
            (0x1234, 0x5678, 0x9abc, 0x89abcdef)
        );
        assert_eq!(progress_encode(-1, 0, 0, None, &mut bytes), 0);
        assert_eq!(
            bytes,
            [0x34, 0x12, 0x78, 0x56, 0xbc, 0x9a, 0xef, 0xcd, 0xab, 0x89]
        );
        for length in 0..=10 {
            assert_eq!(
                progress_decode(&bytes[..length]).is_some(),
                matches!(length, 4 | 6 | 10)
            );
        }
        bytes[2..4].copy_from_slice(&u16::MAX.to_le_bytes());
        let (decoded, stale) = progress_decode(&bytes[..4]).unwrap();
        assert!(stale);
        assert_eq!(
            (decoded.page, decoded.has_count, decoded.has_offset),
            (0, 0, 0)
        );
    }
    #[test]
    fn session_keeps_wrapping_clock_and_dwell_cap() {
        let mut state = Session::default();
        state.render(u32::MAX - 1000, 100, -1);
        state.note(true, true);
        state.render(999, 102, 10001);
        assert_eq!(
            (state.duration, state.start_progress, state.end_progress),
            (2, 0, 10000)
        );
        state.note(true, true);
        state.render(999 + 1801000, 1903, 100);
        assert_eq!(state.duration, 1802);
        assert!(state.worthy());
    }
    #[test]
    fn refresh_and_title_are_scalar() {
        let mut remaining = 0;
        assert!(refresh(&mut remaining, 3));
        assert_eq!(remaining, 3);
        assert!(!refresh(&mut remaining, 3));
        assert!(!refresh(&mut remaining, 3));
        assert!(refresh(&mut remaining, 3));
        let mut title = LinkNote::default();
        assert_eq!(title.draw(false, 1), 0);
        assert!(!title.repaint(2));
        assert_eq!(title.draw(true, 1), 1);
        assert!(title.repaint(2));
        assert!(!title.repaint(2));
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ProgressIo {
    pub context: *mut c_void,
    pub open_temp: Option<unsafe extern "C" fn(*mut c_void) -> u8>,
    pub write_temp: Option<unsafe extern "C" fn(*mut c_void, *const u8, u32) -> u32>,
    pub flush_temp: Option<unsafe extern "C" fn(*mut c_void)>,
    pub close_temp: Option<unsafe extern "C" fn(*mut c_void)>,
    pub remove_final: Option<unsafe extern "C" fn(*mut c_void)>,
    pub rename_temp: Option<unsafe extern "C" fn(*mut c_void) -> u8>,
}
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum ProgressStatus {
    Ok,
    OpenFailed,
    ShortWrite,
    RenameFailed,
    Invalid,
}
/// # Safety
/// Callback context and data remain valid for the synchronous call; callbacks
/// must not reenter this operation or retain the borrowed byte pointer.
pub unsafe fn write_progress(io: ProgressIo, data: *const u8, len: u32) -> (ProgressStatus, u32) {
    let (Some(open), Some(write), Some(flush), Some(close), Some(remove), Some(rename)) = (
        io.open_temp,
        io.write_temp,
        io.flush_temp,
        io.close_temp,
        io.remove_final,
        io.rename_temp,
    ) else {
        return (ProgressStatus::Invalid, 0);
    };
    if len != 0 && data.is_null() {
        return (ProgressStatus::Invalid, 0);
    }
    if open(io.context) == 0 {
        return (ProgressStatus::OpenFailed, 0);
    }
    let written = write(io.context, data, len);
    if written != len {
        return (ProgressStatus::ShortWrite, written);
    }
    flush(io.context);
    close(io.context);
    remove(io.context);
    (
        if rename(io.context) != 0 {
            ProgressStatus::Ok
        } else {
            ProgressStatus::RenameFailed
        },
        written,
    )
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct LinkPosition {
    pub spine: i32,
    pub page: i32,
}
pub fn links_encode(positions: &[LinkPosition; 3], depth: u8, out: &mut [u8; 13]) -> u32 {
    if depth > 3 {
        return 0;
    }
    out[0] = depth;
    for (i, pos) in positions.iter().take(usize::from(depth)).enumerate() {
        let at = 1 + 4 * i;
        out[at..at + 2].copy_from_slice(&(pos.spine as u16).to_le_bytes());
        out[at + 2..at + 4].copy_from_slice(&(pos.page as u16).to_le_bytes());
    }
    1 + 4 * u32::from(depth)
}
pub fn links_decode(bytes: &[u8], spine_count: i32, positions: &mut [LinkPosition; 3]) -> u8 {
    let Some(&depth) = bytes.first() else {
        return 0;
    };
    if !(1..=3).contains(&depth) || bytes.len() != 1 + 4 * usize::from(depth) {
        return 0;
    }
    let mut valid = true;
    for (record, pos) in bytes[1..]
        .as_chunks::<4>()
        .0
        .iter()
        .zip(positions.iter_mut())
    {
        pos.spine = i32::from(u16::from_le_bytes([record[0], record[1]]));
        pos.page = i32::from(u16::from_le_bytes([record[2], record[3]]));
        valid &= pos.spine < spine_count;
    }
    if valid {
        depth
    } else {
        0
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct PageMove {
    pub value: i32,
    pub kind: u8,
    pub last_page: u8,
    pub reserved: [u8; 2],
}
pub mod input;
#[path = "navigation.rs"]
mod navigation;
fn page_move(movement: navigation::Move) -> PageMove {
    use navigation::Move;
    let (value, kind, last_page) = match movement {
        Move::None => (0, 0, false),
        Move::Page(page) => (page, 1, false),
        Move::Chapter { spine, last_page } => (spine, 2, last_page),
        Move::End(spine) => (spine, 3, false),
    };
    PageMove {
        value,
        kind,
        last_page: u8::from(last_page),
        reserved: [0; 2],
    }
}
pub fn turn(
    spine: i32,
    spine_count: i32,
    page: i32,
    page_count: u16,
    building: bool,
    forward: bool,
) -> PageMove {
    page_move(navigation::turn(
        spine,
        spine_count,
        page,
        i32::from(page_count),
        building,
        forward,
    ))
}
pub fn skip(spine: i32, page: i32, amount: i32) -> PageMove {
    page_move(navigation::skip(spine, page, amount))
}
const _: () = {
    use core::mem::{align_of, offset_of, size_of};
    assert!(size_of::<PageMove>() == 8 && align_of::<PageMove>() == 4);
    assert!(offset_of!(PageMove, value) == 0 && offset_of!(PageMove, kind) == 4);
    assert!(offset_of!(PageMove, last_page) == 5 && offset_of!(PageMove, reserved) == 6);
    assert!(size_of::<LinkPosition>() == 8 && align_of::<LinkPosition>() == 4);
    assert!(offset_of!(LinkPosition, spine) == 0 && offset_of!(LinkPosition, page) == 4);
    assert!(size_of::<ProgressIo>() == 7 * size_of::<usize>());
    assert!(align_of::<ProgressIo>() == align_of::<usize>());
    assert!(offset_of!(ProgressIo, context) == 0);
    assert!(offset_of!(ProgressIo, open_temp) == size_of::<usize>());
    assert!(offset_of!(ProgressIo, write_temp) == 2 * size_of::<usize>());
    assert!(offset_of!(ProgressIo, flush_temp) == 3 * size_of::<usize>());
    assert!(offset_of!(ProgressIo, close_temp) == 4 * size_of::<usize>());
    assert!(offset_of!(ProgressIo, remove_final) == 5 * size_of::<usize>());
    assert!(offset_of!(ProgressIo, rename_temp) == 6 * size_of::<usize>());
};
