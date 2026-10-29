//! Bounded CSS parser and v19 cache, with no storage or renderer dependencies.
extern crate alloc;
mod limits;
mod selector;
mod style;
use alloc::vec::Vec;
use core::cmp::Ordering;
pub use limits::*;
pub use selector::{ancestor, Ancestor};
use selector::{Key, CONTEXT, FIRST_LETTER};
pub use style::{inline, Style};
use style::{tokens, trim, whitespace};
const MAX_STYLES: usize = 256;

#[derive(Clone, Copy)]
#[repr(u32)]
pub enum Diagnostic {
    IndexOom = 1,
    SelectorOom,
    StyleOom,
    SelectorLong,
    StoreLimit,
    StoreOom,
    InputTruncated,
    CacheFlags,
    CacheCount,
    CacheSelectorLength,
    CacheDuplicate,
    ScratchOom,
    CacheScratchOom,
}

/// Borrowed for one call; never stored in parser state or used on success hot paths.
#[derive(Clone, Copy, Default)]
#[repr(C)]
pub struct Diagnostics {
    pub ctx: *mut core::ffi::c_void,
    pub diagnostic: Option<extern "C" fn(*mut core::ffi::c_void, u32, u32, u32, u32, u32)>,
}
impl Diagnostics {
    pub fn emit(&self, event: Diagnostic, a: usize, b: usize) {
        if let Some(callback) = self.diagnostic {
            callback(self.ctx, event as u32, a as u32, b as u32, 0, 0);
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Limit,
    OutOfMemory,
    Invalid,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum ParseResult {
    Complete,
    Partial,
    Error,
}
#[derive(Clone, Copy)]
#[repr(C)]
struct Entry {
    offset: u32,
    style: u16,
    len: u16,
}
fn grow<T>(
    pool: &mut Vec<T>,
    needed: usize,
    initial: usize,
    cap: usize,
    diagnostics: &Diagnostics,
    event: Diagnostic,
) -> Result<(), Error> {
    if needed <= pool.capacity() {
        return Ok(());
    }
    if needed > cap {
        return Err(Error::Limit);
    }
    let mut capacity = if pool.capacity() == 0 {
        initial
    } else {
        pool.capacity() * 2
    };
    while capacity < needed {
        capacity *= 2;
    }
    capacity = capacity.min(cap);
    pool.try_reserve_exact(capacity - pool.len()).map_err(|_| {
        diagnostics.emit(event, capacity, 0);
        Error::OutOfMemory
    })
}
#[derive(Default)]
pub struct Store {
    entries: Vec<Entry>,
    selectors: Vec<u8>,
    styles: Vec<Style>,
    stopped: bool,
    has_id: bool,
    has_compound: bool,
    has_context: bool,
    has_first: bool,
}
impl Store {
    pub fn clear(&mut self) {
        *self = Self::default();
    }
    pub fn stop_growth(&mut self) {
        self.stopped = true;
    }
    pub fn rule_count(&self) -> usize {
        self.entries.len()
    }
    pub fn has_first_letter(&self) -> bool {
        self.has_first
    }
    fn selector(&self, i: usize) -> &[u8] {
        let e = self.entries[i];
        &self.selectors[e.offset as usize..e.offset as usize + usize::from(e.len)]
    }
    fn lower_bound(&self, key: &Key<'_>) -> usize {
        self.entries.partition_point(|e| {
            key.compare(
                &self.selectors[e.offset as usize..e.offset as usize + usize::from(e.len)],
                false,
            ) == Ordering::Less
        })
    }
    fn exact(&self, key: &Key<'_>, i: usize) -> bool {
        i < self.entries.len() && key.compare(self.selector(i), false) == Ordering::Equal
    }
    fn intern(&mut self, s: Style, diagnostics: &Diagnostics) -> Result<u16, Error> {
        if let Some(i) = self.styles.iter().position(|v| *v == s) {
            return Ok(i as u16);
        }
        let needed = self.styles.len() + 1;
        grow(
            &mut self.styles,
            needed,
            16,
            MAX_STYLES,
            diagnostics,
            Diagnostic::StyleOom,
        )?;
        let i = self.styles.len();
        self.styles.push(s);
        Ok(i as u16)
    }
    fn insert(
        &mut self,
        key: &Key<'_>,
        style: Style,
        diagnostics: &Diagnostics,
    ) -> Result<bool, Error> {
        let pos = self.lower_bound(key);
        if self.exact(key, pos)
            && self.entries[pos].offset as usize + usize::from(self.entries[pos].len)
                == self.selectors.len()
        {
            let current = self.entries[pos].style;
            let mut merged = self.styles[usize::from(current)];
            merged.apply(&style);
            if self
                .entries
                .iter()
                .enumerate()
                .any(|(i, e)| i != pos && e.style == current)
            {
                let idx = self.intern(merged, diagnostics)?;
                self.entries[pos].style = idx;
            } else {
                self.styles[usize::from(current)] = merged;
            }
            return Ok(true);
        }
        let n = self.entries.len() + 1;
        grow(
            &mut self.entries,
            n,
            32,
            MAX_RULES,
            diagnostics,
            Diagnostic::IndexOom,
        )?;
        let n = self.selectors.len() + key.len;
        grow(
            &mut self.selectors,
            n,
            512,
            SELECTOR_CAP,
            diagnostics,
            Diagnostic::SelectorOom,
        )?;
        let idx = self.intern(style, diagnostics)?;
        let offset = self.selectors.len();
        self.selectors.extend(key.bytes());
        self.entries.insert(
            pos,
            Entry {
                offset: offset as u32,
                style: idx,
                len: key.len as u16,
            },
        );
        let stored = &self.selectors[offset..];
        let sep = stored.iter().position(|&c| c == 31);
        let subject = &stored[..sep.unwrap_or(stored.len())];
        self.has_context |= sep.is_some();
        self.has_id |= subject.contains(&b'#');
        self.has_compound |= subject.iter().filter(|&&c| c == b'.').count() >= 2;
        self.has_first |= subject.contains(&b':');
        Ok(false)
    }
    fn rule(&mut self, selectors: &[u8], style: Style, diagnostics: &Diagnostics) {
        if style.flags() == 0 {
            return;
        }
        for s in selectors
            .split(|&c| c == b',')
            .map(trim)
            .filter(|s| !s.is_empty())
        {
            if s.len() > MAX_SELECTOR_BYTES {
                diagnostics.emit(Diagnostic::SelectorLong, s.len(), MAX_SELECTOR_BYTES);
            }
            let Some(key) = selector::canonical(s) else {
                continue;
            };
            if self.stopped && !self.exact(&key, self.lower_bound(&key)) {
                continue;
            }
            if let Err(error) = self.insert(&key, style, diagnostics) {
                let event = match error {
                    Error::Limit => Diagnostic::StoreLimit,
                    Error::OutOfMemory => Diagnostic::StoreOom,
                    Error::Invalid => Diagnostic::StoreLimit,
                };
                diagnostics.emit(event, self.entries.len(), 0);
                self.stopped = true;
            }
        }
    }
    pub fn resolve(
        &self,
        tag: &[u8],
        classes: &[u8],
        id: &[u8],
        ancestors: &[Ancestor],
        first: bool,
    ) -> Style {
        let mut result = Style::default();
        if self.entries.is_empty() || (first && !self.has_first) {
            return result;
        }
        let mut cl: [&[u8]; 8] = [b""; 8];
        let mut count = 0;
        for c in tokens(classes).take(8) {
            cl[count] = c;
            count += 1;
        }
        cl[..count].sort_unstable_by(|a, b| selector::compare(a, b));
        let mut unique = 0;
        for i in 0..count {
            if unique == 0 || !cl[i].eq_ignore_ascii_case(cl[unique - 1]) {
                cl[unique] = cl[i];
                unique += 1;
            }
        }
        count = unique;
        let mut last: Option<Match> = None;
        loop {
            let mut matches = Matches::default();
            for with_id in 0..=u8::from(self.has_id && !id.is_empty()) {
                for with_tag in 0..=1 {
                    let mut base = Key::default();
                    if with_tag != 0 {
                        base.add(tag);
                    }
                    if with_id != 0 {
                        base.add(b"#");
                        base.add(id);
                    }
                    let spec = selector::specificity(with_id != 0, 0, with_tag != 0);
                    if with_tag != 0 || with_id != 0 {
                        let mut key = base;
                        if first {
                            key.add(FIRST_LETTER);
                        }
                        self.lookup(&mut key, spec, ancestors, &mut matches, last);
                    }
                    for c in &cl[..count] {
                        let mut key = base;
                        key.add(b".");
                        key.add(c);
                        if first {
                            key.add(FIRST_LETTER);
                        }
                        self.lookup(&mut key, spec + 256, ancestors, &mut matches, last);
                    }
                    if !self.has_compound {
                        continue;
                    }
                    for mask in 1u32..(1 << count.min(4)) {
                        let bits = mask.count_ones();
                        if bits < 2 {
                            continue;
                        }
                        let mut key = base;
                        for (i, c) in cl[..count.min(4)].iter().enumerate() {
                            if mask & (1 << i) != 0 {
                                key.add(b".");
                                key.add(c);
                            }
                        }
                        if first {
                            key.add(FIRST_LETTER);
                        }
                        self.lookup(&mut key, spec + (bits << 8), ancestors, &mut matches, last);
                    }
                }
            }
            for m in &matches.items[..matches.len] {
                result.apply(&self.styles[usize::from(self.entries[usize::from(m.index)].style)]);
            }
            if matches.len != 16 {
                break;
            }
            last = Some(matches.items[15]);
        }
        result
    }
    fn precedes(&self, a: Match, b: Match) -> bool {
        (a.specificity, self.entries[usize::from(a.index)].offset)
            < (b.specificity, self.entries[usize::from(b.index)].offset)
    }
    fn lookup(
        &self,
        key: &mut Key<'_>,
        spec: u32,
        ancestors: &[Ancestor],
        matches: &mut Matches,
        last: Option<Match>,
    ) {
        let mut i = self.lower_bound(key);
        while self.exact(key, i) {
            matches.add(
                self,
                Match {
                    specificity: spec,
                    index: i as u16,
                },
                last,
            );
            i += 1;
        }
        if !self.has_context || ancestors.is_empty() || !key.add(CONTEXT) {
            return;
        }
        i = self.lower_bound(key);
        while i < self.entries.len() && key.compare(self.selector(i), true) == Ordering::Equal {
            let context = &self.selector(i)[key.len..];
            if selector::ancestors_match(context, ancestors) {
                matches.add(
                    self,
                    Match {
                        specificity: spec + selector::context_specificity(context),
                        index: i as u16,
                    },
                    last,
                );
            }
            i += 1;
        }
        key.count -= 1;
        key.len -= 1;
    }
    /// Builds a source-order index in caller-owned, bounded scratch storage.
    pub fn cache_order(&self, order: &mut [u16]) -> Result<(), Error> {
        if order.len() != self.entries.len() {
            return Err(Error::Invalid);
        }
        for (i, slot) in order.iter_mut().enumerate() {
            *slot = i as u16;
        }
        // Iterative heapsort bounds time and stack without the general sort's flash cost.
        for root in (0..order.len() / 2).rev() {
            self.sift_cache_order(order, root);
        }
        for end in (1..order.len()).rev() {
            order.swap(0, end);
            self.sift_cache_order(&mut order[..end], 0);
        }
        Ok(())
    }
    fn sift_cache_order(&self, order: &mut [u16], mut root: usize) {
        while root < order.len() / 2 {
            let mut child = root * 2 + 1;
            let offset = |i: usize| self.entries[usize::from(order[i])].offset;
            if child + 1 < order.len() && offset(child) < offset(child + 1) {
                child += 1;
            }
            if offset(root) >= offset(child) {
                break;
            }
            order.swap(root, child);
            root = child;
        }
    }
    pub fn cache_record(&self, index: u16, out: &mut [u8]) -> Result<usize, Error> {
        let i = usize::from(index);
        let e = self.entries.get(i).ok_or(Error::Invalid)?;
        let len = usize::from(e.len);
        let size = 2 + len + WIRE_BYTES;
        if out.len() < size {
            return Err(Error::Invalid);
        }
        out[..2].copy_from_slice(&e.len.to_le_bytes());
        out[2..2 + len].copy_from_slice(self.selector(i));
        out[2 + len..size].copy_from_slice(&self.styles[usize::from(e.style)].0);
        Ok(size)
    }
    pub fn cache_begin(&mut self, count: usize) -> Result<(), Error> {
        self.cache_begin_with_diagnostics(count, &Diagnostics::default())
    }
    pub fn cache_begin_with_diagnostics(
        &mut self,
        count: usize,
        diagnostics: &Diagnostics,
    ) -> Result<(), Error> {
        self.clear();
        if count > MAX_RULES {
            diagnostics.emit(Diagnostic::CacheCount, count, MAX_RULES);
        }
        grow(
            &mut self.entries,
            count,
            32,
            MAX_RULES,
            diagnostics,
            Diagnostic::IndexOom,
        )
    }
    pub fn cache_insert(&mut self, key: &[u8], wire: &[u8]) -> Result<(), Error> {
        self.cache_insert_with_diagnostics(key, wire, &Diagnostics::default())
    }
    pub fn cache_insert_with_diagnostics(
        &mut self,
        key: &[u8],
        wire: &[u8],
        diagnostics: &Diagnostics,
    ) -> Result<(), Error> {
        if key.is_empty() || key.len() > MAX_CACHE_KEY_BYTES || wire.len() != WIRE_BYTES {
            return Err(Error::Invalid);
        }
        let mut style = Style::default();
        style.0.copy_from_slice(wire);
        if !style.valid() {
            return Err(Error::Invalid);
        }
        let mut pieces = Key::default();
        pieces.add(key);
        match self.insert(&pieces, style, diagnostics) {
            Ok(false) => Ok(()),
            Ok(true) => {
                diagnostics.emit(Diagnostic::CacheDuplicate, 0, 0);
                Err(Error::Invalid)
            }
            Err(e) => Err(e),
        }
    }
}
#[derive(Clone, Copy, Default)]
struct Match {
    specificity: u32,
    index: u16,
}
#[derive(Default)]
struct Matches {
    items: [Match; 16],
    len: usize,
}
impl Matches {
    fn add(&mut self, store: &Store, m: Match, last: Option<Match>) {
        if last.is_some_and(|l| !store.precedes(l, m)) {
            return;
        }
        let mut pos = 0;
        while pos < self.len && store.precedes(self.items[pos], m) {
            pos += 1;
        }
        if pos == 16 {
            return;
        }
        if self.len < 16 {
            self.len += 1;
        }
        for i in (pos + 1..self.len).rev() {
            self.items[i] = self.items[i - 1];
        }
        self.items[pos] = m;
    }
}

/// Scratch exists only while feeding one stylesheet; each token is capped at 1024 bytes.
#[derive(Default)]
pub struct Stream {
    selector: Vec<u8>,
    declaration: Vec<u8>,
    style: Style,
    comment: bool,
    slash: bool,
    star: bool,
    at_rule: bool,
    at_depth: usize,
    body_depth: usize,
    skip: bool,
    selector_truncated: bool,
    declaration_truncated: bool,
    truncated: bool,
}
impl Stream {
    pub fn prepare(&mut self) -> Result<(), Error> {
        self.selector
            .try_reserve_exact(1024)
            .map_err(|_| Error::OutOfMemory)?;
        self.declaration
            .try_reserve_exact(1024)
            .map_err(|_| Error::OutOfMemory)
    }
    pub fn feed(&mut self, store: &mut Store, bytes: &[u8]) {
        self.feed_with_diagnostics(store, bytes, &Diagnostics::default());
    }
    pub fn feed_with_diagnostics(
        &mut self,
        store: &mut Store,
        bytes: &[u8],
        diagnostics: &Diagnostics,
    ) {
        for &c in bytes {
            if self.comment {
                if self.star && c == b'/' {
                    self.comment = false;
                    self.star = false;
                    continue;
                }
                self.star = c == b'*';
                continue;
            }
            if self.slash {
                if c == b'*' {
                    self.comment = true;
                    self.slash = false;
                    self.star = false;
                    continue;
                }
                self.character(store, b'/', diagnostics);
                self.slash = false;
            }
            if c == b'/' {
                self.slash = true;
                continue;
            }
            self.character(store, c, diagnostics);
        }
    }
    pub fn finish(&mut self, store: &mut Store) -> ParseResult {
        self.finish_with_diagnostics(store, &Diagnostics::default())
    }
    pub fn finish_with_diagnostics(
        &mut self,
        store: &mut Store,
        diagnostics: &Diagnostics,
    ) -> ParseResult {
        if self.slash {
            self.character(store, b'/', diagnostics);
        }
        if self.truncated {
            diagnostics.emit(Diagnostic::InputTruncated, 0, 0);
        }
        if store.stopped
            || self.truncated
            || self.body_depth > 0
            || self.at_rule
            || self.comment
            || !self.selector.is_empty()
        {
            ParseResult::Partial
        } else {
            ParseResult::Complete
        }
    }
    fn character(&mut self, store: &mut Store, c: u8, diagnostics: &Diagnostics) {
        if self.at_rule {
            match c {
                b'{' => self.at_depth = self.at_depth.saturating_add(1),
                b'}' => {
                    self.at_depth = self.at_depth.saturating_sub(1);
                    if self.at_depth == 0 {
                        self.at_rule = false;
                    }
                }
                b';' if self.at_depth == 0 => self.at_rule = false,
                _ => {}
            }
            return;
        }
        if self.body_depth == 0 {
            if self.selector.is_empty() && whitespace(c) {
                return;
            }
            if c == b'@' && self.selector.is_empty() {
                self.at_rule = true;
                self.at_depth = 0;
                return;
            }
            if c == b'{' {
                self.body_depth = 1;
                self.style = Style::default();
                self.declaration.clear();
                self.skip = self.selector_truncated;
                return;
            }
            if self.selector.len() == 1024 {
                self.selector_truncated = true;
                self.truncated = true;
            } else {
                self.selector.push(c);
            }
            return;
        }
        if c == b'{' {
            self.body_depth = self.body_depth.saturating_add(1);
            return;
        }
        if c == b'}' {
            self.body_depth -= 1;
            if self.body_depth == 0 {
                if !self.skip && !self.declaration_truncated && !self.declaration.is_empty() {
                    self.style.declaration(&self.declaration);
                }
                if !self.skip {
                    store.rule(&self.selector, self.style, diagnostics);
                }
                self.selector.clear();
                self.declaration.clear();
                self.skip = false;
                self.selector_truncated = false;
                self.declaration_truncated = false;
            }
            return;
        }
        if self.body_depth > 1 || self.skip {
            return;
        }
        if c == b';' {
            if !self.declaration_truncated && !self.declaration.is_empty() {
                self.style.declaration(&self.declaration);
            }
            self.declaration.clear();
            self.declaration_truncated = false;
        } else if self.declaration.len() == 1024 {
            self.declaration_truncated = true;
            self.truncated = true;
        } else {
            self.declaration.push(c);
        }
    }
}

#[cfg(test)]
mod tests;
