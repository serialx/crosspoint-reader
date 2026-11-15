use super::style::{eq, tokens, whitespace};
use core::cmp::Ordering;
pub const FIRST_LETTER: &[u8] = b"::first-letter";
pub const CONTEXT: &[u8] = b"\x1f";
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Ancestor {
    pub tag_hash: u32,
    pub id_hash: u32,
    pub class_hashes: [u32; 4],
    pub class_count: u8,
}
pub fn hash(bytes: &[u8]) -> u32 {
    let mut h = 2166136261u32;
    for c in bytes {
        h ^= u32::from(c.to_ascii_lowercase());
        h = h.wrapping_mul(16777619);
    }
    if h == 0 {
        1
    } else {
        h
    }
}
pub fn ancestor(tag: &[u8], classes: &[u8], id: &[u8]) -> Ancestor {
    let mut a = Ancestor {
        tag_hash: hash(tag),
        id_hash: if id.is_empty() { 0 } else { hash(id) },
        ..Ancestor::default()
    };
    for (i, c) in tokens(classes).take(4).enumerate() {
        a.class_hashes[i] = hash(c);
        a.class_count += 1;
    }
    a
}
pub fn compare(a: &[u8], b: &[u8]) -> Ordering {
    a.iter()
        .map(u8::to_ascii_lowercase)
        .cmp(b.iter().map(u8::to_ascii_lowercase))
}
fn ident(c: u8) -> bool {
    c.is_ascii_alphanumeric() || c == b'-' || c == b'_' || c >= 128
}
#[derive(Default)]
pub struct Compound<'a> {
    pub tag: &'a [u8],
    pub id: &'a [u8],
    pub classes: [&'a [u8]; 4],
    pub count: usize,
}
impl<'a> Compound<'a> {
    pub fn parse(text: &'a [u8]) -> Option<Self> {
        if text.is_empty() {
            return None;
        }
        let mut out = Self::default();
        let mut i = 0;
        if text[0] == b'*' {
            i = 1;
        } else {
            while i < text.len() && ident(text[i]) {
                i += 1;
            }
            out.tag = &text[..i];
        }
        while i < text.len() {
            let marker = text[i];
            i += 1;
            let start = i;
            while i < text.len() && ident(text[i]) {
                i += 1;
            }
            let name = &text[start..i];
            if name.is_empty() {
                return None;
            }
            if marker == b'#' && out.id.is_empty() {
                out.id = name;
            } else if marker == b'.' && out.count < 4 {
                out.classes[out.count] = name;
                out.count += 1;
            } else {
                return None;
            }
        }
        Some(out)
    }
    pub fn empty(&self) -> bool {
        self.tag.is_empty() && self.id.is_empty() && self.count == 0
    }
    fn matches(&self, a: &Ancestor) -> bool {
        if !self.tag.is_empty() && hash(self.tag) != a.tag_hash {
            return false;
        }
        if !self.id.is_empty() && hash(self.id) != a.id_hash {
            return false;
        }
        self.classes[..self.count]
            .iter()
            .all(|c| a.class_hashes[..usize::from(a.class_count.min(4))].contains(&hash(c)))
    }
    pub fn specificity(&self) -> u32 {
        specificity(!self.id.is_empty(), self.count, !self.tag.is_empty())
    }
}
pub fn specificity(id: bool, classes: usize, tag: bool) -> u32 {
    (u32::from(id) << 16) | ((classes as u32) << 8) | u32::from(tag)
}
pub fn pop_compound<'a>(text: &mut &'a [u8]) -> &'a [u8] {
    let mut start = text.len();
    while start > 0 && !whitespace(text[start - 1]) && text[start - 1] != b'>' {
        start -= 1;
    }
    let c = &text[start..];
    *text = &text[..start];
    c
}
pub fn pop_combinator(text: &mut &[u8]) -> bool {
    let mut child = false;
    while let Some(&c) = text.last() {
        if !whitespace(c) && c != b'>' {
            break;
        }
        child |= c == b'>';
        *text = &text[..text.len() - 1];
    }
    child
}
fn child_chain(context: &mut &[u8], ancestors: &[Ancestor], candidates: &mut usize) -> bool {
    loop {
        let Some(c) = Compound::parse(pop_compound(context)) else {
            return false;
        };
        if *candidates == 0 || !c.matches(&ancestors[*candidates - 1]) {
            return false;
        }
        *candidates -= 1;
        if !pop_combinator(context) || context.is_empty() {
            return true;
        }
    }
}
pub fn ancestors_match(mut context: &[u8], ancestors: &[Ancestor]) -> bool {
    let mut candidates = ancestors.len();
    let mut child = pop_combinator(&mut context);
    while !context.is_empty() {
        let mut rest = context;
        let mut count = candidates;
        if child_chain(&mut rest, ancestors, &mut count) {
            context = rest;
            candidates = count;
            child = false;
        } else {
            if child || candidates == 0 {
                return false;
            }
            candidates -= 1;
        }
    }
    true
}
pub fn context_specificity(mut context: &[u8]) -> u32 {
    let mut s = 0;
    loop {
        pop_combinator(&mut context);
        if context.is_empty() {
            return s;
        }
        let Some(c) = Compound::parse(pop_compound(&mut context)) else {
            return s;
        };
        s += c.specificity();
    }
}
#[derive(Clone, Copy)]
pub struct Key<'a> {
    pub pieces: [&'a [u8]; 14],
    pub count: usize,
    pub len: usize,
}
impl Default for Key<'_> {
    fn default() -> Self {
        Self {
            pieces: [b""; 14],
            count: 0,
            len: 0,
        }
    }
}
impl<'a> Key<'a> {
    pub fn add(&mut self, p: &'a [u8]) -> bool {
        if p.is_empty() {
            return true;
        }
        if self.count == 14 {
            return false;
        }
        self.pieces[self.count] = p;
        self.count += 1;
        self.len += p.len();
        true
    }
    pub fn bytes(&self) -> impl Iterator<Item = u8> + '_ {
        self.pieces[..self.count]
            .iter()
            .flat_map(|p| p.iter().map(u8::to_ascii_lowercase))
    }
    pub fn compare(&self, stored: &[u8], prefix: bool) -> Ordering {
        let mut i = 0;
        for b in self.bytes() {
            if i == stored.len() {
                return Ordering::Less;
            }
            match stored[i].cmp(&b) {
                Ordering::Equal => {}
                o => return o,
            }
            i += 1;
        }
        if prefix || i == stored.len() {
            Ordering::Equal
        } else {
            Ordering::Greater
        }
    }
}
pub fn canonical(selector: &[u8]) -> Option<Key<'_>> {
    if selector.len() > super::MAX_SELECTOR_BYTES {
        return None;
    }
    let mut s = selector;
    let mut first = false;
    for suffix in [FIRST_LETTER, &FIRST_LETTER[1..]] {
        if !first && s.len() > suffix.len() && eq(&s[s.len() - suffix.len()..], suffix) {
            s = &s[..s.len() - suffix.len()];
            first = true;
        }
    }
    if s.iter().any(|c| b"+~[:".contains(c)) {
        return None;
    }
    let mut context = s;
    let mut subject = Compound::parse(pop_compound(&mut context))?;
    if subject.empty() {
        return None;
    }
    let mut rest = context;
    let mut has_ancestor = false;
    loop {
        pop_combinator(&mut rest);
        if rest.is_empty() {
            break;
        }
        Compound::parse(pop_compound(&mut rest))?;
        has_ancestor = true;
    }
    if !context.is_empty() && !has_ancestor {
        return None;
    }
    subject.classes[..subject.count].sort_unstable_by(|a, b| compare(a, b));
    let mut key = Key::default();
    key.add(subject.tag);
    if !subject.id.is_empty() {
        key.add(b"#");
        key.add(subject.id);
    }
    for c in &subject.classes[..subject.count] {
        key.add(b".");
        key.add(c);
    }
    if first {
        key.add(FIRST_LETTER);
    }
    if has_ancestor {
        key.add(CONTEXT);
        key.add(context);
    }
    Some(key)
}
