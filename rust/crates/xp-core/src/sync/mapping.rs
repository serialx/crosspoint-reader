use super::reverse::{self, Reverse, Step};
use core::ffi::c_void;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Position {
    pub spine: i32,
    pub page: i32,
    pub pages: i32,
    pub offset: u32,
    pub has_offset: u8,
    pub paragraph: u16,
    pub has_paragraph: u8,
    pub li: u16,
    pub has_li: u8,
    pub anchor: [u8; 64],
    pub resolved: u8,
    pub mapped: u8,
}
impl Default for Position {
    fn default() -> Self {
        Self {
            spine: 0,
            page: 0,
            pages: 0,
            offset: 0,
            has_offset: 0,
            paragraph: 0,
            has_paragraph: 0,
            li: 0,
            has_li: 0,
            anchor: [0; 64],
            resolved: 0,
            mapped: 0,
        }
    }
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Span {
    pub data: *const u8,
    pub len: usize,
}
impl Span {
    pub fn new(s: &[u8]) -> Self {
        Self {
            data: s.as_ptr(),
            len: s.len(),
        }
    }
}
#[repr(C)]
pub struct Event {
    pub id: u32,
    pub a: i32,
    pub b: i32,
    pub c: i32,
    pub d: i32,
    pub ratio: f32,
    pub target: usize,
    pub total: usize,
    pub text: Span,
}
#[repr(u32)]
pub enum Query {
    BookSize,
    SpineCount,
    SpineForSize,
    CumulativeSize,
    CachedPages,
    PageForOffset,
    PageForParagraph,
    PageForList,
    PageForAnchor,
    ItemSize,
}
#[repr(C)]
pub struct Services {
    pub ctx: *mut c_void,
    pub query: unsafe extern "C" fn(*mut c_void, u32, i32, usize, Span) -> i64,
    pub stream: unsafe extern "C" fn(*mut c_void, i32, *mut Reverse) -> u8,
    pub diagnose: Option<unsafe extern "C" fn(*mut c_void, *const Position, *const Event)>,
}
impl Services {
    fn q(&self, q: Query, spine: i32, arg: usize, text: &[u8]) -> i64 {
        unsafe { (self.query)(self.ctx, q as u32, spine, arg, Span::new(text)) }
    }
    fn size(&self, spine: i32) -> usize {
        if spine < 0 {
            0
        } else {
            self.q(Query::CumulativeSize, spine, 0, &[]) as usize
        }
    }
    #[allow(clippy::too_many_arguments)] // Fixed diagnostic payload; C++ owns formatting.
    #[inline(never)]
    fn log(
        &self,
        p: &Position,
        id: u32,
        a: i32,
        b: i32,
        c: i32,
        d: i32,
        ratio: f32,
        target: usize,
        total: usize,
        text: &[u8],
    ) {
        if let Some(f) = self.diagnose {
            let event = Event {
                id,
                a,
                b,
                c,
                d,
                ratio,
                target,
                total,
                text: Span::new(text),
            };
            unsafe {
                f(self.ctx, p, &event);
            }
        }
    }
    fn read(&self, spine: i32, r: &mut Reverse) -> bool {
        unsafe { (self.stream)(self.ctx, spine, r) != 0 }
    }
}
fn clamp_cpp(value: f32) -> f32 {
    if 1.0 < value {
        1.0
    } else if value < 0.0 {
        0.0
    } else if value.is_nan() {
        1.0
    } else {
        value
    }
}
fn scale(intra: f32, pages: i32) -> i32 {
    ((intra * (pages - 1) as f32 + 0.5) as i32)
        .min(pages - 1)
        .max(0)
}
fn tag(step: &Step) -> &[u8] {
    &step.tag[..step.tag.iter().position(|&b| b == 0).unwrap_or(12)]
}
fn apply(s: &Services, p: &mut Position, r: &Reverse, steps: &[Step], path: &[u8]) {
    p.offset = r.target.min(u32::MAX as usize) as u32;
    p.has_offset = 1;
    let para = r.paragraph_at_match();
    if para > 0 {
        p.paragraph = para as u16;
        p.has_paragraph = 1;
    }
    if let Some(last) = steps.last() {
        if tag(last).eq_ignore_ascii_case(b"li") {
            let li = r.list_at_match();
            if li > 0 {
                p.li = li as u16;
                p.has_li = 1;
            }
        }
    }
    let anchor = r.anchor();
    p.anchor[..anchor.len()].copy_from_slice(anchor);
    let ratio = if r.total > 0 {
        r.target as f32 / r.total as f32
    } else {
        0.0
    };
    if let Some(last) = steps.last() {
        s.log(
            p,
            1,
            last.index,
            reverse::text_node(path),
            reverse::char_offset(path),
            para,
            ratio,
            r.target,
            r.total,
            tag(last),
        );
    }
}
pub fn map(
    s: &Services,
    path: &[u8],
    percentage: f32,
    current: i32,
    current_pages: i32,
    fallback: i32,
) -> Position {
    let mut p = Position::default();
    let book = s.q(Query::BookSize, 0, 0, &[]) as usize;
    if book == 0 {
        return p;
    }
    let count = s.q(Query::SpineCount, 0, 0, &[]) as i32;
    let target_bytes = (book as f32 * clamp_cpp(percentage)) as usize;
    let fragment = reverse::index(path, b"/body/DocFragment[", false);
    let xp = reverse::index(path, b"/p[", true);
    let char_offset = reverse::char_offset(path);
    let node = reverse::text_node(path);
    let spine = if fragment >= 1 { fragment - 1 } else { -1 };
    let mut steps = [Step::default(); 16];
    let step_count = reverse::steps(path, &mut steps);
    if spine >= 0 && spine < count {
        p.spine = spine;
        p.resolved = 1;
    } else {
        let found = s.q(Query::SpineForSize, 0, target_bytes, &[]) as i32;
        if found >= 0 {
            p.spine = found;
        }
    }
    let previous = s.size(p.spine - 1);
    let size = s.size(p.spine).wrapping_sub(previous);
    if p.spine == current && current_pages > 0 {
        p.pages = current_pages;
    } else if current >= 0 && current < count && current_pages > 0 {
        let cs = s.size(current).wrapping_sub(s.size(current - 1));
        if cs > 0 {
            p.pages = (current_pages as f32 * size as f32 / cs as f32) as i32;
            p.pages = p.pages.max(1);
        }
    }
    if p.pages <= 0 {
        let cached = s.q(Query::CachedPages, p.spine, 0, &[]);
        p.pages = if cached >= 0 {
            cached as i32
        } else if fallback > 0 {
            fallback
        } else {
            1
        };
    }
    if step_count > 0 {
        for relaxed in [false, true] {
            let mut r = Reverse::new(path, usize::MAX, relaxed);
            if s.read(p.spine, &mut r) && r.found() {
                apply(s, &mut p, &r, &steps[..step_count], path);
                break;
            }
        }
    } else if reverse::body_text(path) || xp > 0 {
        let mut r = Reverse::new(path, usize::MAX, false);
        if s.read(p.spine, &mut r) && r.found() {
            p.offset = r.target.min(u32::MAX as usize) as u32;
            p.has_offset = 1;
            let ratio = if r.total > 0 {
                r.target as f32 / r.total as f32
            } else {
                0.0
            };
            s.log(
                &p,
                if reverse::body_text(path) { 2 } else { 3 },
                xp,
                node,
                char_offset,
                0,
                ratio,
                r.target,
                r.total,
                &[],
            );
        }
    }
    if p.has_offset == 0 && spine >= 0 && spine < count && reverse::chapter_start(path) {
        p.offset = 0;
        p.has_offset = 1;
        s.log(&p, 4, 0, 0, 0, 0, 0.0, 0, 0, path);
    }
    if p.has_offset != 0 {
        let image = step_count > 0
            && (tag(&steps[step_count - 1]).eq_ignore_ascii_case(b"img")
                || tag(&steps[step_count - 1]).eq_ignore_ascii_case(b"image"));
        let found = s.q(
            Query::PageForOffset,
            p.spine,
            usize::from(image),
            &p.offset.to_ne_bytes(),
        );
        if found >= 0 {
            p.page = found as i32;
            p.pages = p.pages.max(p.page + 1);
            p.mapped = 1;
            s.log(&p, 5, 0, 0, 0, 0, 0.0, 0, 0, &[]);
            return p;
        }
        s.log(&p, 6, 0, 0, 0, 0, 0.0, 0, 0, &[]);
    }
    let bytes = target_bytes.saturating_sub(previous);
    let intra = if size > 0 {
        clamp_cpp(bytes as f32 / size as f32)
    } else {
        0.0
    };
    p.page = scale(intra, p.pages);
    s.log(&p, 7, 0, 0, 0, 0, percentage, 0, 0, path);
    if p.has_li != 0 || p.anchor[0] != 0 || p.has_paragraph != 0 {
        let mut refined = false;
        if p.has_li != 0 {
            let page = s.q(Query::PageForList, p.spine, p.li as usize, &[]);
            s.log(
                &p,
                if page >= 0 { 8 } else { 9 },
                page as i32,
                0,
                0,
                0,
                0.0,
                0,
                0,
                &[],
            );
            if page >= 0 {
                p.page = page as i32;
                p.mapped = 1;
                refined = true;
            }
        }
        if !refined && p.anchor[0] != 0 {
            let n = p.anchor.iter().position(|&b| b == 0).unwrap_or(64);
            let page = s.q(Query::PageForAnchor, p.spine, 0, &p.anchor[..n]);
            s.log(
                &p,
                if page >= 0 { 10 } else { 11 },
                page as i32,
                0,
                0,
                0,
                0.0,
                0,
                0,
                &[],
            );
            if page >= 0 {
                p.page = page as i32;
                p.mapped = 1;
                refined = true;
            }
        }
        if !refined && p.has_paragraph != 0 {
            let page = s.q(Query::PageForParagraph, p.spine, p.paragraph as usize, &[]);
            let next = s.q(
                Query::PageForParagraph,
                p.spine,
                p.paragraph.wrapping_add(1) as usize,
                &[],
            );
            if page >= 0 {
                let mut chosen = p.page.max(page as i32);
                if next >= 0 && next - page > 1 && i64::from(chosen) >= next {
                    chosen = next as i32 - 1;
                }
                s.log(&p, 12, page as i32, next as i32, chosen, 0, 0.0, 0, 0, &[]);
                p.page = chosen;
                p.mapped = 1;
            } else {
                s.log(&p, 13, 0, 0, 0, 0, 0.0, 0, 0, &[]);
            }
        }
    }
    p
}
#[repr(C)]
pub struct Rich {
    pub pct: u32,
    pub spine: u16,
    pub page: u16,
    pub pages: u16,
    pub paragraph: u16,
    pub has_paragraph: u8,
}
pub fn rich(s: &Services, r: &Rich, path: &[u8], tried: bool) -> Option<Position> {
    let count = s.q(Query::SpineCount, 0, 0, &[]) as i32;
    let mut p = Position {
        spine: r.spine as i32,
        resolved: 1,
        ..Position::default()
    };
    if p.spine >= count {
        s.log(&p, 14, count, 0, 0, 0, 0.0, 0, 0, &[]);
        return None;
    }
    if !tried && !path.is_empty() {
        let content = map(s, path, r.pct as f32 / 1000000.0, -1, 0, 0);
        if content.has_offset != 0 {
            return Some(content);
        }
    }
    let cached = s.q(Query::CachedPages, p.spine, 0, &[]);
    if cached <= 0 {
        s.log(&p, 15, 0, 0, 0, 0, 0.0, 0, 0, &[]);
        return None;
    }
    p.pages = cached as i32;
    let remote = r.pages.max(1) as i32;
    if p.pages == remote {
        p.page = (r.page as i32).min(p.pages - 1);
        p.mapped = 1;
        s.log(&p, 16, 0, 0, 0, 0, 0.0, 0, 0, &[]);
        return Some(p);
    }
    if r.has_paragraph != 0 {
        let page = s.q(Query::PageForParagraph, p.spine, r.paragraph as usize, &[]);
        if page >= 0 {
            p.paragraph = r.paragraph;
            p.has_paragraph = 1;
            p.page = (page as i32).min(p.pages - 1);
            p.mapped = 1;
            s.log(&p, 17, 0, 0, 0, 0, 0.0, 0, 0, &[]);
            return Some(p);
        }
    }
    let intra = if remote > 1 {
        r.page as f32 / (remote - 1) as f32
    } else {
        0.0
    };
    p.page = scale(intra, p.pages);
    s.log(&p, 18, r.page as i32, remote, 0, 0, 0.0, 0, 0, &[]);
    Some(p)
}
pub fn compare(l: &Position, lp: f32, r: &Position, rp: f32) -> u8 {
    fn ordered(a: i64, b: i64) -> u8 {
        if a > b {
            0
        } else if a < b {
            2
        } else {
            1
        }
    }
    if l.resolved != 0 && r.resolved != 0 {
        if l.spine != r.spine {
            return ordered(l.spine as i64, r.spine as i64);
        }
        if l.mapped != 0 && r.mapped != 0 && l.page == r.page {
            return 1;
        }
        if l.has_offset != 0 && r.has_offset != 0 {
            return ordered(l.offset as i64, r.offset as i64);
        }
        if l.mapped != 0 && r.mapped != 0 {
            return ordered(l.page as i64, r.page as i64);
        }
    }
    if lp.is_finite() && rp.is_finite() {
        let d = lp - rp;
        if d.abs() <= 0.001 {
            1
        } else if d > 0.0 {
            0
        } else {
            2
        }
    } else {
        3
    }
}
#[repr(C)]
pub struct SaveServices {
    pub services: Services,
    pub progress: unsafe extern "C" fn(*mut c_void, i32, f32) -> f32,
    // Modes: paragraph, visible offset, progress. The callback stores the output string.
    pub xpath: unsafe extern "C" fn(*mut c_void, i32, u32, u32, f32) -> u8,
    pub append: unsafe extern "C" fn(*mut c_void, Span),
}
impl SaveServices {
    fn append(&self, data: &[u8]) {
        unsafe {
            (self.append)(self.services.ctx, Span::new(data));
        }
    }
    fn number(&self, mut n: u32) {
        let mut digits = [0; 10];
        let mut at = 10;
        loop {
            at -= 1;
            digits[at] = b'0' + (n % 10) as u8;
            n /= 10;
            if n == 0 {
                break;
            }
        }
        self.append(&digits[at..]);
    }
    fn xpath(&self, spine: i32, mode: u32, n: u32, progress: f32) -> bool {
        unsafe { (self.xpath)(self.services.ctx, spine, mode, n, progress) != 0 }
    }
}
pub fn save(s: &SaveServices, p: &Position) -> f32 {
    let intra = if p.pages > 1 {
        p.page as f32 / (p.pages - 1) as f32
    } else {
        0.0
    };
    let percentage = unsafe { (s.progress)(s.services.ctx, p.spine, intra) };
    if p.has_offset != 0 && s.xpath(p.spine, 1, p.offset, 0.0) {
        return percentage;
    }
    if p.has_paragraph != 0 && p.paragraph > 0 && s.xpath(p.spine, 0, p.paragraph as u32, 0.0) {
        return percentage;
    }
    if s.xpath(p.spine, 2, 0, intra) {
        return percentage;
    }
    s.append(b"/body/DocFragment[");
    if p.spine < -1 {
        s.append(b"-");
        s.number(p.spine.wrapping_add(1).unsigned_abs());
    } else {
        s.number(p.spine.wrapping_add(1) as u32);
    }
    s.append(b"]/body");
    if intra <= 0.0 {
        return percentage;
    }
    let size = s.services.q(Query::ItemSize, p.spine, 0, &[]);
    if size <= 0 {
        return percentage;
    }
    let mut r = Reverse::new(
        &[],
        (size as f32 * if 1.0 < intra { 1.0 } else { intra }) as usize,
        false,
    );
    if s.services.read(p.spine, &mut r) && r.paragraph_count() > 0 {
        s.append(b"/p[");
        s.number(r.paragraph_count() as u32);
        s.append(b"]");
    }
    percentage
}
