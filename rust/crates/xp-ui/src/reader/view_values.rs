//! Reader status/title and toolbar row value selection over native resources.
use core::ffi::c_void;
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Operation {
    Status = 0,
    Chapter = 1,
    TextName = 2,
    TextValue = 3,
    MoreName = 4,
    MoreValue = 5,
}

#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Query {
    HasSection = 0,
    Page = 1,
    EstimatedCount = 2,
    HasBook = 3,
    Progress = 4,
    TitleMode = 5,
    Automatic = 6,
    TurnDuration = 7,
    StatusHeight = 8,
    ProgressHeight = 9,
    StatusMargin = 10,
    TocIndex = 11,
    Bookmarked = 12,
    Building = 13,
    HasSdFont = 14,
    PointSize = 15,
    Spacing = 16,
    Alignment = 17,
    SpacingLabel = 18,
    AlignmentLabel = 19,
    MoreCount = 20,
    MoreAction = 21,
    MoreLabel = 22,
    Orientation = 23,
    OrientationLabel = 24,
    AutoOption = 25,
    TextName = 26,
}

#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Command {
    Label = 0,
    Unnamed = 1,
    Chapter = 2,
    SampleSpec = 3,
    TakeLinkNote = 4,
    Connected = 5,
    LowMemory = 6,
    Connecting = 7,
    Failed = 8,
    Automatic = 9,
    Book = 10,
    SdFont = 11,
    NotoSans = 12,
    PointSize = 13,
    Off = 14,
    Number = 15,
    ChapterCopy = 16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Status {
    pub book_progress: f32,
    pub page_count: f32,
    pub page: i32,
    pub y_offset: i32,
    pub bookmarked: u8,
    pub building: u8,
    pub reserved: [u8; 2],
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub context: *mut c_void,
    pub query: Option<unsafe extern "C" fn(*mut c_void, u8, u32) -> u32>,
    pub command: Option<unsafe extern "C" fn(*mut c_void, u8, u32) -> u32>,
    pub paint: Option<unsafe extern "C" fn(*mut c_void, *const Status)>,
}
struct Engine {
    context: *mut c_void,
    query: unsafe extern "C" fn(*mut c_void, u8, u32) -> u32,
    command: unsafe extern "C" fn(*mut c_void, u8, u32) -> u32,
    paint: unsafe extern "C" fn(*mut c_void, *const Status),
}
impl Engine {
    fn q(&self, q: Query) -> u32 {
        self.qa(q, 0)
    }
    fn qa(&self, q: Query, a: u32) -> u32 {
        unsafe { (self.query)(self.context, q as u8, a) }
    }
    fn c(&self, c: Command, a: u32) -> u32 {
        unsafe { (self.command)(self.context, c as u8, a) }
    }
    fn label(&self, q: Query, index: u32) {
        self.c(Command::Label, self.qa(q, index));
    }
    fn chapter(&self, empty_without_book: bool) {
        if self.q(Query::HasBook) == 0 {
            if !empty_without_book {
                self.c(Command::Unnamed, 0);
            }
            return;
        }
        // Status rendering sets its fallback before fetching the native title.
        if !empty_without_book {
            self.c(Command::Unnamed, 0);
        }
        let toc = self.q(Query::TocIndex) as i32;
        if toc != -1 {
            self.c(
                if empty_without_book {
                    Command::Chapter
                } else {
                    Command::ChapterCopy
                },
                toc as u32,
            );
        } else if empty_without_book {
            self.c(Command::Unnamed, 0);
        }
    }
    fn status(&self) {
        let section = self.q(Query::HasSection) != 0;
        let page = if section {
            (self.q(Query::Page) as i32).wrapping_add(1)
        } else {
            1
        };
        let count = if section {
            self.q(Query::EstimatedCount) as f32
        } else {
            1.0
        };
        let fraction = if count > 0.0 {
            page as f32 / count
        } else {
            0.0
        };
        let progress = if self.q(Query::HasBook) != 0 {
            f32::from_bits(self.qa(Query::Progress, fraction.to_bits())) * 100.0
        } else {
            0.0
        };
        self.c(Command::SampleSpec, 0);
        let mode = self.q(Query::TitleMode);
        let note = self.c(Command::TakeLinkNote, u32::from(mode != 2));
        let mut y = 0;
        match note {
            3 => {
                self.c(Command::Connected, 0);
            }
            4 => {
                self.c(Command::LowMemory, 0);
            }
            1 => {
                self.c(Command::Connecting, 0);
            }
            0 => {
                if self.q(Query::Automatic) != 0 {
                    let duration = self.q(Query::TurnDuration);
                    // Active auto-turn always has a nonzero configured period.
                    if duration == 0 {
                        return;
                    }
                    self.c(Command::Automatic, 60_000 / duration);
                    let height = self.q(Query::StatusHeight);
                    if height == 0 || height == self.q(Query::ProgressHeight) {
                        y = self.q(Query::StatusMargin) as i32;
                    }
                } else if mode == 1 {
                    self.chapter(false);
                } else if mode == 0 && self.q(Query::HasBook) != 0 {
                    self.c(Command::Book, 0);
                }
            }
            _ => {
                self.c(Command::Failed, 0);
            }
        }
        let view = Status {
            book_progress: progress,
            page_count: count,
            page,
            y_offset: y,
            bookmarked: (self.q(Query::Bookmarked) != 0).into(),
            building: (self.q(Query::HasSection) != 0 && self.q(Query::Building) != 0).into(),
            reserved: [0; 2],
        };
        unsafe {
            (self.paint)(self.context, &view);
        }
    }
    fn text_value(&self, row: i32) {
        match row {
            0 => {
                if self.q(Query::HasSdFont) != 0 {
                    self.c(Command::SdFont, 0);
                } else {
                    self.c(Command::NotoSans, 0);
                }
            }
            1 => {
                self.c(Command::PointSize, self.q(Query::PointSize));
            }
            2 => self.label(Query::SpacingLabel, self.q(Query::Spacing) % 4),
            3 => self.label(Query::AlignmentLabel, self.q(Query::Alignment) % 5),
            _ => {}
        }
    }
    fn more_value(&self, row: i32) {
        if row < 0 || row as u32 >= self.q(Query::MoreCount) {
            return;
        }
        match self.qa(Query::MoreAction, row as u32) {
            8 => self.label(Query::OrientationLabel, self.q(Query::Orientation) % 4),
            7 => {
                let index = self.q(Query::AutoOption);
                if index == 0 || index >= 5 {
                    self.c(Command::Off, 0);
                } else {
                    self.c(Command::Number, [1, 1, 3, 6, 12][index as usize]);
                }
            }
            _ => {}
        }
    }
}
/// # Safety
/// The services and native result string remain live for this synchronous call.
/// Paint borrows Status only until it returns. No callback may retain it.
pub unsafe fn run(s: *const Services, operation: u8, row: i32) {
    if operation > Operation::MoreValue as u8 {
        return;
    }
    let Some(s) = s.as_ref() else {
        return;
    };
    let (Some(query), Some(command), Some(paint)) = (s.query, s.command, s.paint) else {
        return;
    };
    let e = Engine {
        context: s.context,
        query,
        command,
        paint,
    };
    match operation {
        0 => e.status(),
        1 => e.chapter(true),
        2 => {
            if (0..5).contains(&row) {
                e.label(Query::TextName, row as u32);
            }
        }
        3 => e.text_value(row),
        4 => {
            if row >= 0 && (row as u32) < e.q(Query::MoreCount) {
                e.label(Query::MoreLabel, row as u32);
            }
        }
        5 => e.more_value(row),
        _ => {}
    }
}
pub fn toolbar(style: u8) -> u8 {
    (style == 1).into()
}

const _: () = {
    use core::mem::{align_of, offset_of, size_of};
    assert!(size_of::<Services>() == 4 * size_of::<usize>());
    assert!(align_of::<Services>() == align_of::<usize>());
    assert!(size_of::<Status>() == 20);
    assert!(align_of::<Status>() == 4);
    assert!(offset_of!(Services, context) == 0);
    assert!(offset_of!(Services, query) == size_of::<usize>());
    assert!(offset_of!(Services, command) == 2 * size_of::<usize>());
    assert!(offset_of!(Services, paint) == 3 * size_of::<usize>());
    assert!(offset_of!(Status, book_progress) == 0);
    assert!(offset_of!(Status, page_count) == 4);
    assert!(offset_of!(Status, page) == 8);
    assert!(offset_of!(Status, y_offset) == 12);
    assert!(offset_of!(Status, bookmarked) == 16);
    assert!(offset_of!(Status, building) == 17);
    assert!(offset_of!(Status, reserved) == 18);
    assert!(Operation::Status as u8 == 0);
    assert!(Operation::Chapter as u8 == 1);
    assert!(Operation::TextName as u8 == 2);
    assert!(Operation::TextValue as u8 == 3);
    assert!(Operation::MoreName as u8 == 4);
    assert!(Operation::MoreValue as u8 == 5);
    assert!(Query::HasSection as u8 == 0);
    assert!(Query::Page as u8 == 1);
    assert!(Query::EstimatedCount as u8 == 2);
    assert!(Query::HasBook as u8 == 3);
    assert!(Query::Progress as u8 == 4);
    assert!(Query::TitleMode as u8 == 5);
    assert!(Query::Automatic as u8 == 6);
    assert!(Query::TurnDuration as u8 == 7);
    assert!(Query::StatusHeight as u8 == 8);
    assert!(Query::ProgressHeight as u8 == 9);
    assert!(Query::StatusMargin as u8 == 10);
    assert!(Query::TocIndex as u8 == 11);
    assert!(Query::Bookmarked as u8 == 12);
    assert!(Query::Building as u8 == 13);
    assert!(Query::HasSdFont as u8 == 14);
    assert!(Query::PointSize as u8 == 15);
    assert!(Query::Spacing as u8 == 16);
    assert!(Query::Alignment as u8 == 17);
    assert!(Query::SpacingLabel as u8 == 18);
    assert!(Query::AlignmentLabel as u8 == 19);
    assert!(Query::MoreCount as u8 == 20);
    assert!(Query::MoreAction as u8 == 21);
    assert!(Query::MoreLabel as u8 == 22);
    assert!(Query::Orientation as u8 == 23);
    assert!(Query::OrientationLabel as u8 == 24);
    assert!(Query::AutoOption as u8 == 25);
    assert!(Query::TextName as u8 == 26);
    assert!(Command::Label as u8 == 0);
    assert!(Command::Unnamed as u8 == 1);
    assert!(Command::Chapter as u8 == 2);
    assert!(Command::SampleSpec as u8 == 3);
    assert!(Command::TakeLinkNote as u8 == 4);
    assert!(Command::Connected as u8 == 5);
    assert!(Command::LowMemory as u8 == 6);
    assert!(Command::Connecting as u8 == 7);
    assert!(Command::Failed as u8 == 8);
    assert!(Command::Automatic as u8 == 9);
    assert!(Command::Book as u8 == 10);
    assert!(Command::SdFont as u8 == 11);
    assert!(Command::NotoSans as u8 == 12);
    assert!(Command::PointSize as u8 == 13);
    assert!(Command::Off as u8 == 14);
    assert!(Command::Number as u8 == 15);
    assert!(Command::ChapterCopy as u8 == 16);
};

#[cfg(test)]
mod tests {
    use super::*;
    unsafe extern "C" fn query(_: *mut c_void, _: u8, _: u32) -> u32 {
        panic!("invalid services must not be called");
    }
    unsafe extern "C" fn command(_: *mut c_void, _: u8, _: u32) -> u32 {
        panic!("invalid services must not be called");
    }
    unsafe extern "C" fn paint(_: *mut c_void, _: *const Status) {
        panic!("invalid services must not be called");
    }
    unsafe extern "C" fn zero_period_query(_: *mut c_void, query: u8, _: u32) -> u32 {
        u32::from(query == Query::Automatic as u8)
    }
    unsafe extern "C" fn zero_period_command(
        context: *mut c_void,
        command: u8,
        argument: u32,
    ) -> u32 {
        let calls = &mut *context.cast::<u32>();
        if command == Command::SampleSpec as u8 {
            *calls |= 1;
        } else {
            assert_eq!(command, Command::TakeLinkNote as u8);
            assert_eq!(argument, 1);
            *calls |= 2;
        }
        0
    }
    #[test]
    fn active_automatic_zero_period_returns_without_painting() {
        let mut calls = 0u32;
        let services = Services {
            context: (&mut calls as *mut u32).cast(),
            query: Some(zero_period_query),
            command: Some(zero_period_command),
            paint: Some(paint),
        };
        unsafe {
            run(&services, Operation::Status as u8, 0);
        }
        assert_eq!(calls, 3);
    }
    #[test]
    fn invalid_services_and_operations_are_noops() {
        unsafe {
            run(core::ptr::null(), 0, 0);
        }
        let complete = Services {
            context: core::ptr::null_mut(),
            query: Some(query),
            command: Some(command),
            paint: Some(paint),
        };
        for operation in [6, 255] {
            unsafe {
                run(&complete, operation, 0);
            }
        }
        for missing in 0..3 {
            let mut services = complete;
            match missing {
                0 => services.query = None,
                1 => services.command = None,
                _ => services.paint = None,
            }
            unsafe {
                run(&services, 0, 0);
            }
        }
        for style in 0..=255 {
            assert_eq!(toolbar(style), u8::from(style == 1));
        }
    }
}
