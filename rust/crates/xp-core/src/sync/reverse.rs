//! Legacy streaming XPath reverse mapping, including its permissive HTML rules.
use super::xpath::invisible;
const DEPTH: usize = 16;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Step {
    pub tag: [u8; 12],
    pub index: i32,
}
fn find(s: &[u8], p: &[u8]) -> Option<usize> {
    s.windows(p.len()).position(|w| w == p)
}
fn rfind(s: &[u8], p: &[u8]) -> Option<usize> {
    s.windows(p.len()).rposition(|w| w == p)
}
fn number(s: &[u8]) -> Option<i32> {
    let mut n = 0i32;
    for b in s {
        if !b.is_ascii_digit() {
            return None;
        }
        n = n.wrapping_mul(10).wrapping_add(i32::from(b - b'0'));
    }
    Some(n)
}
fn z(s: &[u8]) -> &[u8] {
    &s[..s.iter().position(|&c| c == 0).unwrap_or(s.len())]
}
pub fn index(s: &[u8], prefix: &[u8], last: bool) -> i32 {
    let Some(at) = (if last {
        rfind(s, prefix)
    } else {
        find(s, prefix)
    })
    .map(|p| p + prefix.len()) else {
        return -1;
    };
    let Some(end) = s[at..].iter().position(|&c| c == b']').map(|n| at + n) else {
        return -1;
    };
    if at == end {
        return -1;
    }
    number(&s[at..end]).unwrap_or(-1)
}
pub fn char_offset(s: &[u8]) -> i32 {
    let at = if let Some(t) = rfind(s, b"text()") {
        s[t..].iter().position(|&c| c == b'.').map(|i| i + t)
    } else {
        s.iter().rposition(|&c| c == b'.')
    };
    at.and_then(|i| number(&s[i + 1..])).unwrap_or(0)
}
pub fn text_node(s: &[u8]) -> i32 {
    let v = index(s, b"text()[", true);
    if v > 0 {
        v
    } else {
        1
    }
}
fn content(s: &[u8]) -> Option<usize> {
    let at = find(s, b"/body/DocFragment[")? + 18;
    let end = s.get(at..)?.iter().position(|&c| c == b']')? + at;
    s.get(end + 1..)?.starts_with(b"/body/").then_some(end + 7)
}
pub fn body_text(s: &[u8]) -> bool {
    content(s).is_some_and(|p| s[p..].starts_with(b"text()"))
}
pub fn chapter_start(s: &[u8]) -> bool {
    if find(s, b"/p[").is_some() || find(s, b"/li[").is_some() {
        return false;
    }
    let Some(start) = find(s, b"/body/DocFragment[") else {
        return false;
    };
    let Some(end) = s[start + 18..]
        .iter()
        .position(|&b| b == b']')
        .map(|n| start + 18 + n)
    else {
        return false;
    };
    if end + 1 == s.len() {
        return true;
    }
    if s[end + 1] == b'.' {
        return end + 2 < s.len() && s[end + 2..].iter().all(|&b| b == b'0');
    }
    let Some(mut p) = find(s, b"]/body").map(|i| i + 6) else {
        return false;
    };
    if p == s.len() {
        return true;
    }
    if s[p] != b'/' {
        return false;
    }
    p += 1;
    if p == s.len() {
        return true;
    }
    let Some(dot) = s.iter().rposition(|&b| b == b'.') else {
        return false;
    };
    if dot <= p || dot + 1 >= s.len() {
        return false;
    }
    let terminal = rfind(&s[..dot + 1], b"/text()")
        .filter(|&i| i >= p)
        .unwrap_or(dot);
    if s[p..]
        .iter()
        .position(|&b| b == b'/')
        .is_some_and(|i| p + i < terminal)
    {
        return false;
    }
    s[dot + 1..].iter().all(|&b| b == b'0') && text_node(s) <= 1
}
pub fn steps(s: &[u8], out: &mut [Step; DEPTH]) -> usize {
    let Some(mut p) = content(s) else {
        return 0;
    };
    let end = if let Some(end) = rfind(s, b"/text()") {
        end
    } else {
        let Some(end) = s.iter().rposition(|&b| b == b'.') else {
            return 0;
        };
        if end <= p || end + 1 >= s.len() || !s[end + 1..].iter().all(u8::is_ascii_digit) {
            return 0;
        }
        end
    };
    if end <= p {
        return 0;
    }
    let mut count = 0;
    while p < end && count < DEPTH {
        let slash = s[p..]
            .iter()
            .position(|&b| b == b'/')
            .map(|n| n + p)
            .unwrap_or(s.len());
        let seg_end = slash.min(end);
        let bracket = s[p..]
            .iter()
            .position(|&b| b == b'[')
            .map(|n| n + p)
            .unwrap_or(s.len());
        let name_end = bracket.min(seg_end);
        let len = name_end - p;
        if len == 0 || len >= 12 {
            return 0;
        }
        out[count] = Step::default();
        out[count].tag[..len].copy_from_slice(&s[p..name_end]);
        if bracket < seg_end {
            let Some(close) = s[bracket + 1..]
                .iter()
                .position(|&b| b == b']')
                .map(|n| n + bracket + 1)
            else {
                return 0;
            };
            if close > seg_end {
                return 0;
            }
            let Some(n) = number(&s[bracket + 1..close]) else {
                return 0;
            };
            out[count].index = n;
        }
        count += 1;
        p = if slash < end { slash + 1 } else { end };
    }
    count
}
#[derive(Clone, Copy, Default)]
enum TagState {
    #[default]
    Idle,
    Name,
    Attrs,
}
#[derive(Clone, Copy, Default)]
enum AttrState {
    #[default]
    Find,
    Name,
    AfterName,
    BeforeValue,
    Double,
    Single,
}
pub struct Reverse {
    steps: [Step; DEPTH],
    count: usize,
    char_offset: i32,
    target_node: i32,
    relaxed: bool,
    body_text: bool,
    forward_target: usize,
    bytes: usize,
    forward_result: i32,
    forward_captured: bool,
    paragraph: i32,
    p_count: i32,
    li_count: i32,
    p_at_match: i32,
    li_at_match: i32,
    found_element: bool,
    done: bool,
    rev_chars: i32,
    p_depth: i32,
    current_node: i32,
    pub total: usize,
    pub target: usize,
    counters: [i32; DEPTH],
    inside_step: [bool; DEPTH],
    entered: [i32; DEPTH],
    matched: usize,
    depth: i32,
    body_depth: i32,
    inside_body: bool,
    hidden: u8,
    in_tag: bool,
    in_entity: bool,
    entity: [u8; 16],
    entity_len: usize,
    prev_cr: bool,
    tag_state: TagState,
    close_tag: bool,
    tag: [u8; 12],
    tag_len: usize,
    pub anchor: [u8; 64],
    anchor_len: usize,
    capturing: bool,
    attr_state: AttrState,
    attr_len: u8,
    is_id: bool,
    in_quote: bool,
    quote: u8,
}
impl Reverse {
    pub fn new(path: &[u8], forward_target: usize, relaxed: bool) -> Self {
        let mut st = [Step::default(); DEPTH];
        let count = steps(path, &mut st);
        Self {
            steps: st,
            count,
            char_offset: char_offset(path),
            target_node: text_node(path),
            relaxed,
            body_text: count == 0 && body_text(path),
            forward_target,
            bytes: 0,
            forward_result: 0,
            forward_captured: false,
            paragraph: index(path, b"/p[", true).max(0),
            p_count: 0,
            li_count: 0,
            p_at_match: 0,
            li_at_match: 0,
            found_element: false,
            done: false,
            rev_chars: 0,
            p_depth: -1,
            current_node: 0,
            total: 0,
            target: 0,
            counters: [0; DEPTH],
            inside_step: [false; DEPTH],
            entered: [-1; DEPTH],
            matched: 0,
            depth: 0,
            body_depth: -1,
            inside_body: false,
            hidden: 0,
            in_tag: false,
            in_entity: false,
            entity: [0; 16],
            entity_len: 0,
            prev_cr: false,
            tag_state: TagState::Idle,
            close_tag: false,
            tag: [0; 12],
            tag_len: 0,
            anchor: [0; 64],
            anchor_len: 0,
            capturing: false,
            attr_state: AttrState::Find,
            attr_len: 0,
            is_id: false,
            in_quote: false,
            quote: 0,
        }
    }
    pub fn found(&self) -> bool {
        self.done
    }
    pub fn paragraph_count(&self) -> i32 {
        if self.forward_captured {
            self.forward_result
        } else {
            self.p_count
        }
    }
    pub fn paragraph_at_match(&self) -> i32 {
        self.p_at_match
    }
    pub fn list_at_match(&self) -> i32 {
        self.li_at_match
    }
    pub fn anchor(&self) -> &[u8] {
        if self.anchor_len > 0 {
            z(&self.anchor)
        } else {
            &[]
        }
    }
    fn tag_is(&self, s: &[u8]) -> bool {
        z(&self.tag).eq_ignore_ascii_case(s)
    }
    fn reset_attr(&mut self) {
        self.attr_state = AttrState::Find;
        self.attr_len = 0;
        self.is_id = false;
    }
    fn begin_anchor(&mut self) {
        self.capturing = true;
        self.reset_attr();
    }
    fn end_anchor(&mut self) {
        if self.capturing {
            self.anchor_len = 0;
        }
        self.capturing = false;
        self.reset_attr();
    }
    fn attr(&mut self, c: u8) {
        let name = c.is_ascii_alphanumeric() || matches!(c, b'_' | b'-' | b':' | b'.');
        let space = matches!(c, b' ' | b'\t' | b'\n' | b'\r');
        match self.attr_state {
            AttrState::Find => {
                if name {
                    self.attr_state = AttrState::Name;
                    self.attr_len = 1;
                    self.is_id = c == b'i';
                }
            }
            AttrState::Name => {
                if name {
                    self.is_id = self.is_id && self.attr_len == 1 && c == b'd';
                    self.attr_len = self.attr_len.wrapping_add(1);
                } else {
                    self.is_id = self.is_id && self.attr_len == 2;
                    if space {
                        self.attr_state = AttrState::AfterName;
                    } else if c == b'=' {
                        self.attr_state = AttrState::BeforeValue;
                    } else {
                        self.reset_attr();
                    }
                }
            }
            AttrState::AfterName => {
                if !space {
                    if c == b'=' {
                        self.attr_state = AttrState::BeforeValue;
                    } else if name {
                        self.attr_state = AttrState::Name;
                        self.attr_len = 1;
                        self.is_id = c == b'i';
                    } else {
                        self.reset_attr();
                    }
                }
            }
            AttrState::BeforeValue => {
                if !space {
                    if c == b'"' || c == b'\'' {
                        if self.is_id {
                            self.anchor_len = 0;
                        }
                        self.attr_state = if c == b'"' {
                            AttrState::Double
                        } else {
                            AttrState::Single
                        };
                    } else {
                        self.reset_attr();
                    }
                }
            }
            AttrState::Double | AttrState::Single => {
                let closing = if matches!(self.attr_state, AttrState::Double) {
                    b'"'
                } else {
                    b'\''
                };
                if c == closing {
                    if self.is_id {
                        self.anchor[self.anchor_len] = 0;
                        self.capturing = false;
                    }
                    self.reset_attr();
                } else if self.is_id && self.anchor_len + 1 < 64 {
                    self.anchor[self.anchor_len] = c;
                    self.anchor_len += 1;
                }
            }
        }
    }
    fn visible(&mut self) {
        self.total = self.total.wrapping_add(1);
        if self.found_element && !self.done {
            let inside = if self.count > 0 {
                self.matched == self.count
                    && self.depth == self.entered[self.count - 1]
                    && self.current_node == self.target_node
            } else {
                self.p_depth >= 0
                    && self.depth == self.p_depth
                    && self.current_node == self.target_node
            };
            if inside {
                self.rev_chars = self.rev_chars.wrapping_add(1);
                if self.rev_chars >= self.char_offset {
                    self.target = self.total;
                    self.done = true;
                }
            }
        }
    }
    fn start_match(&mut self) {
        self.found_element = true;
        self.rev_chars = 0;
        self.current_node = 1;
        if self.char_offset <= 0 && self.target_node <= 1 {
            self.target = self.total;
            self.done = true;
        }
    }
    fn open(&mut self) {
        self.depth = self.depth.wrapping_add(1);
        if self.tag_is(b"body") {
            self.inside_body = true;
            self.body_depth = self.depth;
            if self.body_text {
                self.p_depth = self.depth;
                self.start_match();
            }
            return;
        }
        if !self.inside_body {
            return;
        }
        if self.hidden > 0 || invisible(z(&self.tag)) {
            self.hidden = self.hidden.wrapping_add(1);
            return;
        }
        if self.count == 0 {
            if self.tag_is(b"p") {
                self.p_count = self.p_count.wrapping_add(1);
                if !self.found_element && self.paragraph > 0 && self.p_count >= self.paragraph {
                    self.p_depth = self.depth;
                    self.start_match();
                }
            }
            return;
        }
        if self.found_element
            && self.matched == self.count
            && self.anchor_len == 0
            && self.tag_is(b"a")
        {
            self.begin_anchor();
        }
        if self.done {
            return;
        }
        if self.tag_is(b"p") {
            self.p_count = self.p_count.wrapping_add(1);
        }
        if self.tag_is(b"li") {
            self.li_count = self.li_count.wrapping_add(1);
        }
        if self.matched < self.count {
            let step = self.steps[self.matched];
            if self.tag_is(z(&step.tag)) {
                let correct = if self.matched == 0 {
                    self.relaxed || self.depth == self.body_depth + 1
                } else {
                    self.depth == self.entered[self.matched - 1] + 1
                };
                if !correct {
                    return;
                }
                self.counters[self.matched] = self.counters[self.matched].wrapping_add(1);
                if step.index == 0 || self.counters[self.matched] == step.index {
                    self.inside_step[self.matched] = true;
                    self.entered[self.matched] = self.depth;
                    self.matched += 1;
                    if self.matched == self.count {
                        self.begin_anchor();
                        self.p_at_match = self.p_count;
                        self.li_at_match = self.li_count;
                        self.anchor_len = 0;
                        self.start_match();
                    }
                }
            }
        }
    }
    fn next_node(&mut self) {
        self.current_node = self.current_node.wrapping_add(1);
        if self.current_node == self.target_node && self.char_offset <= 0 {
            self.target = self.total;
            self.done = true;
        }
    }
    fn close(&mut self) {
        if self.tag_is(b"body") {
            self.inside_body = false;
            self.depth = (self.depth - 1).max(0);
            return;
        }
        if !self.inside_body {
            self.depth = (self.depth - 1).max(0);
            return;
        }
        if self.hidden > 0 {
            self.hidden -= 1;
            self.depth = (self.depth - 1).max(0);
            return;
        }
        if self.count == 0
            && self.found_element
            && !self.done
            && self.p_depth >= 0
            && self.depth == self.p_depth + 1
        {
            self.next_node();
        }
        if self.count == 0
            && self.found_element
            && !self.done
            && self.p_depth >= 0
            && self.depth == self.p_depth
        {
            self.found_element = false;
            self.p_depth = -1;
        }
        if self.count > 0
            && self.matched == self.count
            && self.found_element
            && !self.done
            && self.depth == self.entered[self.count - 1] + 1
        {
            self.next_node();
        }
        if self.count > 0 && self.matched > 0 {
            let step = self.matched - 1;
            if self.inside_step[step] && self.depth == self.entered[step] {
                self.inside_step[step] = false;
                self.matched -= 1;
                if self.matched < self.count && self.found_element && !self.done {
                    self.found_element = false;
                }
                for i in self.matched + 1..self.count {
                    self.counters[i] = 0;
                    self.inside_step[i] = false;
                    self.entered[i] = -1;
                }
            }
        }
        self.depth = (self.depth - 1).max(0);
    }
    fn tag_byte(&mut self, c: u8) {
        match self.tag_state {
            TagState::Idle => {
                if c == b'/' {
                    self.close_tag = true;
                    self.tag_state = TagState::Name;
                } else if c != b'!' && c != b'?' {
                    self.close_tag = false;
                    self.tag[0] = c;
                    self.tag_len = 1;
                    self.tag_state = TagState::Name;
                }
            }
            TagState::Name => {
                if matches!(c, b'>' | b' ' | b'\t' | b'\n' | b'\r' | b'/') {
                    self.tag[self.tag_len] = 0;
                    if self.tag_len > 0 {
                        if self.close_tag {
                            self.close();
                        } else {
                            self.open();
                        }
                        if c == b'/' && !self.close_tag {
                            self.close();
                        }
                    }
                    self.tag_len = 0;
                    self.tag_state = if c == b'>' {
                        TagState::Idle
                    } else {
                        TagState::Attrs
                    };
                } else if self.tag_len + 1 < 12 {
                    self.tag[self.tag_len] = c;
                    self.tag_len += 1;
                }
            }
            TagState::Attrs => {
                if !self.in_quote {
                    if c == b'"' || c == b'\'' {
                        self.in_quote = true;
                        self.quote = c;
                    }
                } else if c == self.quote {
                    self.in_quote = false;
                    self.quote = 0;
                }
                if self.capturing {
                    self.attr(c);
                }
                if c == b'/' && !self.in_quote {
                    self.end_anchor();
                    self.close();
                }
            }
        }
    }
    fn literal_entity(&mut self) {
        for _ in 0..self.entity_len {
            self.visible();
        }
    }
    pub fn feed(
        &mut self,
        bytes: &[u8],
        entity_count: Option<unsafe extern "C" fn(*const u8, usize) -> i32>,
    ) {
        for &c in bytes {
            self.byte(c, entity_count);
        }
    }
    fn byte(&mut self, c: u8, entity_count: Option<unsafe extern "C" fn(*const u8, usize) -> i32>) {
        if !self.forward_captured && self.bytes >= self.forward_target {
            self.forward_result = self.p_count;
            self.forward_captured = true;
        }
        self.bytes = self.bytes.wrapping_add(1);
        if self.in_entity {
            if self.entity_len + 1 < 16 {
                self.entity[self.entity_len] = c;
                self.entity_len += 1;
            } else {
                self.literal_entity();
                self.in_entity = false;
                self.entity_len = 0;
            }
            if self.in_entity {
                if c == b';' {
                    if let Some(n) = entity_count.and_then(|f| {
                        let n = unsafe { f(self.entity.as_ptr(), self.entity_len) };
                        (n >= 0).then_some(n as usize)
                    }) {
                        for _ in 0..n {
                            self.visible();
                        }
                    } else if self.entity_len >= 3 && self.entity[1] == b'#' {
                        self.visible();
                    } else {
                        self.literal_entity();
                    }
                    self.in_entity = false;
                    self.entity_len = 0;
                } else if matches!(c, b'<' | b' ' | b'\t' | b'\n' | b'\r') {
                    self.literal_entity();
                    self.in_entity = false;
                    self.entity_len = 0;
                }
            }
            return;
        }
        let after_cr = self.prev_cr;
        self.prev_cr = false;
        if c == b'<' {
            self.in_tag = true;
            self.tag_state = TagState::Idle;
            self.tag_len = 0;
            self.close_tag = false;
            self.capturing = false;
            self.reset_attr();
            self.in_quote = false;
            self.quote = 0;
        } else if c == b'>' {
            if matches!(self.tag_state, TagState::Attrs) {
                self.end_anchor();
            }
            self.in_tag = false;
            self.in_quote = false;
            if matches!(self.tag_state, TagState::Name) && self.tag_len > 0 {
                self.tag[self.tag_len] = 0;
                if self.close_tag {
                    self.close();
                } else {
                    self.open();
                }
                self.tag_len = 0;
            }
            self.tag_state = TagState::Idle;
        } else if self.in_tag {
            self.tag_byte(c);
        } else if self.inside_body && self.hidden == 0 {
            if c == b'&' {
                self.in_entity = true;
                self.entity[0] = b'&';
                self.entity_len = 1;
            } else if !(c == b'\n' && after_cr) {
                if c & 0xc0 != 0x80 {
                    self.visible();
                }
                self.prev_cr = c == b'\r';
            }
        }
    }
}
