//! Reader clipping hit testing, traversal, geometry and highlight painting.
use core::ffi::c_void;
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Operation {
    Hit = 0,
    Draw = 1,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Query {
    HasSection = 0,
    HasClippings = 1,
    Page = 2,
    PageCount = 3,
    Spine = 4,
    ElementCount = 5,
    WordCount = 6,
    Visible = 7,
    WordX = 8,
    LineX = 9,
    LineY = 10,
    Top = 11,
    Left = 12,
    ScreenMargin = 13,
    ClipCount = 14,
    A4 = 15,
    Bw = 16,
    EmSpace = 17,
    SourceStart = 18,
    SourceEnd = 19,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Command {
    Margins = 0,
    ReaderFont = 1,
    Signature = 2,
    ElementBegin = 3,
    ElementEnd = 4,
    BlockBegin = 5,
    BlockEnd = 6,
    WordFont = 7,
    Measure = 8,
    EmWidth = 9,
    LineFont = 10,
    Ruby = 11,
    YOffset = 12,
    LineHeight = 13,
    Pixel = 14,
    ClockStart = 15,
    Log = 16,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Rect {
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Input {
    pub x: i32,
    pub y: i32,
    pub font: i32,
    pub top: i32,
    pub left: i32,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Record {
    pub spine: u16,
    pub start_page: u16,
    pub end_page: u16,
    pub page_count: u16,
    pub start_word: u16,
    pub end_word: u16,
    pub signature: u32,
    pub start_offset: u32,
    pub end_offset: u32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub context: *mut c_void,
    pub query: Option<unsafe extern "C" fn(*mut c_void, u8, u32) -> u32>,
    pub command: Option<unsafe extern "C" fn(*mut c_void, u8, i32, i32) -> u32>,
    pub paint: Option<unsafe extern "C" fn(*mut c_void, *const Rect)>,
    pub clipping: Option<unsafe extern "C" fn(*mut c_void, u32, *mut Record)>,
}
struct Policy {
    context: *mut c_void,
    query: unsafe extern "C" fn(*mut c_void, u8, u32) -> u32,
    command: unsafe extern "C" fn(*mut c_void, u8, i32, i32) -> u32,
    paint: unsafe extern "C" fn(*mut c_void, *const Rect),
    clipping: unsafe extern "C" fn(*mut c_void, u32, *mut Record),
}
struct ViewGuard<'a>(&'a Policy, Command);
impl Drop for ViewGuard<'_> {
    fn drop(&mut self) {
        self.0.op(self.1);
    }
}
impl Policy {
    fn q(&self, q: Query, i: u32) -> u32 {
        unsafe { (self.query)(self.context, q as u8, i) }
    }
    fn f(&self, q: Query) -> u32 {
        self.q(q, 0)
    }
    fn c(&self, c: Command, a: i32, b: i32) -> u32 {
        unsafe { (self.command)(self.context, c as u8, a, b) }
    }
    fn op(&self, c: Command) -> u32 {
        self.c(c, 0, 0)
    }
    fn record(&self, index: u32) -> Record {
        let mut r = Record::default();
        unsafe { (self.clipping)(self.context, index, &mut r) };
        r
    }
    fn find(&self, word: u16, start: u32, end: u32, page: u16, signature: u32) -> Option<u32> {
        let mut i = 0;
        while i < self.f(Query::ClipCount) {
            let r = self.record(i);
            let spine = self.f(Query::Spine) as u16;
            let count = self.f(Query::PageCount) as u16;
            let matched = if r.spine != spine {
                false
            } else if r.start_offset != u32::MAX && r.end_offset != u32::MAX {
                start != u32::MAX && end > r.start_offset && start < r.end_offset
            } else {
                r.page_count == count
                    && r.signature != 0
                    && signature != 0
                    && r.signature == signature
                    && page >= r.start_page
                    && page <= r.end_page
                    && (page != r.start_page || word >= r.start_word)
                    && (page != r.end_page || word <= r.end_word)
            };
            if matched {
                return Some(i);
            }
            i += 1;
        }
        None
    }
    fn rect(&self, i: u16, base: i32, top: i32, left: i32, word_width: i32) -> Rect {
        let font = self.c(Command::WordFont, i32::from(i), base) as i32;
        let skip = if self.q(Query::EmSpace, u32::from(i)) != 0 {
            self.c(Command::EmWidth, i32::from(i), font) as i32
        } else {
            0
        };
        let mut width = word_width.wrapping_sub(skip);
        if u32::from(i) + 1 < self.f(Query::WordCount) {
            let next = self.q(Query::WordX, u32::from(i) + 1) as i32;
            let x = self.q(Query::WordX, u32::from(i)) as i32;
            if next > x {
                width = width.min(next.wrapping_sub(x).wrapping_sub(skip));
            }
        }
        let line_font = self.c(Command::LineFont, base, 0) as i32;
        let x = left
            .wrapping_add(self.f(Query::LineX) as i32)
            .wrapping_add(self.q(Query::WordX, u32::from(i)) as i32)
            .wrapping_add(skip);
        let y = top
            .wrapping_add(self.f(Query::LineY) as i32)
            .wrapping_add(self.c(Command::Ruby, line_font, 0) as i32)
            .wrapping_add(self.c(Command::YOffset, i32::from(i), base) as i32);
        let height = self.c(Command::LineHeight, font, 0) as i32;
        Rect {
            x,
            y,
            width,
            height,
        }
    }
    fn fill(&self, rect: Rect) {
        if self.f(Query::A4) != 0 && self.f(Query::Bw) == 0 {
            let mut y = rect.y.wrapping_add(rect.y & 1);
            while y < rect.y.wrapping_add(rect.height) {
                let mut x = rect.x.wrapping_add(rect.x & 1);
                while x < rect.x.wrapping_add(rect.width) {
                    self.c(Command::Pixel, x, y);
                    x = x.wrapping_add(2);
                }
                y = y.wrapping_add(2);
            }
        } else {
            unsafe { (self.paint)(self.context, &rect) }
        }
    }
    fn hit(&self, input: &Input) -> i32 {
        self.op(Command::Margins);
        let font = self.op(Command::ReaderFont) as i32;
        let signature = self.op(Command::Signature);
        let mut word = 0u16;
        let mut element = 0u16;
        while u32::from(element) < self.f(Query::ElementCount) {
            let line = self.c(Command::ElementBegin, i32::from(element), 0) != 0;
            let _element = ViewGuard(self, Command::ElementEnd);
            element = element.wrapping_add(1);
            if !line {
                continue;
            }
            let valid = self.op(Command::BlockBegin) != 0;
            let _block = ViewGuard(self, Command::BlockEnd);
            if !valid {
                continue;
            }
            let mut i = 0u16;
            while u32::from(i) < self.f(Query::WordCount) {
                let current = i;
                i = i.wrapping_add(1);
                if self.q(Query::Visible, u32::from(current)) == 0 {
                    continue;
                }
                let word_font = self.c(Command::WordFont, i32::from(current), font) as i32;
                let width = self.c(Command::Measure, i32::from(current), word_font) as i32;
                if width <= 0 {
                    continue;
                }
                let index = word;
                word = word.wrapping_add(1);
                let top =
                    (self.f(Query::Top) as i32).wrapping_add(self.f(Query::ScreenMargin) as i32);
                let left =
                    (self.f(Query::Left) as i32).wrapping_add(self.f(Query::ScreenMargin) as i32);
                let r = self.rect(current, font, top, left, width);
                if input.x < r.x
                    || input.x >= r.x.wrapping_add(r.width)
                    || input.y < r.y
                    || input.y >= r.y.wrapping_add(r.height)
                {
                    continue;
                }
                let start = self.q(Query::SourceStart, u32::from(current));
                let end = self.q(Query::SourceEnd, u32::from(current));
                if let Some(index) =
                    self.find(index, start, end, self.f(Query::Page) as u16, signature)
                {
                    return index as i32;
                }
            }
        }
        -1
    }
    fn draw(&self, input: &Input) {
        if self.f(Query::HasSection) == 0
            || self.f(Query::HasClippings) == 0
            || (self.f(Query::Page) as i32) < 0
            || (self.f(Query::Page) as i32) >= (self.f(Query::PageCount) as i32)
        {
            return;
        }
        let mut chapter = 0u32;
        let mut clip = 0;
        while clip < self.f(Query::ClipCount) {
            if u32::from(self.record(clip).spine) == self.f(Query::Spine) {
                chapter += 1;
            }
            clip += 1;
        }
        if chapter == 0 {
            return;
        }
        self.op(Command::ClockStart);
        let page = self.f(Query::Page) as u16;
        let signature = self.op(Command::Signature);
        let mut highlighted = 0u32;
        let mut word = 0u16;
        let mut element = 0u16;
        while u32::from(element) < self.f(Query::ElementCount) {
            let line = self.c(Command::ElementBegin, i32::from(element), 0) != 0;
            let _element = ViewGuard(self, Command::ElementEnd);
            element = element.wrapping_add(1);
            if !line {
                continue;
            }
            let valid = self.op(Command::BlockBegin) != 0;
            let _block = ViewGuard(self, Command::BlockEnd);
            if !valid {
                continue;
            }
            let mut previous: Option<(i32, i32)> = None;
            let mut i = 0u16;
            while u32::from(i) < self.f(Query::WordCount) {
                let current = i;
                i = i.wrapping_add(1);
                if self.q(Query::Visible, u32::from(current)) == 0 {
                    continue;
                }
                let font = self.c(Command::WordFont, i32::from(current), input.font) as i32;
                let width = self.c(Command::Measure, i32::from(current), font) as i32;
                if width <= 0 {
                    continue;
                }
                let index = word;
                word = word.wrapping_add(1);
                let start = self.q(Query::SourceStart, u32::from(current));
                let end = self.q(Query::SourceEnd, u32::from(current));
                if self.find(index, start, end, page, signature).is_none() {
                    previous = None;
                    continue;
                }
                let r = self.rect(current, input.font, input.top, input.left, width);
                if r.width > 0 {
                    let right = r.x.wrapping_add(r.width);
                    if let Some((x, width)) = previous {
                        let previous_right = x.wrapping_add(width);
                        let bridge = if previous_right < r.x {
                            Some((previous_right, r.x.wrapping_sub(previous_right)))
                        } else if right < x {
                            Some((right, x.wrapping_sub(right)))
                        } else {
                            None
                        };
                        if let Some((x, width)) = bridge {
                            let height = self.c(Command::LineHeight, input.font, 0) as i32;
                            self.fill(Rect {
                                x,
                                y: r.y,
                                width,
                                height,
                            });
                        }
                    }
                    let height = self.c(Command::LineHeight, input.font, 0) as i32;
                    self.fill(Rect { height, ..r });
                    previous = Some((r.x, r.width));
                    highlighted += 1;
                } else {
                    previous = None;
                }
            }
        }
        self.c(Command::Log, chapter as i32, highlighted as i32);
    }
}
/// # Safety
/// Services/input remain live for this call and obey the borrowed-view contract.
pub unsafe fn run(s: *const Services, operation: u8, input: *const Input) -> i32 {
    let (Some(s), Some(input)) = (s.as_ref(), input.as_ref()) else {
        return -1;
    };
    let (Some(query), Some(command), Some(paint), Some(clipping)) =
        (s.query, s.command, s.paint, s.clipping)
    else {
        return -1;
    };
    let p = Policy {
        context: s.context,
        query,
        command,
        paint,
        clipping,
    };
    match operation {
        x if x == Operation::Hit as u8 => p.hit(input),
        x if x == Operation::Draw as u8 => {
            p.draw(input);
            -1
        }
        _ => -1,
    }
}
const _: () = {
    assert!(Operation::Hit as u8 == 0);
    assert!(Operation::Draw as u8 == 1);
    assert!(Query::HasSection as u8 == 0);
    assert!(Query::HasClippings as u8 == 1);
    assert!(Query::Page as u8 == 2);
    assert!(Query::PageCount as u8 == 3);
    assert!(Query::Spine as u8 == 4);
    assert!(Query::ElementCount as u8 == 5);
    assert!(Query::WordCount as u8 == 6);
    assert!(Query::Visible as u8 == 7);
    assert!(Query::WordX as u8 == 8);
    assert!(Query::LineX as u8 == 9);
    assert!(Query::LineY as u8 == 10);
    assert!(Query::Top as u8 == 11);
    assert!(Query::Left as u8 == 12);
    assert!(Query::ScreenMargin as u8 == 13);
    assert!(Query::ClipCount as u8 == 14);
    assert!(Query::A4 as u8 == 15);
    assert!(Query::Bw as u8 == 16);
    assert!(Query::EmSpace as u8 == 17);
    assert!(Query::SourceStart as u8 == 18);
    assert!(Query::SourceEnd as u8 == 19);
    assert!(Command::Margins as u8 == 0);
    assert!(Command::ReaderFont as u8 == 1);
    assert!(Command::Signature as u8 == 2);
    assert!(Command::ElementBegin as u8 == 3);
    assert!(Command::ElementEnd as u8 == 4);
    assert!(Command::BlockBegin as u8 == 5);
    assert!(Command::BlockEnd as u8 == 6);
    assert!(Command::WordFont as u8 == 7);
    assert!(Command::Measure as u8 == 8);
    assert!(Command::EmWidth as u8 == 9);
    assert!(Command::LineFont as u8 == 10);
    assert!(Command::Ruby as u8 == 11);
    assert!(Command::YOffset as u8 == 12);
    assert!(Command::LineHeight as u8 == 13);
    assert!(Command::Pixel as u8 == 14);
    assert!(Command::ClockStart as u8 == 15);
    assert!(Command::Log as u8 == 16);
    assert!(core::mem::size_of::<Services>() == 5 * core::mem::size_of::<usize>());
    assert!(core::mem::align_of::<Services>() == core::mem::align_of::<usize>());
    assert!(core::mem::offset_of!(Services, context) == 0);
    assert!(core::mem::offset_of!(Services, query) == core::mem::size_of::<usize>());
    assert!(core::mem::offset_of!(Services, command) == 2 * core::mem::size_of::<usize>());
    assert!(core::mem::offset_of!(Services, paint) == 3 * core::mem::size_of::<usize>());
    assert!(core::mem::offset_of!(Services, clipping) == 4 * core::mem::size_of::<usize>());
    assert!(core::mem::size_of::<Rect>() == 16);
    assert!(core::mem::align_of::<Rect>() == 4);
    assert!(core::mem::offset_of!(Rect, x) == 0);
    assert!(core::mem::offset_of!(Rect, y) == 4);
    assert!(core::mem::offset_of!(Rect, width) == 8);
    assert!(core::mem::offset_of!(Rect, height) == 12);
    assert!(core::mem::size_of::<Input>() == 20);
    assert!(core::mem::align_of::<Input>() == 4);
    assert!(core::mem::offset_of!(Input, x) == 0);
    assert!(core::mem::offset_of!(Input, y) == 4);
    assert!(core::mem::offset_of!(Input, font) == 8);
    assert!(core::mem::offset_of!(Input, top) == 12);
    assert!(core::mem::offset_of!(Input, left) == 16);
    assert!(core::mem::size_of::<Record>() == 24);
    assert!(core::mem::align_of::<Record>() == 4);
    assert!(core::mem::offset_of!(Record, spine) == 0);
    assert!(core::mem::offset_of!(Record, start_page) == 2);
    assert!(core::mem::offset_of!(Record, end_page) == 4);
    assert!(core::mem::offset_of!(Record, page_count) == 6);
    assert!(core::mem::offset_of!(Record, start_word) == 8);
    assert!(core::mem::offset_of!(Record, end_word) == 10);
    assert!(core::mem::offset_of!(Record, signature) == 12);
    assert!(core::mem::offset_of!(Record, start_offset) == 16);
    assert!(core::mem::offset_of!(Record, end_offset) == 20);
};

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use std::vec::Vec;
    #[derive(Default)]
    struct Fake {
        bits: u32,
        element: bool,
        block: bool,
        current: u32,
        events: Vec<u8>,
        bad_element: bool,
        bad_block: bool,
    }
    unsafe extern "C" fn query(c: *mut c_void, q: u8, i: u32) -> u32 {
        let f = &mut *c.cast::<Fake>();
        match q {
            x if x == Query::HasSection as u8 => f.bits & 1,
            x if x == Query::HasClippings as u8 => (f.bits >> 1) & 1,
            x if x == Query::Page as u8 => {
                if f.bits & 4 != 0 {
                    0
                } else {
                    u32::MAX
                }
            }
            x if x == Query::PageCount as u8 => 2,
            x if x == Query::Spine as u8 => 1,
            x if x == Query::ElementCount as u8 => 3,
            x if x == Query::WordCount as u8 => {
                assert!(f.block);
                3
            }
            x if x == Query::Visible as u8 => u32::from(i != 1 || f.bits & 32 != 0),
            x if x == Query::WordX as u8 => i * 7,
            x if x == Query::LineX as u8 || x == Query::LineY as u8 => 1,
            x if x == Query::ClipCount as u8 => 2,
            x if x == Query::A4 as u8 => (f.bits >> 8) & 1,
            x if x == Query::Bw as u8 => (f.bits >> 9) & 1,
            x if x == Query::EmSpace as u8 => u32::from(i == 2 && f.bits & 1024 != 0),
            x if x == Query::SourceStart as u8 => i * 4,
            x if x == Query::SourceEnd as u8 => i * 4 + 4,
            _ => 0,
        }
    }
    unsafe extern "C" fn command(c: *mut c_void, id: u8, a: i32, _b: i32) -> u32 {
        let f = &mut *c.cast::<Fake>();
        f.events.push(id);
        match id {
            x if x == Command::ElementBegin as u8 => {
                assert!(!f.element && !f.block);
                f.element = true;
                f.current = a as u32;
                let ok = f.bits & 8 != 0 && a != 1;
                f.bad_element |= !ok;
                u32::from(ok)
            }
            x if x == Command::ElementEnd as u8 => {
                assert!(f.element && !f.block);
                f.element = false;
                0
            }
            x if x == Command::BlockBegin as u8 => {
                assert!(f.element && !f.block);
                f.block = true;
                let ok = f.bits & 16 != 0 && f.current != 2;
                f.bad_block |= !ok;
                u32::from(ok)
            }
            x if x == Command::BlockEnd as u8 => {
                assert!(f.element && f.block);
                f.block = false;
                0
            }
            x if x == Command::Signature as u8 => 1,
            x if x == Command::Measure as u8 => {
                if a == 0 && f.bits & 64 != 0 {
                    0
                } else {
                    4
                }
            }
            x if x == Command::EmWidth as u8 => 6,
            x if x == Command::LineHeight as u8 => 4,
            x if x == Command::Log as u8 => {
                assert!(!f.element && !f.block);
                0
            }
            _ => 0,
        }
    }
    unsafe extern "C" fn paint(c: *mut c_void, r: *const Rect) {
        let f = &mut *c.cast::<Fake>();
        assert!(f.element && f.block);
        assert!((*r).width > 0);
    }
    unsafe extern "C" fn clipping(c: *mut c_void, i: u32, r: *mut Record) {
        let f = &mut *c.cast::<Fake>();
        *r = Record {
            spine: if i == 1 && f.bits & 128 != 0 { 1 } else { 2 },
            page_count: 2,
            signature: 1,
            start_offset: 0,
            end_offset: 12,
            ..Record::default()
        };
    }
    #[test]
    fn borrowed_views_release_in_order_on_every_exit() {
        let mut saw_hit = false;
        let mut saw_element_failure = false;
        let mut saw_block_failure = false;
        for bits in 0..2048 {
            for operation in [0, 1] {
                for x in [-1, 2, 8, 15] {
                    let mut f = Fake {
                        bits,
                        ..Fake::default()
                    };
                    let s = Services {
                        context: (&mut f as *mut Fake).cast(),
                        query: Some(query),
                        command: Some(command),
                        paint: Some(paint),
                        clipping: Some(clipping),
                    };
                    let input = Input {
                        x,
                        y: 2,
                        ..Input::default()
                    };
                    let result = unsafe { run(&s, operation, &input) };
                    saw_hit |= operation == 0 && result >= 0;
                    saw_element_failure |= f.bad_element;
                    saw_block_failure |= f.bad_block;
                    assert!(!f.element && !f.block);
                    for (begin, end) in [
                        (Command::ElementBegin, Command::ElementEnd),
                        (Command::BlockBegin, Command::BlockEnd),
                    ] {
                        assert_eq!(
                            f.events.iter().filter(|&&x| x == begin as u8).count(),
                            f.events.iter().filter(|&&x| x == end as u8).count()
                        );
                    }
                }
            }
        }
        assert!(saw_hit && saw_element_failure && saw_block_failure);
    }
    #[test]
    fn invalid_service_input_and_operation_have_no_effects() {
        let mut f = Fake::default();
        let input = Input::default();
        let s = Services {
            context: (&mut f as *mut Fake).cast(),
            query: Some(query),
            command: Some(command),
            paint: Some(paint),
            clipping: Some(clipping),
        };
        unsafe {
            assert_eq!(run(core::ptr::null(), 0, &input), -1);
            assert_eq!(run(&s, 0, core::ptr::null()), -1);
            assert_eq!(run(&s, 255, &input), -1);
            assert_eq!(run(&Services { query: None, ..s }, 0, &input), -1);
            assert_eq!(run(&Services { command: None, ..s }, 0, &input), -1);
            assert_eq!(run(&Services { paint: None, ..s }, 0, &input), -1);
            assert_eq!(
                run(
                    &Services {
                        clipping: None,
                        ..s
                    },
                    0,
                    &input
                ),
                -1
            );
        }
        assert!(f.events.is_empty());
    }
}
