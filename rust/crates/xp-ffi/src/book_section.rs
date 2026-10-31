//! Synchronous section file policy over borrowed HAL services.
use core::{ffi::c_void, slice};
use xp_core::section::{self, Kind, Record, HEADER_SIZE, PARTIAL_VERSION, VERSION};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FileServices {
    ctx: *mut c_void,
    operation: Option<unsafe extern "C" fn(*mut c_void, u32, usize) -> usize>,
    read: Option<unsafe extern "C" fn(*mut c_void, *mut u8, usize) -> usize>,
    write: Option<unsafe extern "C" fn(*mut c_void, *const u8, usize) -> usize>,
    resize_text: Option<unsafe extern "C" fn(*mut c_void, usize) -> *mut u8>,
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32, u32)>,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum FileOp {
    OpenRead,
    Seek,
    Size,
    Position,
    Close,
    ReleaseText,
    ExistsFinal,
    RemoveFinal,
    RemoveTemporary,
    PromoteTemporary,
    ExistsTemporary,
    IsOpen,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum Diagnostic {
    ShortRecord,
    InvalidRecord,
    InvalidString,
    VisibleSeek,
    VisibleRead,
    WriteRecord,
    InvalidLut,
    Promote,
    TruncatedHeader,
    UnknownVersion,
    Parameters,
    MalformedPartial,
    Loaded,
    CacheMissing,
    CacheClearFailed,
    CacheCleared,
    PageFileClosed,
    PageWriteFailed,
    PageProcessed,
    HeaderFileClosed,
}
impl FileServices {
    fn op(&self, op: FileOp, argument: usize) -> usize {
        self.operation
            .map_or(0, |call| unsafe { call(self.ctx, op as u32, argument) })
    }
    fn log(&self, event: Diagnostic, a: u32, b: u32) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, event as u32, a, b) };
        }
    }
    fn read(&self, output: &mut [u8]) -> bool {
        self.read.is_some_and(|read| unsafe {
            read(self.ctx, output.as_mut_ptr(), output.len()) == output.len()
        })
    }
    fn record(&self, kind: Kind) -> Option<Record> {
        let mut bytes = [0; HEADER_SIZE];
        let input = bytes.get_mut(..kind.size())?;
        if !self.read(input) {
            self.log(Diagnostic::ShortRecord, kind as u32, 0);
            return None;
        }
        match section::decode(kind, input) {
            Ok((record, _)) => Some(record),
            Err(_) => {
                self.log(Diagnostic::InvalidRecord, kind as u32, 0);
                None
            }
        }
    }
    fn value(&self, kind: Kind) -> Option<u32> {
        Some(self.record(kind)?.values[0])
    }
    fn seek(&self, position: u32) -> bool {
        self.op(FileOp::Seek, position as usize) != 0
    }
    fn size(&self) -> u32 {
        self.op(FileOp::Size, 0) as u32
    }
    fn table(&self, offset: u32, index: u32, width: u32, minimum: u32) -> Option<u32> {
        section::table_entry(offset, index, width, self.size(), minimum)
    }
    fn table_seek(&self, offset: u32, index: u32, width: u32, minimum: u32) -> bool {
        self.table(offset, index, width, minimum)
            .is_some_and(|position| self.seek(position))
    }
    unsafe fn string_matches(&self, needle: &[u8]) -> Option<bool> {
        let length = self.value(Kind::U32)?;
        if self
            .table(self.op(FileOp::Position, 0) as u32, 0, length, 0)
            .is_none()
        {
            self.log(Diagnostic::InvalidString, length, 0);
            return None;
        }
        let data = self.resize_text?(self.ctx, length as usize);
        let text = if length == 0 {
            &mut []
        } else {
            if data.is_null() {
                return None;
            }
            slice::from_raw_parts_mut(data, length as usize)
        };
        if length != 0 && !self.read(text) {
            return None;
        }
        Some(text == needle)
    }
}

// Operations are appended in xp_book.h; output has three u32 slots for page ranges.
fn query(
    io: &FileServices,
    operation: u32,
    index: u32,
    first: bool,
    output: &mut [u32; 3],
) -> Option<()> {
    if io.op(FileOp::OpenRead, 0) == 0 {
        return None;
    }
    let header = io.record(Kind::Header)?;
    let v = &header.values;
    match operation {
        0 => {
            if u64::from(v[15]) > u64::from(u32::MAX).wrapping_sub(4 * (u64::from(index) + 1))
                || !io.table_seek(v[15], index, 4, 0)
            {
                return None;
            }
            let position = io.value(Kind::U32)?;
            let mut visible = 0;
            if u64::from(v[19]) <= u64::from(u32::MAX).wrapping_sub(4 * (u64::from(index) + 1))
                && io.table_seek(v[19], index, 4, HEADER_SIZE as u32)
            {
                if let Some(value) = io.value(Kind::U32) {
                    visible = value;
                }
            }
            let mut end = v[15];
            if index.wrapping_add(1) < v[14] {
                if !io.table_seek(v[15], index.wrapping_add(1), 4, 0) {
                    return None;
                }
                end = io.value(Kind::U32)?;
            }
            if position < HEADER_SIZE as u32
                || end <= position
                || end > v[15]
                || end > io.size()
                || !io.seek(position)
            {
                return None;
            }
            *output = [position, end, visible];
        }
        1 => {
            if v[0] != u32::from(VERSION) {
                return None;
            }
            output[0] = v[14];
        }
        2 => {
            if !io.table_seek(v[16], 0, 2, 1) {
                return None;
            }
            output[0] = io.value(Kind::U16)?;
        }
        3 | 4 => {
            if !io.table_seek(v[17], 0, 2, 1) {
                return None;
            }
            let count = io.value(Kind::U16)?;
            if count == 0 {
                return None;
            }
            let offset = if operation == 3 {
                v[17].wrapping_add(2)
            } else {
                v[18]
            };
            if io.table(offset, count - 1, 2, 1).is_none() || !io.seek(offset) {
                return None;
            }
            for i in 0..count {
                if io.value(Kind::U16)? >= index {
                    output[0] = i;
                    return Some(());
                }
            }
            output[0] = count - 1;
        }
        5 => {
            if !io.table_seek(v[17], 0, 2, 1) {
                return None;
            }
            let count = io.value(Kind::U16)?;
            if index >= count || !io.table_seek(v[17], index.wrapping_add(1), 2, 1) {
                return None;
            }
            output[0] = io.value(Kind::U16)?;
        }
        6 => {
            if index >= v[14]
                || u64::from(v[19]) > u64::from(u32::MAX).wrapping_sub(4 * (u64::from(index) + 1))
                || !io.table_seek(v[19], index, 4, HEADER_SIZE as u32)
            {
                return None;
            }
            output[0] = io.value(Kind::U32)?;
        }
        7 => {
            if v[14] == 0 || io.table(v[19], v[14] - 1, 4, HEADER_SIZE as u32).is_none() {
                return None;
            }
            if !io.seek(v[19]) {
                io.log(Diagnostic::VisibleSeek, 0, 0);
                return None;
            }
            let mut result = 0;
            let mut last = 0;
            let mut starts = [0; 128];
            let mut base = 0;
            while base < v[14] {
                let batch = (v[14] - base).min(32);
                let bytes = starts.get_mut(..batch as usize * 4)?;
                if !io.read(bytes) {
                    io.log(Diagnostic::VisibleRead, 0, 0);
                    return None;
                }
                for (i, word) in bytes.chunks_exact(4).enumerate() {
                    let (record, _) = section::decode(Kind::U32, word).ok()?;
                    last = record.values[0];
                    if last > index {
                        output[0] = result;
                        return Some(());
                    }
                    result = (base + i as u32) as u16 as u32;
                    if first && last == index {
                        output[0] = result;
                        return Some(());
                    }
                }
                base += batch;
            }
            if v[0] == u32::from(PARTIAL_VERSION) && index > last {
                return None;
            }
            output[0] = result;
        }
        _ => return None,
    }
    Some(())
}

/// # Safety
/// Services and callbacks remain valid for the call; output points to three writable u32s.
/// Callbacks must not retain pointers or reenter this operation.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_query(
    services: *const FileServices,
    operation: u32,
    index: u32,
    first: u8,
    output: *mut [u32; 3],
) -> u8 {
    let (Some(services), Some(output)) = (
        services
            .as_ref()
            .copied()
            .filter(|s| s.operation.is_some() && s.read.is_some()),
        output.as_mut(),
    ) else {
        return 0;
    };
    query(&services, operation, index, first != 0, output).is_some() as u8
}

/// # Safety
/// Services and needle are readable through the call. Output is one writable u32;
/// resize_text returns disjoint writable storage until ReleaseText or the next resize.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_anchor(
    services: *const FileServices,
    needle: *const u8,
    length: usize,
    output: *mut u32,
) -> u8 {
    let (Some(io), Some(output)) = (
        services
            .as_ref()
            .copied()
            .filter(|s| s.operation.is_some() && s.read.is_some()),
        output.as_mut(),
    ) else {
        return 0;
    };
    if length > isize::MAX as usize || (needle.is_null() && length != 0) {
        return 0;
    }
    let needle = if length == 0 {
        &[]
    } else {
        slice::from_raw_parts(needle, length)
    };
    let mut result = [0; 3];
    if query(&io, 2, 0, false, &mut result).is_none() {
        return 0;
    }
    for _ in 0..result[0] {
        let Some(matches) = io.string_matches(needle) else {
            return 0;
        };
        let Some(page) = io.value(Kind::U16) else {
            return 0;
        };
        io.op(FileOp::ReleaseText, 0);
        if matches {
            *output = page;
            return 1;
        }
    }
    0
}

#[repr(C)]
pub struct Estimate {
    consumed: u32,
    total: u32,
    partial_consumed: u32,
    partial_total: u32,
    smoothed_at: u32,
    smoothed: f32,
    pages: u16,
    built_pages: u16,
    partial_pages: u16,
    partial: u8,
    building: u8,
}
impl Estimate {
    fn clamp(&self, value: u64) -> u16 {
        if value <= u64::from(self.pages) {
            self.pages
        } else {
            value.min(60000) as u16
        }
    }
    fn partial_estimate(&self) -> u16 {
        if self.partial == 0
            || self.partial_consumed == 0
            || self.partial_total <= self.partial_consumed
        {
            return self.pages;
        }
        self.clamp(
            u64::from(self.partial_pages) * u64::from(self.partial_total)
                / u64::from(self.partial_consumed),
        )
    }
}
/// # Safety
/// State is one initialized writable estimate, disjoint from other accesses.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_estimate(state: *mut Estimate) -> u16 {
    let Some(s) = state.as_mut() else {
        return 0;
    };
    if s.building == 0 {
        return if s.partial != 0 {
            s.partial_estimate()
        } else {
            s.pages
        };
    }
    if s.built_pages == 0 || s.consumed == 0 || s.total <= s.consumed {
        return s.partial_estimate();
    }
    let raw = u64::from(s.built_pages) * u64::from(s.total) / u64::from(s.consumed);
    if s.smoothed <= 0.0 {
        s.smoothed = raw as f32;
    } else if s.consumed != s.smoothed_at {
        s.smoothed += 0.25 * (raw as f32 - s.smoothed);
    }
    s.smoothed_at = s.consumed;
    s.clamp((s.smoothed + 0.5) as u64)
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    ctx: *mut c_void,
    operation: Option<unsafe extern "C" fn(*mut c_void, u32, u32) -> u32>,
    millis: Option<unsafe extern "C" fn(*mut c_void) -> u32>,
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32)>,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum Op {
    HasBuild,
    HasParser,
    BuiltPages,
    Partial,
    PartialPages,
    Total,
    ReusedHtml,
    ParserStep,
    ParserFinish,
    ParserAbort,
    ParserConsumed,
    Yield,
    CssClear,
    ResetBuild,
    FileOpen,
    FileClose,
    TmpBinRemove,
    FinalExists,
    FinalRemove,
    TmpHtmlExists,
    TmpHtmlRemove,
    PromoteHtml,
    CommitFinal,
    CommitPartial,
    SetComplete,
    SetPartial,
    SetPartialPages,
    SetPages,
    SetBuiltPages,
    SetPartialConsumed,
    SetPartialTotal,
    SetConsumed,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum Event {
    NoBuild,
    ParseError,
    FinalizeError,
    HtmlPromotion,
    Suspended,
}
impl Services {
    fn call(&self, op: Op, argument: u32) -> u32 {
        self.operation
            .map_or(0, |call| unsafe { call(self.ctx, op as u32, argument) })
    }
    fn get(&self, op: Op) -> u32 {
        self.call(op, 0)
    }
    fn yes(&self, op: Op) -> bool {
        self.get(op) != 0
    }
    fn log(&self, event: Event, argument: u32) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, event as u32, argument) };
        }
    }
}
fn abandon(s: &Services) {
    if !s.yes(Op::HasBuild) {
        return;
    }
    if s.yes(Op::HasParser) {
        s.get(Op::ParserAbort);
    }
    s.get(Op::CssClear);
    if s.yes(Op::FileOpen) {
        s.get(Op::FileClose);
        s.get(Op::TmpBinRemove);
    }
    if s.yes(Op::FinalExists) {
        s.get(Op::FinalRemove);
    }
    if !s.yes(Op::ReusedHtml) && s.yes(Op::TmpHtmlExists) {
        s.get(Op::TmpHtmlRemove);
    }
    s.get(Op::ResetBuild);
    s.call(Op::SetComplete, 0);
    s.call(Op::SetPartial, 0);
    s.call(Op::SetPartialPages, 0);
    s.call(Op::SetPages, 0);
    s.call(Op::SetBuiltPages, 0);
}
fn suspend(s: &Services) {
    if !s.yes(Op::HasBuild) {
        return;
    }
    let built = s.get(Op::BuiltPages);
    let worth = built > 0 && (!s.yes(Op::Partial) || built > s.get(Op::PartialPages));
    let mut committed = false;
    if worth {
        let consumed = s.get(Op::ParserConsumed);
        committed = s.call(Op::CommitPartial, consumed) != 0;
        if committed {
            s.call(Op::SetPartial, 1);
            s.call(Op::SetPartialPages, built);
            s.call(Op::SetPartialConsumed, consumed);
            s.call(Op::SetPartialTotal, s.get(Op::Total));
            s.log(Event::Suspended, built);
        }
    }
    if s.yes(Op::HasParser) {
        s.get(Op::ParserAbort);
    }
    s.get(Op::CssClear);
    if !committed && s.yes(Op::FileOpen) {
        s.get(Op::FileClose);
        s.get(Op::TmpBinRemove);
    }
    if !s.yes(Op::ReusedHtml) && s.yes(Op::TmpHtmlExists) {
        s.get(Op::TmpHtmlRemove);
    }
    s.get(Op::ResetBuild);
    s.call(Op::SetComplete, 0);
    s.call(
        Op::SetPages,
        if s.yes(Op::Partial) {
            s.get(Op::PartialPages)
        } else {
            0
        },
    );
    s.call(Op::SetBuiltPages, 0);
}
fn finish(s: &Services) -> bool {
    if !s.yes(Op::ParserFinish) {
        s.log(Event::FinalizeError, 0);
        abandon(s);
        return false;
    }
    if !s.yes(Op::ReusedHtml) && !s.yes(Op::PromoteHtml) {
        s.log(Event::HtmlPromotion, 0);
        s.get(Op::TmpHtmlRemove);
    }
    let committed = s.yes(Op::CommitFinal);
    s.get(Op::CssClear);
    s.get(Op::ResetBuild);
    if !committed {
        s.call(Op::SetPartial, 0);
        s.call(Op::SetPartialPages, 0);
        s.call(Op::SetPages, 0);
        s.call(Op::SetBuiltPages, 0);
        return false;
    }
    s.call(Op::SetComplete, 1);
    s.call(Op::SetPartial, 0);
    s.call(Op::SetPartialPages, 0);
    s.call(Op::SetPages, s.get(Op::BuiltPages));
    true
}
fn advance(s: &Services, max: i32) -> bool {
    if !s.yes(Op::HasParser) {
        s.log(Event::NoBuild, 0);
        return false;
    }
    let start = s.get(Op::BuiltPages) as i32;
    loop {
        match s.get(Op::ParserStep) {
            2 => {
                s.log(Event::ParseError, 0);
                abandon(s);
                return false;
            }
            1 => return finish(s),
            _ => {}
        }
        if (max > 0 && s.get(Op::BuiltPages) as i32 - start >= max) || s.yes(Op::Yield) {
            s.call(Op::SetConsumed, s.get(Op::ParserConsumed));
            return true;
        }
    }
}
/// # Safety
/// Services are readable and callbacks own their context throughout the call.
/// A callback may call a separate codec/commit operation, never this transition.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_transition(
    services: *const Services,
    operation: u32,
    argument: i32,
) -> u8 {
    let Some(s) = services.as_ref().copied().filter(|s| s.operation.is_some()) else {
        return 0;
    };
    match operation {
        0 => advance(&s, argument) as u8,
        1 => finish(&s) as u8,
        2 => {
            suspend(&s);
            1
        }
        3 => {
            abandon(&s);
            1
        }
        _ => 0,
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Anchor {
    text: *const u8,
    length: usize,
    page: u16,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct BuildRecords {
    ctx: *mut c_void,
    entries: usize,
    anchors: usize,
    entry: unsafe extern "C" fn(*mut c_void, usize, u32) -> u32,
    anchor: unsafe extern "C" fn(*mut c_void, usize, *mut Anchor),
}
impl BuildRecords {
    fn entry(&self, index: usize, field: u32) -> u32 {
        unsafe { (self.entry)(self.ctx, index, field) }
    }
    fn anchor(&self, index: usize) -> Anchor {
        let mut anchor = Anchor {
            text: core::ptr::null(),
            length: 0,
            page: 0,
        };
        unsafe { (self.anchor)(self.ctx, index, &mut anchor) };
        anchor
    }
}
impl FileServices {
    fn write(&self, input: &[u8]) -> bool {
        self.write.is_some_and(|write| unsafe {
            write(self.ctx, input.as_ptr(), input.len()) == input.len()
        })
    }
    fn write_value(&self, kind: Kind, value: u32) -> bool {
        let mut record = Record {
            kind: kind as u32,
            values: [0; 20],
        };
        record.values[0] = value;
        let mut bytes = [0; HEADER_SIZE];
        if let Ok(len) = section::encode(&record, &mut bytes) {
            if bytes.get(..len).is_some_and(|bytes| self.write(bytes)) {
                return true;
            }
        }
        self.log(Diagnostic::WriteRecord, kind as u32, 0);
        false
    }
    fn fail_commit(&self) -> bool {
        self.op(FileOp::Close, 0);
        self.op(FileOp::RemoveTemporary, 0);
        false
    }
}
fn commit(
    io: &FileServices,
    records: &BuildRecords,
    version: u8,
    pages: u16,
    consumed: u32,
    total: u32,
) -> bool {
    let partial = version == PARTIAL_VERSION;
    let lut = io.op(FileOp::Position, 0) as u32;
    for index in 0..records.entries {
        let offset = records.entry(index, 0);
        if offset == 0 {
            io.log(Diagnostic::InvalidLut, 0, 0);
            return io.fail_commit();
        }
        if !io.write_value(Kind::U32, offset) {
            return io.fail_commit();
        }
    }
    let anchors = io.op(FileOp::Position, 0) as u32;
    let mut count = 0u16;
    for index in 0..records.anchors {
        if !partial || records.anchor(index).page < pages {
            count = count.wrapping_add(1);
        }
    }
    if !io.write_value(Kind::U16, count.into()) {
        return io.fail_commit();
    }
    for index in 0..records.anchors {
        let anchor = records.anchor(index);
        if partial && anchor.page >= pages {
            continue;
        }
        if anchor.length > isize::MAX as usize || (anchor.length != 0 && anchor.text.is_null()) {
            return io.fail_commit();
        }
        let text = if anchor.length == 0 {
            &[]
        } else {
            unsafe { slice::from_raw_parts(anchor.text, anchor.length) }
        };
        if !io.write_value(Kind::U32, anchor.length as u32)
            || !io.write(text)
            || !io.write_value(Kind::U16, anchor.page.into())
        {
            return io.fail_commit();
        }
    }
    let paragraphs = io.op(FileOp::Position, 0) as u32;
    if !io.write_value(Kind::U16, records.entries as u16 as u32) {
        return io.fail_commit();
    }
    for index in 0..records.entries {
        if !io.write_value(Kind::U16, records.entry(index, 1)) {
            return io.fail_commit();
        }
    }
    let lists = io.op(FileOp::Position, 0) as u32;
    for index in 0..records.entries {
        if !io.write_value(Kind::U16, records.entry(index, 2)) {
            return io.fail_commit();
        }
    }
    let visible = io.op(FileOp::Position, 0) as u32;
    for index in 0..records.entries {
        if !io.write_value(Kind::U32, records.entry(index, 3)) {
            return io.fail_commit();
        }
    }
    if partial && (!io.write_value(Kind::U32, consumed) || !io.write_value(Kind::U32, total)) {
        return io.fail_commit();
    }
    io.op(FileOp::Seek, HEADER_SIZE - 22);
    if !io.write_value(Kind::U16, pages.into()) {
        return io.fail_commit();
    }
    for offset in [lut, anchors, paragraphs, lists, visible] {
        if !io.write_value(Kind::U32, offset) {
            return io.fail_commit();
        }
    }
    io.op(FileOp::Seek, 0);
    if !io.write_value(Kind::U8, version.into()) {
        return io.fail_commit();
    }
    io.op(FileOp::Close, 0);
    if io.op(FileOp::ExistsFinal, 0) != 0 {
        io.op(FileOp::RemoveFinal, 0);
    }
    if io.op(FileOp::PromoteTemporary, 0) == 0 {
        io.log(Diagnostic::Promote, 0, 0);
        io.op(FileOp::RemoveTemporary, 0);
        return false;
    }
    true
}
/// # Safety
/// Both service descriptors and borrowed entry/anchor storage remain valid for
/// the call. Callbacks neither retain buffer pointers nor reenter this commit.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_commit(
    services: *const FileServices,
    records: *const BuildRecords,
    version: u8,
    pages: u16,
    consumed: u32,
    total: u32,
) -> u8 {
    let (Some(io), Some(records)) = (
        services
            .as_ref()
            .copied()
            .filter(|s| s.operation.is_some() && s.write.is_some()),
        records.as_ref().copied(),
    ) else {
        return 0;
    };
    commit(&io, &records, version, pages, consumed, total) as u8
}

#[repr(u32)]
#[derive(Clone, Copy)]
enum StartOp {
    HasBuild,
    ReleaseFonts,
    SetComplete,
    SetBuiltPages,
    Partial,
    PartialPages,
    SetPages,
    TmpBinExists,
    TmpBinRemove,
    PreparePaths,
    MkdirSections,
    HtmlExists,
    MkdirHtml,
    Delay,
    TmpHtmlExists,
    TmpHtmlRemove,
    TmpHtmlOpen,
    Stream,
    TmpHtmlSize,
    TmpHtmlClose,
    TmpHtmlDrop,
    PromoteHtml,
    BinOpen,
    WriteHeader,
    AllocateContext,
    ConfigureContext,
    EmbeddedStyle,
    GetCss,
    LoadCss,
    ClearCss,
    FileClose,
    ContextReused,
    TocFirst,
    TocCount,
    TocRead,
    TocSpine,
    Spine,
    TocLength,
    TocMove,
    TocDrop,
    AllocateParser,
    ConfigureParser,
    Activate,
    BeginParse,
    Abandon,
    SetTotal,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum StartEvent {
    Active,
    Reused,
    Retry,
    RemovedIncomplete,
    StreamFailed,
    Streamed,
    Promotion,
    ContextOom,
    CssLow,
    CssInvalid,
    ParserOom,
    BeginFailed,
}

impl Services {
    fn start(&self, op: StartOp, argument: u32) -> u32 {
        self.operation
            .map_or(0, |call| unsafe { call(self.ctx, op as u32, argument) })
    }
    fn start_log(&self, event: StartEvent, argument: u32) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, event as u32, argument) };
        }
    }
}
fn start_cleanup(s: &Services, remove_html: bool) {
    s.start(StartOp::FileClose, 0);
    s.start(StartOp::TmpBinRemove, 0);
    if remove_html {
        s.start(StartOp::TmpHtmlRemove, 0);
    }
}
fn start_build(s: &Services) -> bool {
    use StartOp::*;
    let get = |op| s.start(op, 0);
    if get(HasBuild) != 0 {
        s.start_log(StartEvent::Active, 0);
        return false;
    }
    get(ReleaseFonts);
    s.start(SetComplete, 0);
    s.start(SetBuiltPages, 0);
    s.start(
        SetPages,
        if get(Partial) != 0 {
            get(PartialPages)
        } else {
            0
        },
    );
    if get(TmpBinExists) != 0 {
        get(TmpBinRemove);
    }
    get(PreparePaths);
    get(MkdirSections);
    let reused = get(HtmlExists) != 0;
    let mut cached = reused;
    if reused {
        s.start_log(StartEvent::Reused, 0);
    } else {
        get(MkdirHtml);
        let mut streamed = false;
        let mut size = 0;
        for attempt in 0..3 {
            if attempt > 0 {
                s.start_log(StartEvent::Retry, attempt + 1);
                s.start(Delay, 50);
            }
            if get(TmpHtmlExists) != 0 {
                get(TmpHtmlRemove);
            }
            if get(TmpHtmlOpen) == 0 {
                get(TmpHtmlDrop);
                continue;
            }
            streamed = s.start(Stream, 8192) != 0;
            size = get(TmpHtmlSize);
            get(TmpHtmlClose);
            if !streamed && get(TmpHtmlExists) != 0 {
                get(TmpHtmlRemove);
                s.start_log(StartEvent::RemovedIncomplete, 0);
            }
            get(TmpHtmlDrop);
            if streamed {
                break;
            }
        }
        if !streamed {
            s.start_log(StartEvent::StreamFailed, 0);
            return false;
        }
        s.start_log(StartEvent::Streamed, size);
        if get(PromoteHtml) != 0 {
            cached = true;
        } else {
            s.start_log(StartEvent::Promotion, 0);
        }
    }
    if get(BinOpen) == 0 {
        if !reused {
            get(TmpHtmlRemove);
        }
        return false;
    }
    get(WriteHeader);
    if get(AllocateContext) == 0 {
        s.start_log(StartEvent::ContextOom, 0);
        start_cleanup(s, !reused);
        return false;
    }
    s.start(ConfigureContext, cached as u32);
    if get(EmbeddedStyle) != 0 && get(GetCss) != 0 {
        match get(LoadCss) {
            1 => {
                s.start_log(StartEvent::CssLow, 0);
                get(ClearCss);
                start_cleanup(s, get(ContextReused) == 0);
                return false;
            }
            2 => s.start_log(StartEvent::CssInvalid, 0),
            _ => {}
        }
    }
    let first = get(TocFirst) as i32;
    if first >= 0 {
        let mut i = first;
        while i < get(TocCount) as i32 {
            s.start(TocRead, i as u32);
            if get(TocSpine) != get(Spine) {
                get(TocDrop);
                break;
            }
            if get(TocLength) != 0 {
                get(TocMove);
            }
            get(TocDrop);
            i += 1;
        }
    }
    if get(AllocateParser) == 0 {
        s.start_log(StartEvent::ParserOom, 0);
        get(ClearCss);
        start_cleanup(s, !reused);
        return false;
    }
    get(ConfigureParser);
    get(Activate);
    if get(BeginParse) == 0 {
        s.start_log(StartEvent::BeginFailed, 0);
        get(Abandon);
        return false;
    }
    get(SetTotal);
    true
}
/// # Safety
/// Services own their context and borrowed settings until this synchronous call returns.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_start(services: *const Services) -> u8 {
    let Some(services) = services.as_ref().copied().filter(|s| s.operation.is_some()) else {
        return 0;
    };
    start_build(&services) as u8
}

fn clear_cache(io: &FileServices) -> bool {
    if io.op(FileOp::ExistsTemporary, 0) != 0 {
        io.op(FileOp::RemoveTemporary, 0);
    }
    if io.op(FileOp::ExistsFinal, 0) == 0 {
        io.log(Diagnostic::CacheMissing, 0, 0);
        return true;
    }
    if io.op(FileOp::RemoveFinal, 0) == 0 {
        io.log(Diagnostic::CacheClearFailed, 0, 0);
        return false;
    }
    io.log(Diagnostic::CacheCleared, 0, 0);
    true
}
#[repr(C)]
pub struct Loaded {
    consumed: u32,
    total: u32,
    pages: u16,
    partial_pages: u16,
    partial: u8,
}
fn load(io: &FileServices, expected: &[u32; 20], state: &mut Loaded) -> bool {
    if io.op(FileOp::OpenRead, 0) == 0 {
        return false;
    }
    if io.size() < HEADER_SIZE as u32 {
        io.op(FileOp::Close, 0);
        io.log(Diagnostic::TruncatedHeader, 0, 0);
        clear_cache(io);
        return false;
    }
    let mut bytes = [0; HEADER_SIZE];
    let mut rejected = None;
    let record = if io.read(&mut bytes) {
        section::decode(Kind::Header, &bytes)
            .map(|v| v.0)
            .map_err(|_| {
                rejected = Some(bytes[0]);
            })
            .ok()
    } else {
        io.log(Diagnostic::ShortRecord, Kind::Header as u32, 0);
        None
    };
    let Some(header) = record else {
        io.op(FileOp::Close, 0);
        if let Some(version) = rejected {
            io.log(Diagnostic::UnknownVersion, version as u32, 0);
        }
        clear_cache(io);
        return false;
    };
    if !section::header_matches(
        &header,
        &Record {
            kind: Kind::Header as u32,
            values: *expected,
        },
    ) {
        io.op(FileOp::Close, 0);
        io.log(Diagnostic::Parameters, 0, 0);
        clear_cache(io);
        return false;
    }
    state.pages = header.values[14] as u16;
    let partial = header.values[0] == PARTIAL_VERSION as u32;
    if partial {
        let valid = (|| {
            if header.values[19] > u32::MAX - u32::from(state.pages) * 4 - 8 {
                return None;
            }
            let trailer = section::partial_trailer(&header, io.size())?;
            if !io.seek(trailer) {
                return None;
            }
            state.consumed = io.value(Kind::U32)?;
            state.total = io.value(Kind::U32)?;
            Some(())
        })()
        .is_some();
        if !valid {
            io.op(FileOp::Close, 0);
            io.log(Diagnostic::MalformedPartial, 0, 0);
            clear_cache(io);
            state.pages = 0;
            return false;
        }
        state.partial = 1;
        state.partial_pages = state.pages;
    }
    io.op(FileOp::Close, 0);
    io.log(Diagnostic::Loaded, state.pages as u32, partial as u32);
    true
}
/// # Safety
/// All pointers describe initialized borrowed values valid until return; output state is exclusive.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_load(
    io: *const FileServices,
    expected: *const [u32; 20],
    state: *mut Loaded,
) -> u8 {
    let (Some(io), Some(expected), Some(state)) = (io.as_ref(), expected.as_ref(), state.as_mut())
    else {
        return 0;
    };
    load(io, expected, state) as u8
}
/// # Safety
/// Services and their context remain valid until return.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_clear(io: *const FileServices) -> u8 {
    io.as_ref().is_some_and(clear_cache) as u8
}
/// # Safety
/// Bytes is readable for length; built/pages are exclusive initialized outputs and cannot alias.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_append(
    io: *const FileServices,
    bytes: *const u8,
    length: usize,
    built: *mut u16,
    pages: *mut u16,
) -> u32 {
    let (Some(io), Some(built), Some(pages)) = (io.as_ref(), built.as_mut(), pages.as_mut()) else {
        return 0;
    };
    if io.op(FileOp::IsOpen, 0) == 0 {
        io.log(Diagnostic::PageFileClosed, *built as u32, 0);
        return 0;
    }
    let position = io.op(FileOp::Position, 0) as u32;
    if io
        .write
        .is_none_or(|write| write(io.ctx, bytes, length) != length)
    {
        io.log(Diagnostic::PageWriteFailed, *built as u32, 0);
        return 0;
    }
    io.log(Diagnostic::PageProcessed, *built as u32, 0);
    *built = built.wrapping_add(1);
    *pages = (*pages).max(*built);
    position
}
/// # Safety
/// Borrowed services and twenty initialized header fields remain valid until return.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_header(io: *const FileServices, values: *const [u32; 20]) {
    let (Some(io), Some(values)) = (io.as_ref(), values.as_ref()) else {
        return;
    };
    if io.op(FileOp::IsOpen, 0) == 0 {
        io.log(Diagnostic::HeaderFileClosed, 0, 0);
        return;
    }
    let record = Record {
        kind: Kind::Header as u32,
        values: *values,
    };
    let mut bytes = [0; HEADER_SIZE];
    if section::encode(&record, &mut bytes).is_err()
        || io
            .write
            .is_none_or(|write| write(io.ctx, bytes.as_ptr(), bytes.len()) != bytes.len())
    {
        io.log(Diagnostic::WriteRecord, Kind::Header as u32, 0);
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct TocServices {
    ctx: *mut c_void,
    operation: Option<unsafe extern "C" fn(*mut c_void, u32, usize) -> usize>,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum TocOp {
    First,
    Count,
    Allocate,
    OpenAnchors,
    BuildAnchors,
    SelectBuild,
    SelectDisk,
    ActiveHash,
    TocMatches,
    BuildOverride,
    RewindDisk,
    ReadToc,
    TocSpine,
    Spine,
    TocLength,
    TocHash,
    DropToc,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct TocTarget {
    hash: usize,
    index: i32,
}
impl TocServices {
    fn call(&self, op: TocOp, arg: usize) -> usize {
        self.operation
            .map_or(0, |call| unsafe { call(self.ctx, op as u32, arg) })
    }
    fn get(&self, op: TocOp) -> usize {
        self.call(op, 0)
    }
}
struct TocSelection {
    page: i32,
    selected: i32,
    latest: i32,
}
fn consider_anchor(
    s: &TocServices,
    targets: &[TocTarget],
    selection: &mut TocSelection,
    page: usize,
    disk: bool,
) {
    let hash = s.get(TocOp::ActiveHash);
    for target in targets {
        if target.hash != hash || s.call(TocOp::TocMatches, target.index as usize) == 0 {
            continue;
        }
        let resolved = if disk {
            s.call(TocOp::BuildOverride, page)
        } else {
            page
        } as i32;
        if resolved <= selection.page
            && (resolved > selection.latest
                || (resolved == selection.latest && target.index > selection.selected))
        {
            selection.selected = target.index;
            selection.latest = resolved;
        }
    }
}
fn resolve_toc(
    s: &TocServices,
    targets: &[TocTarget],
    selection: &mut TocSelection,
    anchors: usize,
) {
    let count = s.get(TocOp::BuildAnchors);
    for i in 0..count {
        let page = s.call(TocOp::SelectBuild, i);
        consider_anchor(s, targets, selection, page, false);
    }
    if s.get(TocOp::RewindDisk) != 0 {
        for _ in 0..anchors {
            let page = s.get(TocOp::SelectDisk);
            if page == usize::MAX {
                break;
            }
            consider_anchor(s, targets, selection, page, true);
        }
    }
}
/// # Safety
/// Services are valid for the call; Allocate lends 64 writable targets owned until return.
/// No callback accesses that target allocation while Rust holds it.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_toc(services: *const TocServices, page: i32) -> i32 {
    use TocOp::*;
    let Some(s) = services.as_ref().copied().filter(|s| s.operation.is_some()) else {
        return -1;
    };
    let selected = s.get(First) as i32;
    let pointer = s.call(Allocate, 64) as *mut TocTarget;
    if pointer.is_null() {
        return selected;
    }
    let targets = slice::from_raw_parts_mut(pointer, 64);
    let anchors = s.get(OpenAnchors);
    let mut selection = TocSelection {
        page,
        selected,
        latest: -1,
    };
    let mut count = 0;
    let mut i = selected.max(0);
    while i < s.get(Count) as i32 {
        s.call(ReadToc, i as usize);
        if s.get(TocSpine) == s.get(Spine) {
            if s.get(TocLength) == 0 {
                if page >= 0 && selection.latest <= 0 {
                    selection.selected = i;
                    selection.latest = 0;
                }
            } else {
                targets[count] = TocTarget {
                    hash: s.get(TocHash),
                    index: i,
                };
                count += 1;
                if count == 64 {
                    resolve_toc(&s, targets, &mut selection, anchors);
                    count = 0;
                }
            }
        }
        s.get(DropToc);
        i += 1;
    }
    if count > 0 {
        resolve_toc(&s, &targets[..count], &mut selection, anchors);
    }
    selection.selected
}

fn build_anchor(records: &BuildRecords, needle: &[u8]) -> Option<u32> {
    for i in 0..records.anchors {
        let anchor = records.anchor(i);
        let text = if anchor.length == 0 {
            &[]
        } else {
            unsafe { slice::from_raw_parts(anchor.text, anchor.length) }
        };
        if text == needle {
            return Some(anchor.page as u32);
        }
    }
    None
}
/// # Safety
/// Records and the needle are borrowed for the call; output is a disjoint writable u32.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_build_anchor(
    records: *const BuildRecords,
    needle: *const u8,
    length: usize,
    page: *mut u32,
) -> u8 {
    let (Some(records), Some(page)) = (records.as_ref(), page.as_mut()) else {
        return 0;
    };
    let needle = if length == 0 {
        &[]
    } else {
        slice::from_raw_parts(needle, length)
    };
    let Some(found) = build_anchor(records, needle) else {
        return 0;
    };
    *page = found;
    1
}
/// # Safety
/// All services/records/needle remain borrowed; page is one disjoint writable u32.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_find_anchor(
    io: *const FileServices,
    records: *const BuildRecords,
    needle: *const u8,
    length: usize,
    page: *mut u32,
) -> u8 {
    if xp_book_section_build_anchor(records, needle, length, page) != 0 {
        1
    } else {
        xp_book_section_anchor(io, needle, length, page)
    }
}
fn reached(records: &BuildRecords, offset: u32) -> bool {
    records.entries != 0 && offset <= records.entry(records.entries - 1, 3)
}
/// # Safety
/// Records is borrowed and callbacks preserve its entry count throughout the call.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_reached(records: *const BuildRecords, offset: u32) -> u8 {
    records.as_ref().is_some_and(|r| reached(r, offset)) as u8
}
/// # Safety
/// Services and records are borrowed; page is a disjoint writable u32.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_visible(
    io: *const FileServices,
    records: *const BuildRecords,
    offset: u32,
    first: u8,
    page: *mut u32,
) -> u8 {
    let (Some(io), Some(records), Some(page)) = (io.as_ref(), records.as_ref(), page.as_mut())
    else {
        return 0;
    };
    if reached(records, offset) {
        let mut result = 0;
        for i in 0..records.entries {
            let start = records.entry(i, 3);
            if first != 0 && start == offset {
                *page = i as u16 as u32;
                return 1;
            }
            if start > offset {
                break;
            }
            result = i as u16 as u32;
        }
        *page = result;
        return 1;
    }
    let mut out = [0; 3];
    if query(io, 7, offset, first != 0, &mut out).is_none() {
        return 0;
    }
    *page = out[0];
    1
}
/// # Safety
/// Services and records remain valid; offset is one disjoint writable u32.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_page_visible(
    io: *const FileServices,
    records: *const BuildRecords,
    page: u16,
    offset: *mut u32,
) -> u8 {
    let (Some(io), Some(records), Some(offset)) = (io.as_ref(), records.as_ref(), offset.as_mut())
    else {
        return 0;
    };
    if usize::from(page) < records.entries {
        *offset = records.entry(usize::from(page), 3);
        return 1;
    }
    let mut out = [0; 3];
    if query(io, 6, page as u32, false, &mut out).is_none() {
        return 0;
    }
    *offset = out[0];
    1
}
#[no_mangle]
pub extern "C" fn xp_book_section_page_source(
    page: i32,
    entries: usize,
    pages: u16,
    partial_pages: u16,
    building: u8,
    partial: u8,
) -> u8 {
    if page < 0 {
        return 0;
    }
    if building != 0 && page < entries as i32 {
        return 1;
    }
    let disk = if partial != 0 {
        partial_pages
    } else if building != 0 {
        0
    } else {
        pages
    };
    if page < disk as i32 {
        2
    } else {
        0
    }
}
#[repr(C)]
pub struct PageLoader {
    ctx: *mut c_void,
    load: unsafe extern "C" fn(*mut c_void, u32, u32) -> *mut c_void,
}
/// # Safety
/// All pointers are borrowed for the call; loader transfers an opaque owned page or null.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_built_page(
    io: *const FileServices,
    records: *const BuildRecords,
    page: i32,
    loader: *const PageLoader,
) -> *mut c_void {
    let (Some(io), Some(records), Some(loader)) = (io.as_ref(), records.as_ref(), loader.as_ref())
    else {
        return core::ptr::null_mut();
    };
    if page < 0 || page >= records.entries as i32 || io.op(FileOp::IsOpen, 0) == 0 {
        return core::ptr::null_mut();
    }
    let page = page as usize;
    let position = records.entry(page, 0);
    if position == 0 {
        return core::ptr::null_mut();
    }
    let write = io.op(FileOp::Position, 0) as u32;
    let end = if page + 1 < records.entries {
        records.entry(page + 1, 0)
    } else {
        write
    };
    if end <= position || end > write || !io.seek(position) {
        return core::ptr::null_mut();
    }
    let result = (loader.load)(loader.ctx, end - position, records.entry(page, 3));
    io.seek(write);
    result
}

#[repr(C)]
pub struct TextSpan {
    data: *const u8,
    length: usize,
}
#[repr(C)]
pub struct PageText {
    ctx: *mut c_void,
    elements: u16,
    field: unsafe extern "C" fn(*mut c_void, u16, u32) -> u32,
    word: unsafe extern "C" fn(*mut c_void, u16, u16) -> TextSpan,
    resize: unsafe extern "C" fn(*mut c_void, usize) -> *mut u8,
}
/// # Safety
/// Source owns a stable page and exclusive string; word views and resized storage do not overlap.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_text(source: *const PageText) -> u8 {
    let Some(s) = source.as_ref() else {
        return 0;
    };
    let mut previous = 0usize;
    for element in 0..s.elements {
        if (s.field)(s.ctx, element, 0) != 1 || (s.field)(s.ctx, element, 1) == 0 {
            continue;
        }
        let count = (s.field)(s.ctx, element, 2) as u16;
        for index in 0..count {
            let word = (s.word)(s.ctx, element, index);
            let word = if word.length == 0 {
                &[]
            } else {
                slice::from_raw_parts(word.data, word.length)
            };
            let required = match section::encode_text_word(previous, word, &mut []) {
                Ok(count) | Err(section::Error::NeedOutput(count)) => count,
                Err(_) => return 0,
            };
            let length = previous.wrapping_add(required);
            let output = (s.resize)(s.ctx, length);
            let output = if required == 0 {
                &mut []
            } else {
                slice::from_raw_parts_mut(output.add(previous), required)
            };
            if section::encode_text_word(previous, word, output).is_err() {
                return 0;
            }
            previous = length;
        }
    }
    1
}
