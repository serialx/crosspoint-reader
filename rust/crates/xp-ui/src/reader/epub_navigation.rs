//! Chapter, progress and link navigation with synchronous borrowed resources.
use super::services::{Core, Guard};
use core::ffi::c_void;
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Field {
    Spine = 0,
    CachedSpine = 1,
    NextPage = 2,
    CachedCount = 3,
    HasCachedOffset = 4,
    CachedOffset = 5,
    HasCurrentOffset = 6,
    CurrentOffset = 7,
    PendingOffset = 8,
    PercentJump = 9,
    SpineProgress = 10,
    FootnoteDepth = 11,
    SavedSpine0 = 12,
    SavedPage0 = 13,
    SavedSpine1 = 14,
    SavedPage1 = 15,
    SavedSpine2 = 16,
    SavedPage2 = 17,
}

#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Query {
    HasBook = 0,
    BookSize = 1,
    SpineCount = 2,
    FindSpine = 3,
    Cumulative = 4,
    HasSection = 5,
    SectionPage = 6,
    SectionCount = 7,
    EstimatedCount = 8,
    Building = 9,
    MapOffset = 10,
    GetOffset = 11,
    OffsetValue = 12,
    FallbackSpine = 13,
    FallbackPage = 14,
    SyncHasOffset = 15,
    SyncOffset = 16,
    SyncSpine = 17,
    SyncPage = 18,
    SyncCount = 19,
    SyncHasProgress = 20,
    ResolveHref = 21,
}

#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Command {
    Lock = 0,
    Unlock = 1,
    DropSection = 2,
    Update = 3,
    SetSectionPage = 4,
    Fallback = 5,
    PrepareAnchor = 6,
    MoveAnchor = 7,
    LogUnresolved = 8,
    LogSaved = 9,
    LogRestored = 10,
    LogNavigated = 11,
    SaveProgress = 12,
}

#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Operation {
    Percent = 0,
    Progress = 1,
    Deferred = 2,
    Clear = 3,
    Save = 4,
    Remember = 5,
    Href = 6,
    Restore = 7,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub context: *mut c_void,
    pub read: Option<unsafe extern "C" fn(*mut c_void, u8) -> u32>,
    pub write: Option<unsafe extern "C" fn(*mut c_void, u8, u32)>,
    pub query: Option<unsafe extern "C" fn(*mut c_void, u8, u32) -> u32>,
    pub command: Option<unsafe extern "C" fn(*mut c_void, u8, i32, i32) -> u32>,
    pub href: *const u8,
    pub href_len: u32,
}
struct Engine {
    core: Core,
    query: unsafe extern "C" fn(*mut c_void, u8, u32) -> u32,
}
impl Engine {
    fn f(&self, field: Field) -> u32 {
        self.read_id(field as u8)
    }
    fn read_id(&self, field: u8) -> u32 {
        self.core.read(field)
    }
    fn set(&self, field: Field, value: u32) {
        self.write_id(field as u8, value);
    }
    fn write_id(&self, field: u8, value: u32) {
        self.core.write(field, value);
    }
    fn q(&self, query: Query) -> u32 {
        self.qa(query, 0)
    }
    fn qa(&self, query: Query, a: u32) -> u32 {
        unsafe { (self.query)(self.core.context, query as u8, a) }
    }
    fn c(&self, command: Command, a: i32, b: i32) -> u32 {
        self.core.command(command as u8, a, b)
    }
    fn op(&self, command: Command) -> u32 {
        self.core.op(command as u8)
    }
    fn lock(&self) -> Guard<'_> {
        self.op(Command::Lock);
        self.core.guard(Command::Unlock as u8)
    }
    fn clear(&self) {
        self.set(Field::CachedCount, 0);
        self.set(Field::HasCachedOffset, 0);
    }
    fn percent(&self, percent: i32) {
        if self.q(Query::HasBook) == 0 {
            return;
        }
        let size = self.q(Query::BookSize);
        if size == 0 {
            return;
        }
        let percent = percent.clamp(0, 100) as u32;
        let mut target = (size / 100) * percent + (size % 100) * percent / 100;
        if percent >= 100 {
            target = size - 1;
        }
        let count = self.q(Query::SpineCount) as i32;
        if count == 0 {
            return;
        }
        let mut spine = count - 1;
        let mut previous = 0;
        let found = self.qa(Query::FindSpine, target) as i32;
        if found >= 0 {
            spine = found;
            if found > 0 {
                previous = self.qa(Query::Cumulative, (found - 1) as u32);
            }
        }
        let cumulative = self.qa(Query::Cumulative, spine as u32);
        let span = cumulative.saturating_sub(previous);
        let progress = if span == 0 {
            0.0
        } else {
            target.wrapping_sub(previous) as f32 / span as f32
        };
        self.set(Field::SpineProgress, progress.clamp(0.0, 1.0).to_bits());
        {
            let _lock = self.lock();
            self.clear();
            self.set(Field::Spine, spine as u32);
            self.set(Field::NextPage, 0);
            self.set(Field::PercentJump, 1);
            self.op(Command::DropSection);
        }
        self.op(Command::Update);
    }
    fn progress(&self) {
        let spine = self.q(Query::SyncSpine) as i32;
        if self.q(Query::SyncHasOffset) != 0
            && spine >= 0
            && spine < self.q(Query::SpineCount) as i32
        {
            let _lock = self.lock();
            self.clear();
            let page = if self.q(Query::HasSection) != 0 && self.f(Field::Spine) as i32 == spine {
                self.qa(Query::MapOffset, self.q(Query::SyncOffset))
            } else {
                0
            };
            if page & 0x10000 != 0 {
                self.c(Command::SetSectionPage, (page & 0xffff) as i32, 0);
            } else {
                self.set(Field::Spine, spine as u32);
                self.set(Field::PendingOffset, self.q(Query::SyncOffset));
                self.set(
                    Field::NextPage,
                    (self.q(Query::SyncPage) as i32).max(0) as u32,
                );
                self.op(Command::DropSection);
            }
            self.op(Command::Update);
            return;
        }
        let mut target_spine = spine;
        let mut target_page = self.q(Query::SyncPage) as i32;
        let active_total = if self.q(Query::HasSection) != 0 {
            self.q(Query::EstimatedCount) as i32
        } else {
            0
        };
        let saved_count = self.q(Query::SyncCount) as i32;
        let matches = self.q(Query::HasSection) != 0
            && saved_count > 0
            && self.f(Field::Spine) as i32 == spine
            && target_page >= 0
            && target_page < saved_count
            && active_total == saved_count;
        if !matches && self.q(Query::SyncHasProgress) != 0 {
            let count = if self.q(Query::HasSection) != 0 {
                self.q(Query::EstimatedCount)
            } else {
                self.f(Field::CachedCount)
            };
            self.c(Command::Fallback, self.f(Field::Spine) as i32, count as i32);
            target_spine = self.q(Query::FallbackSpine) as i32;
            target_page = self.q(Query::FallbackPage) as i32;
        }
        let _lock = self.lock();
        self.clear();
        if self.f(Field::Spine) as i32 != target_spine {
            self.set(Field::Spine, target_spine as u32);
            self.set(Field::NextPage, target_page as u32);
            self.op(Command::DropSection);
        } else if self.q(Query::HasSection) != 0 && self.q(Query::SectionPage) as i32 != target_page
        {
            self.c(Command::SetSectionPage, target_page.max(0), 0);
        } else if self.q(Query::HasSection) == 0 {
            self.set(Field::NextPage, target_page as u32);
        }
        self.op(Command::Update);
    }
    fn deferred(&self) -> bool {
        if (self.f(Field::HasCachedOffset) == 0 && self.f(Field::CachedCount) == 0)
            || self.q(Query::HasSection) == 0
            || self.q(Query::Building) != 0
        {
            return false;
        }
        let mut changed = false;
        if self.f(Field::Spine) == self.f(Field::CachedSpine) {
            let mut page = self.q(Query::SectionPage) as i32;
            let mut mapped = false;
            if self.f(Field::HasCachedOffset) != 0 {
                let offset_page = self.qa(Query::MapOffset, self.f(Field::CachedOffset));
                if offset_page & 0x10000 != 0 {
                    page = (offset_page & 0xffff) as i32;
                    mapped = true;
                }
            }
            let cached = self.f(Field::CachedCount) as i32;
            let count = self.q(Query::SectionCount) as i32;
            if !mapped && cached > 0 && count != cached {
                let progress = self.q(Query::SectionPage) as i32 as f32 / cached as f32;
                page = (progress * count as f32) as i32;
            }
            page = page.max(0);
            if count > 0 && page >= count {
                page = count - 1;
            }
            if page != self.q(Query::SectionPage) as i32 {
                self.c(Command::SetSectionPage, page, 0);
                changed = true;
            }
        }
        self.clear();
        changed
    }
    fn save(&self, spine: i32, page: i32) -> u32 {
        let mut present = 0;
        let mut offset = 0;
        if self.q(Query::HasSection) != 0
            && spine == self.f(Field::Spine) as i32
            && page >= 0
            && page < self.q(Query::SectionCount) as i32
        {
            if page == self.q(Query::SectionPage) as i32 && self.f(Field::HasCurrentOffset) != 0 {
                present = 1;
                offset = self.f(Field::CurrentOffset);
            } else {
                present = self.qa(Query::GetOffset, page as u32);
                if present != 0 {
                    offset = self.q(Query::OffsetValue);
                }
            }
        }
        self.c(Command::SaveProgress, present as i32, offset as i32)
    }
    fn remember(&self) {
        self.set(Field::HasCachedOffset, 0);
        if self.q(Query::HasSection) != 0 {
            let page = self.q(Query::SectionPage) as i32;
            if page >= 0
                && page < self.q(Query::SectionCount) as i32
                && self.qa(Query::GetOffset, page as u32) != 0
            {
                self.set(Field::CachedOffset, self.q(Query::OffsetValue));
            }
        }
    }
    fn href(&self, href: &[u8], save: bool) {
        if self.q(Query::HasBook) == 0 {
            return;
        }
        if let Some(hash) = href.iter().position(|byte| *byte == b'#') {
            if hash + 1 < href.len() {
                self.c(
                    Command::PrepareAnchor,
                    (hash + 1) as i32,
                    (href.len() - hash - 1) as i32,
                );
            }
        }
        let target = if href.first() == Some(&b'#') {
            self.f(Field::Spine) as i32
        } else {
            self.q(Query::ResolveHref) as i32
        };
        if target < 0 {
            self.op(Command::LogUnresolved);
            return;
        }
        if save {
            let mut depth = self.f(Field::FootnoteDepth) as i32;
            if depth == 3 {
                for index in 0..4 {
                    self.write_id(
                        Field::SavedSpine0 as u8 + index,
                        self.read_id(Field::SavedSpine0 as u8 + index + 2),
                    );
                }
                depth -= 1;
                self.set(Field::FootnoteDepth, depth as u32);
            }
            let page = if self.q(Query::HasSection) != 0 {
                self.q(Query::SectionPage)
            } else {
                self.f(Field::NextPage)
            };
            self.write_id(
                Field::SavedSpine0 as u8 + 2 * depth as u8,
                self.f(Field::Spine),
            );
            self.write_id(Field::SavedSpine0 as u8 + 2 * depth as u8 + 1, page);
            depth += 1;
            self.set(Field::FootnoteDepth, depth as u32);
            self.c(Command::LogSaved, depth, page as i32);
        }
        {
            let _lock = self.lock();
            self.clear();
            self.op(Command::MoveAnchor);
            self.set(Field::Spine, target as u32);
            self.set(Field::NextPage, 0);
            self.op(Command::DropSection);
        }
        self.op(Command::Update);
        self.c(Command::LogNavigated, target, 0);
    }
    fn restore(&self) {
        let depth = self.f(Field::FootnoteDepth) as i32;
        if depth <= 0 {
            return;
        }
        let depth = depth - 1;
        self.set(Field::FootnoteDepth, depth as u32);
        let spine = self.read_id(Field::SavedSpine0 as u8 + 2 * depth as u8);
        let page = self.read_id(Field::SavedSpine0 as u8 + 2 * depth as u8 + 1);
        self.c(Command::LogRestored, depth, 0);
        {
            let _lock = self.lock();
            self.clear();
            self.set(Field::Spine, spine);
            self.set(Field::NextPage, page);
            self.op(Command::DropSection);
        }
        self.op(Command::Update);
    }
}
/// # Safety
/// Callbacks, context and href bytes remain valid for the synchronous call and
/// are not retained or reentered. Scalar callbacks use the short model lock;
/// resource callbacks run outside it. Saved-position depth is in 0..=3.
pub unsafe fn run(s: *const Services, operation: u8, a: i32, b: i32) -> u32 {
    let Some(s) = s.as_ref() else {
        return 0;
    };
    let (Some(read), Some(write), Some(query), Some(command)) =
        (s.read, s.write, s.query, s.command)
    else {
        return 0;
    };
    let e = Engine {
        core: Core {
            context: s.context,
            read,
            write,
            command,
        },
        query,
    };
    match operation {
        x if x == Operation::Percent as u8 => e.percent(a),
        x if x == Operation::Progress as u8 => e.progress(),
        x if x == Operation::Deferred as u8 => return u32::from(e.deferred()),
        x if x == Operation::Clear as u8 => e.clear(),
        x if x == Operation::Save as u8 => return e.save(a, b),
        x if x == Operation::Remember as u8 => e.remember(),
        x if x == Operation::Href as u8 => {
            let href = if s.href_len == 0 {
                &[]
            } else if s.href.is_null() {
                return 0;
            } else {
                core::slice::from_raw_parts(s.href, s.href_len as usize)
            };
            e.href(href, a != 0);
        }
        x if x == Operation::Restore as u8 => e.restore(),
        _ => {}
    }
    0
}

const _: () = {
    use core::mem::{align_of, offset_of, size_of};
    assert!(size_of::<Services>() == 7 * size_of::<usize>());
    assert!(align_of::<Services>() == align_of::<usize>());
    assert!(offset_of!(Services, context) == 0);
    assert!(offset_of!(Services, read) == size_of::<usize>());
    assert!(offset_of!(Services, write) == 2 * size_of::<usize>());
    assert!(offset_of!(Services, query) == 3 * size_of::<usize>());
    assert!(offset_of!(Services, command) == 4 * size_of::<usize>());
    assert!(offset_of!(Services, href) == 5 * size_of::<usize>());
    assert!(offset_of!(Services, href_len) == 6 * size_of::<usize>());
    assert!(Field::Spine as u8 == 0);
    assert!(Field::CachedSpine as u8 == 1);
    assert!(Field::NextPage as u8 == 2);
    assert!(Field::CachedCount as u8 == 3);
    assert!(Field::HasCachedOffset as u8 == 4);
    assert!(Field::CachedOffset as u8 == 5);
    assert!(Field::HasCurrentOffset as u8 == 6);
    assert!(Field::CurrentOffset as u8 == 7);
    assert!(Field::PendingOffset as u8 == 8);
    assert!(Field::PercentJump as u8 == 9);
    assert!(Field::SpineProgress as u8 == 10);
    assert!(Field::FootnoteDepth as u8 == 11);
    assert!(Field::SavedSpine0 as u8 == 12);
    assert!(Field::SavedPage0 as u8 == 13);
    assert!(Field::SavedSpine1 as u8 == 14);
    assert!(Field::SavedPage1 as u8 == 15);
    assert!(Field::SavedSpine2 as u8 == 16);
    assert!(Field::SavedPage2 as u8 == 17);
    assert!(Query::HasBook as u8 == 0);
    assert!(Query::BookSize as u8 == 1);
    assert!(Query::SpineCount as u8 == 2);
    assert!(Query::FindSpine as u8 == 3);
    assert!(Query::Cumulative as u8 == 4);
    assert!(Query::HasSection as u8 == 5);
    assert!(Query::SectionPage as u8 == 6);
    assert!(Query::SectionCount as u8 == 7);
    assert!(Query::EstimatedCount as u8 == 8);
    assert!(Query::Building as u8 == 9);
    assert!(Query::MapOffset as u8 == 10);
    assert!(Query::GetOffset as u8 == 11);
    assert!(Query::OffsetValue as u8 == 12);
    assert!(Query::FallbackSpine as u8 == 13);
    assert!(Query::FallbackPage as u8 == 14);
    assert!(Query::SyncHasOffset as u8 == 15);
    assert!(Query::SyncOffset as u8 == 16);
    assert!(Query::SyncSpine as u8 == 17);
    assert!(Query::SyncPage as u8 == 18);
    assert!(Query::SyncCount as u8 == 19);
    assert!(Query::SyncHasProgress as u8 == 20);
    assert!(Query::ResolveHref as u8 == 21);
    assert!(Command::Lock as u8 == 0);
    assert!(Command::Unlock as u8 == 1);
    assert!(Command::DropSection as u8 == 2);
    assert!(Command::Update as u8 == 3);
    assert!(Command::SetSectionPage as u8 == 4);
    assert!(Command::Fallback as u8 == 5);
    assert!(Command::PrepareAnchor as u8 == 6);
    assert!(Command::MoveAnchor as u8 == 7);
    assert!(Command::LogUnresolved as u8 == 8);
    assert!(Command::LogSaved as u8 == 9);
    assert!(Command::LogRestored as u8 == 10);
    assert!(Command::LogNavigated as u8 == 11);
    assert!(Command::SaveProgress as u8 == 12);
    assert!(Operation::Percent as u8 == 0);
    assert!(Operation::Progress as u8 == 1);
    assert!(Operation::Deferred as u8 == 2);
    assert!(Operation::Clear as u8 == 3);
    assert!(Operation::Save as u8 == 4);
    assert!(Operation::Remember as u8 == 5);
    assert!(Operation::Href as u8 == 6);
    assert!(Operation::Restore as u8 == 7);
};

#[cfg(test)]
mod tests {
    use super::*;
    struct Fake {
        fields: [u32; 18],
        queries: [u32; 22],
        locks: i32,
        calls: u32,
        offset_ready: bool,
        invalid: bool,
    }
    impl Fake {
        fn new(flags: u32, page: i32, depth: u32) -> Self {
            let mut fields = [0; 18];
            fields[Field::Spine as usize] = 2;
            fields[Field::CachedSpine as usize] = if flags & 1 != 0 { 2 } else { 3 };
            fields[Field::CachedCount as usize] = if flags & 2 != 0 { 10 } else { 0 };
            fields[Field::HasCachedOffset as usize] = (flags >> 2) & 1;
            fields[Field::HasCurrentOffset as usize] = (flags >> 3) & 1;
            fields[Field::FootnoteDepth as usize] = depth;
            fields[Field::NextPage as usize] = page as u32;
            let mut queries = [0; 22];
            queries[Query::HasBook as usize] = flags & 1;
            queries[Query::BookSize as usize] = if flags & 2 != 0 { 101 } else { 0 };
            queries[Query::SpineCount as usize] = if flags & 4 != 0 { 7 } else { 0 };
            queries[Query::FindSpine as usize] = if flags & 8 != 0 { 2 } else { u32::MAX };
            queries[Query::Cumulative as usize] = 101;
            queries[Query::HasSection as usize] = (flags >> 1) & 1;
            queries[Query::SectionPage as usize] = page as u32;
            queries[Query::SectionCount as usize] = if flags & 4 != 0 { 20 } else { 0 };
            queries[Query::EstimatedCount as usize] = 20;
            queries[Query::Building as usize] = (flags >> 3) & 1;
            queries[Query::MapOffset as usize] = if flags & 16 != 0 { 0x10007 } else { 0 };
            queries[Query::GetOffset as usize] = (flags >> 5) & 1;
            queries[Query::OffsetValue as usize] = 0xfeedbeef;
            queries[Query::SyncHasOffset as usize] = (flags >> 4) & 1;
            queries[Query::SyncSpine as usize] = if flags & 32 != 0 { 2 } else { u32::MAX };
            queries[Query::SyncPage as usize] = page as u32;
            queries[Query::SyncCount as usize] = if flags & 64 != 0 { 20 } else { 0 };
            queries[Query::SyncHasProgress as usize] = (flags >> 7) & 1;
            queries[Query::ResolveHref as usize] = if flags & 64 != 0 { 4 } else { u32::MAX };
            Self {
                fields,
                queries,
                locks: 0,
                calls: 0,
                offset_ready: false,
                invalid: false,
            }
        }
        fn services(&mut self) -> Services {
            Services {
                context: (self as *mut Self).cast(),
                read: Some(read),
                write: Some(write),
                query: Some(query),
                command: Some(command),
                href: b"x#anchor".as_ptr(),
                href_len: 8,
            }
        }
    }
    unsafe extern "C" fn read(p: *mut c_void, field: u8) -> u32 {
        let f = &mut *p.cast::<Fake>();
        f.calls += 1;
        f.fields[field as usize]
    }
    unsafe extern "C" fn write(p: *mut c_void, field: u8, value: u32) {
        let f = &mut *p.cast::<Fake>();
        f.calls += 1;
        f.fields[field as usize] = value;
        if field == Field::CachedOffset as u8 {
            f.fields[Field::HasCachedOffset as usize] = 1;
        }
    }
    unsafe extern "C" fn query(p: *mut c_void, query: u8, _: u32) -> u32 {
        let f = &mut *p.cast::<Fake>();
        f.calls += 1;
        if query == Query::GetOffset as u8 {
            f.offset_ready = f.queries[query as usize] != 0;
        }
        if query == Query::OffsetValue as u8 {
            f.invalid |= !f.offset_ready;
            f.offset_ready = false;
        }
        f.queries[query as usize]
    }
    unsafe extern "C" fn command(p: *mut c_void, command: u8, a: i32, _: i32) -> u32 {
        let f = &mut *p.cast::<Fake>();
        f.calls += 1;
        match command {
            x if x == Command::Lock as u8 => {
                f.invalid |= f.locks != 0;
                f.locks += 1;
            }
            x if x == Command::Unlock as u8 => {
                f.invalid |= f.locks != 1;
                f.locks -= 1;
            }
            x if x == Command::DropSection as u8 => {
                f.queries[Query::HasSection as usize] = 0;
            }
            x if x == Command::SetSectionPage as u8 => {
                f.queries[Query::SectionPage as usize] = a as u32;
            }
            x if x == Command::Fallback as u8 => {
                f.queries[Query::FallbackSpine as usize] = 4;
                f.queries[Query::FallbackPage as usize] = 9;
            }
            x if x == Command::SaveProgress as u8 => return 1,
            _ => {}
        }
        0
    }
    #[test]
    fn every_operation_balances_guards_and_offset_reads() {
        for operation in 0..=9 {
            for flags in 0..256 {
                for page in [-1, 3, 30] {
                    for depth in 0..=3 {
                        let mut f = Fake::new(flags, page, depth);
                        unsafe {
                            run(&f.services(), operation, 100, page);
                        }
                        assert_eq!(f.locks, 0);
                        assert!(!f.invalid);
                        if operation == 9 {
                            assert_eq!(f.calls, 0);
                        }
                    }
                }
            }
        }
        let mut f = Fake::new(255, 3, 0);
        let mut s = f.services();
        s.href = core::ptr::null();
        unsafe {
            run(&s, Operation::Href as u8, 1, 0);
        }
        assert_eq!(f.calls, 0);
    }
    #[test]
    fn absent_offset_does_not_read_a_previous_value() {
        for operation in [Operation::Save, Operation::Remember] {
            let mut f = Fake::new(255, 3, 0);
            f.fields[Field::HasCurrentOffset as usize] = 0;
            unsafe {
                run(&f.services(), operation as u8, 2, 3);
            }
            assert!(!f.invalid && !f.offset_ready);
            f.queries[Query::GetOffset as usize] = 0;
            f.queries[Query::OffsetValue as usize] = 0xdeadbeef;
            unsafe {
                run(&f.services(), operation as u8, 2, 3);
            }
            assert!(!f.invalid && !f.offset_ready);
            assert_eq!(f.locks, 0);
        }
    }
    #[test]
    fn absent_services_do_not_start_a_navigation_operation() {
        for operation in 0..=8 {
            assert_eq!(unsafe { run(core::ptr::null(), operation, 0, 0) }, 0);
            let services = Services {
                context: core::ptr::null_mut(),
                read: None,
                write: None,
                query: None,
                command: None,
                href: core::ptr::null(),
                href_len: 0,
            };
            assert_eq!(unsafe { run(&services, operation, 0, 0) }, 0);
        }
    }
}
