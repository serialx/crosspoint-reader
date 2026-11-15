//! Shared Section cache reads over a synchronous, exclusively owned cursor.
use xp_core::section::{self, Kind, Record, HEADER_SIZE, PARTIAL_VERSION, VERSION};

#[repr(u32)]
#[derive(Clone, Copy)]
pub(crate) enum Diagnostic {
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
#[derive(Clone, Copy)]
pub(crate) enum Query {
    PageRange(u32),
    PageCount,
    AnchorMap,
    PageForParagraph(u32),
    PageForList(u32),
    ParagraphForPage(u32),
    VisibleForPage(u32),
    PageForVisible { offset: u32, first: bool },
    Invalid,
}

pub(crate) trait Reader {
    fn open_read(&mut self) -> bool;
    fn read(&mut self, output: &mut [u8]) -> bool;
    fn seek(&mut self, position: u32) -> bool;
    fn size(&mut self) -> u32;
    fn position(&mut self) -> u32;
    fn log(&mut self, event: Diagnostic, a: u32, b: u32);
    fn text_matches(&mut self, length: u32, needle: &[u8]) -> Option<bool>;
    fn release_text(&mut self);

    fn record(&mut self, kind: Kind) -> Option<Record> {
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
    fn value(&mut self, kind: Kind) -> Option<u32> {
        Some(self.record(kind)?.values[0])
    }
    fn table(&mut self, offset: u32, index: u32, width: u32, minimum: u32) -> Option<u32> {
        section::table_entry(offset, index, width, self.size(), minimum)
    }
    fn table_seek(&mut self, offset: u32, index: u32, width: u32, minimum: u32) -> bool {
        self.table(offset, index, width, minimum)
            .is_some_and(|position| self.seek(position))
    }
    fn string_matches(&mut self, needle: &[u8]) -> Option<bool> {
        let length = self.value(Kind::U32)?;
        let position = self.position();
        if self.table(position, 0, length, 0).is_none() {
            self.log(Diagnostic::InvalidString, length, 0);
            return None;
        }
        self.text_matches(length, needle)
    }
}

pub(crate) fn query(io: &mut impl Reader, operation: Query, output: &mut [u32; 3]) -> Option<()> {
    if !io.open_read() {
        return None;
    }
    let header = io.record(Kind::Header)?;
    let v = &header.values;
    match operation {
        Query::PageRange(index) => {
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
        Query::PageCount => {
            if v[0] != u32::from(VERSION) {
                return None;
            }
            output[0] = v[14];
        }
        Query::AnchorMap => {
            if !io.table_seek(v[16], 0, 2, 1) {
                return None;
            }
            output[0] = io.value(Kind::U16)?;
        }
        Query::PageForParagraph(index) | Query::PageForList(index) => {
            if !io.table_seek(v[17], 0, 2, 1) {
                return None;
            }
            let count = io.value(Kind::U16)?;
            if count == 0 {
                return None;
            }
            let offset = if matches!(operation, Query::PageForParagraph(_)) {
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
        Query::ParagraphForPage(index) => {
            if !io.table_seek(v[17], 0, 2, 1) {
                return None;
            }
            let count = io.value(Kind::U16)?;
            if index >= count || !io.table_seek(v[17], index.wrapping_add(1), 2, 1) {
                return None;
            }
            output[0] = io.value(Kind::U16)?;
        }
        Query::VisibleForPage(index) => {
            if index >= v[14]
                || u64::from(v[19]) > u64::from(u32::MAX).wrapping_sub(4 * (u64::from(index) + 1))
                || !io.table_seek(v[19], index, 4, HEADER_SIZE as u32)
            {
                return None;
            }
            output[0] = io.value(Kind::U32)?;
        }
        Query::PageForVisible {
            offset: index,
            first,
        } => {
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
                for (i, word) in bytes.as_chunks::<4>().0.iter().enumerate() {
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
        Query::Invalid => return None,
    }
    Some(())
}

pub(crate) fn anchor(io: &mut impl Reader, needle: &[u8]) -> Option<u32> {
    let mut result = [0; 3];
    query(io, Query::AnchorMap, &mut result)?;
    for _ in 0..result[0] {
        let matches = io.string_matches(needle)?;
        let page = io.value(Kind::U16)?;
        io.release_text();
        if matches {
            return Some(page);
        }
    }
    None
}

/// Physical cache paths; readers and builders share invalidation ordering.
pub(crate) trait Storage: Reader {
    fn close(&mut self);
    fn exists_final(&mut self) -> bool;
    fn remove_final(&mut self) -> bool;
    fn exists_temporary(&mut self) -> bool;
    fn remove_temporary(&mut self);
}

pub(crate) fn clear(io: &mut impl Storage) -> bool {
    if io.exists_temporary() {
        io.remove_temporary();
    }
    if !io.exists_final() {
        io.log(Diagnostic::CacheMissing, 0, 0);
        return true;
    }
    if !io.remove_final() {
        io.log(Diagnostic::CacheClearFailed, 0, 0);
        return false;
    }
    io.log(Diagnostic::CacheCleared, 0, 0);
    true
}
#[derive(Clone, Copy, Default)]
pub(crate) struct LoadState {
    pub(crate) consumed: u32,
    pub(crate) total: u32,
    pub(crate) pages: u16,
    pub(crate) partial_pages: u16,
    pub(crate) partial: u8,
}
pub(crate) fn load(io: &mut impl Storage, expected: &[u32; 20], state: &mut LoadState) -> bool {
    if !io.open_read() {
        return false;
    }
    if io.size() < HEADER_SIZE as u32 {
        io.close();
        io.log(Diagnostic::TruncatedHeader, 0, 0);
        clear(io);
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
        io.close();
        if let Some(version) = rejected {
            io.log(Diagnostic::UnknownVersion, version as u32, 0);
        }
        clear(io);
        return false;
    };
    if !section::header_matches(
        &header,
        &Record {
            kind: Kind::Header as u32,
            values: *expected,
        },
    ) {
        io.close();
        io.log(Diagnostic::Parameters, 0, 0);
        clear(io);
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
            io.close();
            io.log(Diagnostic::MalformedPartial, 0, 0);
            clear(io);
            state.pages = 0;
            return false;
        }
        state.partial = 1;
        state.partial_pages = state.pages;
    }
    io.close();
    io.log(Diagnostic::Loaded, state.pages as u32, partial as u32);
    true
}

pub(crate) trait Writer: Storage {
    fn is_open(&mut self) -> bool;
    fn write(&mut self, input: &[u8]) -> bool;
    fn promote_temporary(&mut self) -> bool;
    fn write_value(&mut self, kind: Kind, value: u32) -> bool {
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
    fn fail_commit(&mut self) -> bool {
        self.close();
        self.remove_temporary();
        false
    }
}

pub(crate) struct Anchor<'a> {
    pub(crate) text: &'a [u8],
    pub(crate) page: u16,
}

pub(crate) trait Records {
    fn entries(&self) -> usize;
    fn anchors(&self) -> usize;
    fn offset(&self, index: usize) -> u32;
    fn paragraph(&self, index: usize) -> u32;
    fn list(&self, index: usize) -> u32;
    fn visible(&self, index: usize) -> u32;
    fn anchor_page(&self, index: usize) -> u16;
    fn anchor(&self, index: usize, limit: Option<u16>) -> Result<Option<Anchor<'_>>, ()>;
}

pub(crate) fn commit(
    io: &mut impl Writer,
    records: &impl Records,
    version: u8,
    pages: u16,
    consumed: u32,
    total: u32,
) -> bool {
    let partial = version == PARTIAL_VERSION;
    let lut = io.position();
    for index in 0..records.entries() {
        let offset = records.offset(index);
        if offset == 0 {
            io.log(Diagnostic::InvalidLut, 0, 0);
            return io.fail_commit();
        }
        if !io.write_value(Kind::U32, offset) {
            return io.fail_commit();
        }
    }
    let anchors = io.position();
    let mut count = 0u16;
    for index in 0..records.anchors() {
        if !partial || records.anchor_page(index) < pages {
            count = count.wrapping_add(1);
        }
    }
    if !io.write_value(Kind::U16, count.into()) {
        return io.fail_commit();
    }
    for index in 0..records.anchors() {
        let anchor = match records.anchor(index, partial.then_some(pages)) {
            Ok(Some(anchor)) => anchor,
            Ok(None) => continue,
            Err(()) => return io.fail_commit(),
        };
        if !io.write_value(Kind::U32, anchor.text.len() as u32)
            || !io.write(anchor.text)
            || !io.write_value(Kind::U16, anchor.page.into())
        {
            return io.fail_commit();
        }
    }
    let paragraphs = io.position();
    if !io.write_value(Kind::U16, records.entries() as u16 as u32) {
        return io.fail_commit();
    }
    for index in 0..records.entries() {
        if !io.write_value(Kind::U16, records.paragraph(index)) {
            return io.fail_commit();
        }
    }
    let lists = io.position();
    for index in 0..records.entries() {
        if !io.write_value(Kind::U16, records.list(index)) {
            return io.fail_commit();
        }
    }
    let visible = io.position();
    for index in 0..records.entries() {
        if !io.write_value(Kind::U32, records.visible(index)) {
            return io.fail_commit();
        }
    }
    if partial && (!io.write_value(Kind::U32, consumed) || !io.write_value(Kind::U32, total)) {
        return io.fail_commit();
    }
    io.seek((HEADER_SIZE - 22) as u32);
    if !io.write_value(Kind::U16, pages.into()) {
        return io.fail_commit();
    }
    for offset in [lut, anchors, paragraphs, lists, visible] {
        if !io.write_value(Kind::U32, offset) {
            return io.fail_commit();
        }
    }
    io.seek(0);
    if !io.write_value(Kind::U8, version.into()) {
        return io.fail_commit();
    }
    io.close();
    if io.exists_final() {
        io.remove_final();
    }
    if !io.promote_temporary() {
        io.log(Diagnostic::Promote, 0, 0);
        io.remove_temporary();
        return false;
    }
    true
}

// The successful count and readable watermark are separate from LUT entry count.
pub(crate) fn append(io: &mut impl Writer, bytes: &[u8], built: &mut u16, pages: &mut u16) -> u32 {
    if !io.is_open() {
        io.log(Diagnostic::PageFileClosed, u32::from(*built), 0);
        return 0;
    }
    let position = io.position();
    if !io.write(bytes) {
        io.log(Diagnostic::PageWriteFailed, u32::from(*built), 0);
        return 0;
    }
    io.log(Diagnostic::PageProcessed, u32::from(*built), 0);
    *built = built.wrapping_add(1);
    *pages = (*pages).max(*built);
    position
}

pub(crate) fn header(io: &mut impl Writer, values: &[u32; 20]) {
    if !io.is_open() {
        io.log(Diagnostic::HeaderFileClosed, 0, 0);
        return;
    }
    let record = Record {
        kind: Kind::Header as u32,
        values: *values,
    };
    let mut bytes = [0; HEADER_SIZE];
    if section::encode(&record, &mut bytes).is_err() || !io.write(&bytes) {
        io.log(Diagnostic::WriteRecord, Kind::Header as u32, 0);
    }
}
