//! Pinned MiniBidi rules, including its simplified isolate and bracket handling.
//! Derived from Ahmad Khalifa / Thomas Wolff's MIT-licensed MiniBidi.
use super::*;

pub(super) fn reorder(line: &mut [Character], auto: bool, mut paragraph: u8, s: &mut Scratch) {
    let count = line.len();
    if count == 0 {
        return;
    }
    let mut isolate_level = 0;
    let mut resolved = None;
    let mut has_rtl = false;
    for ch in line.iter() {
        let t = class(ch.wc);
        match t {
            LRI | RLI | FSI => {
                has_rtl = true;
                isolate_level += 1;
            }
            PDI => {
                has_rtl = true;
                if isolate_level > 0 {
                    isolate_level -= 1;
                }
            }
            _ if isolate_level == 0 => match t {
                R | AL => {
                    has_rtl = true;
                    if resolved.is_none() {
                        resolved = Some(1);
                    }
                    break;
                }
                RLE | LRE | RLO | LRO | PDF => {
                    has_rtl = true;
                    if resolved.is_some() {
                        break;
                    }
                }
                L => {
                    if resolved.is_none() {
                        resolved = Some(0);
                    }
                }
                AN => has_rtl = true,
                _ => {}
            },
            _ => {}
        }
    }
    if auto {
        paragraph = resolved.unwrap_or(paragraph);
    }
    if !has_rtl && paragraph == 0 {
        return;
    }
    let mut embedding = paragraph;
    let mut override_type = ON;
    let mut isolate = 0;
    isolate_level = 0;
    let mut depth = 1;
    s.stack[0] = [embedding, override_type, isolate];
    for i in 0..count {
        let mut t = class(line[i].wc);
        s.levels[i] = embedding;
        if t == FSI {
            let mut level = 0;
            t = LRI;
            for ch in &line[i + 1..] {
                match class(ch.wc) {
                    FSI | RLI | LRI => level += 1,
                    PDI => {
                        if level > 0 {
                            level -= 1;
                        } else {
                            break;
                        }
                    }
                    R | AL => {
                        t = RLI;
                        break;
                    }
                    L => break,
                    _ => {}
                }
            }
        }
        match t {
            RLE | LRE | RLO | LRO | RLI | LRI => {
                let original = t;
                if matches!(original, RLI | LRI) && override_type != ON {
                    t = override_type;
                }
                // C narrows each computed embedding level to unsigned char.
                embedding = if matches!(original, RLE | RLO | RLI) {
                    embedding.wrapping_add(1) | 1
                } else {
                    embedding.wrapping_add(2) & !1
                };
                override_type = match original {
                    RLO => R,
                    LRO => L,
                    _ => ON,
                };
                isolate = u8::from(matches!(original, RLI | LRI));
                isolate_level += i32::from(isolate);
                if depth < s.stack.len() {
                    s.stack[depth] = [embedding, override_type, isolate];
                    depth += 1;
                }
            }
            PDF => {
                if isolate == 0 && depth >= 2 {
                    depth -= 1;
                    [embedding, override_type, isolate] = s.stack[depth - 1];
                }
                s.levels[i] = embedding;
            }
            PDI => {
                if isolate_level > 0 {
                    while isolate == 0 && depth > 0 {
                        pop(
                            s,
                            &mut depth,
                            &mut embedding,
                            &mut override_type,
                            &mut isolate,
                        );
                    }
                    pop(
                        s,
                        &mut depth,
                        &mut embedding,
                        &mut override_type,
                        &mut isolate,
                    );
                    isolate_level -= 1;
                }
                if override_type != ON {
                    t = override_type;
                }
                s.levels[i] = embedding;
            }
            _ => {
                if override_type != ON {
                    t = override_type;
                }
            }
        }
        s.types[i] = t;
    }
    let types = &mut s.types[..count];
    let levels = &mut s.levels[..count];
    let skip = &mut s.skip[..count];
    for i in 0..count {
        skip[i] = u8::from(matches!(types[i], RLE | LRE | RLO | LRO | PDF | BN));
        if skip[i] != 0 {
            types[i] = NSM;
        }
    }
    let base = if paragraph & 1 != 0 { R } else { L };
    if types[0] == NSM {
        types[0] = base;
    }
    for i in 1..count {
        if types[i] == NSM {
            types[i] = if matches!(types[i - 1], LRI | RLI | FSI | PDI) {
                ON
            } else {
                types[i - 1]
            };
        }
    }
    // W2 precedes W3: AL must remain visible to the backward scan.
    for i in 0..count {
        if types[i] == EN {
            for j in (0..i).rev() {
                if types[j] == AL {
                    types[i] = AN;
                    break;
                }
                if matches!(types[j], R | L) {
                    break;
                }
            }
        }
    }
    for t in types.iter_mut() {
        if *t == AL {
            *t = R;
        }
    }
    for i in 1..count.saturating_sub(1) {
        if !matches!(types[i], ES | CS) {
            continue;
        }
        let prev = (0..i).rev().find(|&j| skip[j] == 0);
        let next = (i + 1..count).find(|&j| skip[j] == 0);
        if let (Some(p), Some(n)) = (prev, next) {
            if types[p] == EN && types[n] == EN {
                types[i] = EN;
            }
            if types[i] == CS && types[p] == AN && types[n] == AN {
                types[i] = AN;
            }
        }
    }
    for i in 0..count {
        if skip[i] != 0 || types[i] != ET {
            continue;
        }
        for j in i..count {
            if skip[j] != 0 || types[j] == ET {
                continue;
            }
            if types[j] == EN {
                types[i] = EN;
            }
            break;
        }
    }
    for i in (0..count).rev() {
        if skip[i] != 0 || types[i] != ET {
            continue;
        }
        for j in (0..=i).rev() {
            if skip[j] != 0 || types[j] == ET {
                continue;
            }
            if types[j] == EN {
                types[i] = EN;
            }
            break;
        }
    }
    let mut last = base;
    for i in 0..count {
        if matches!(types[i], ES | ET | CS) {
            types[i] = ON;
        }
        if skip[i] != 0 {
            continue;
        }
        if matches!(types[i], L | R) {
            last = types[i];
        }
        if types[i] == EN && last == L {
            types[i] = L;
        }
    }
    // N0: retain MiniBidi's paragraph-based bracket resolution.
    let opposite = if base == L { R } else { L };
    let mut top = 0;
    for i in 0..count {
        if skip[i] != 0 {
            continue;
        }
        let bc = bracket(line[i].wc);
        if bc == 0 {
            continue;
        }
        if bc == line[i].wc {
            if top < s.openers.len() {
                s.openers[top] = [bc, i as u32];
                top += 1;
            }
            continue;
        }
        let Some(k) = (0..top).rev().find(|&k| s.openers[k][0] == bc) else {
            continue;
        };
        let open = s.openers[k][1] as usize;
        top = k;
        let mut found_base = false;
        let mut found_opposite = false;
        for m in open + 1..i {
            if skip[m] != 0 {
                continue;
            }
            let t = strong(types[m]);
            if matches!(t, R | L) {
                if t == base {
                    found_base = true;
                } else {
                    found_opposite = true;
                }
            }
        }
        let dir = if found_base {
            base
        } else if found_opposite {
            let ctx = (0..open)
                .rev()
                .filter(|&m| skip[m] == 0)
                .map(|m| strong(types[m]))
                .find(|t| matches!(*t, R | L))
                .unwrap_or(base);
            if ctx == opposite {
                opposite
            } else {
                base
            }
        } else {
            continue;
        };
        types[open] = dir;
        types[i] = dir;
        for t in &mut types[open + 1..i] {
            if neutral(*t) {
                *t = dir;
            }
        }
    }
    let mut i = 0;
    while i < count {
        if skip[i] != 0 || !neutral(types[i]) {
            i += 1;
            continue;
        }
        let mut end = i;
        while end + 1 < count && (skip[end + 1] != 0 || neutral(types[end + 1])) {
            end += 1;
        }
        let prev = (0..i)
            .rev()
            .filter(|&j| skip[j] == 0)
            .map(|j| strong(types[j]))
            .find(|t| matches!(*t, R | L))
            .unwrap_or(base);
        let next = (end + 1..count)
            .filter(|&j| skip[j] == 0)
            .map(|j| strong(types[j]))
            .find(|t| matches!(*t, R | L))
            .unwrap_or(base);
        if prev == next {
            types[i..=end].fill(prev);
        }
        i = end + 1;
    }
    for i in 0..count {
        if neutral(types[i]) {
            types[i] = if levels[i] & 1 != 0 { R } else { L };
        }
        if skip[i] != 0 {
            continue;
        }
        let increment = if levels[i] & 1 == 0 {
            match types[i] {
                R => 1,
                AN | EN => 2,
                _ => 0,
            }
        } else {
            u8::from(matches!(types[i], L | EN | AN))
        };
        levels[i] = levels[i].wrapping_add(increment);
    }
    for i in (0..count).rev() {
        if skip[i] != 0 {
            continue;
        }
        if matches!(types[i], WS | S | B) {
            levels[i] = paragraph;
        } else {
            break;
        }
    }
    for i in 0..count {
        if types[i] != S {
            continue;
        }
        levels[i] = paragraph;
        for j in (0..i).rev() {
            if skip[j] != 0 {
                continue;
            }
            if matches!(types[j], WS | BN) {
                levels[j] = paragraph;
            } else {
                break;
            }
        }
    }
    let max = levels
        .iter()
        .copied()
        .max()
        .unwrap_or(paragraph)
        .max(paragraph);
    let min = levels
        .iter()
        .copied()
        .filter(|l| l & 1 != 0)
        .min()
        .unwrap_or(255);
    for level in (min..=max).rev() {
        let mut i = 0;
        while i < count {
            while i < count && levels[i] < level {
                i += 1;
            }
            let start = i;
            while i < count && levels[i] >= level {
                i += 1;
            }
            line[start..i].reverse();
        }
    }
    // The C implementation deliberately keeps levels in their original slots.
    for i in 0..count {
        if levels[i] & 1 != 0 {
            line[i].wc = mirror(line[i].wc);
        }
    }
}

fn pop(s: &Scratch, depth: &mut usize, embedding: &mut u8, over: &mut u8, isolate: &mut u8) {
    *depth = depth.saturating_sub(1);
    [*embedding, *over, *isolate] = if *depth > 0 {
        s.stack[*depth - 1]
    } else {
        [0, ON, 0]
    };
}
fn neutral(t: u8) -> bool {
    matches!(t, B | S | WS | ON | FSI | LRI | RLI | PDI)
}
fn strong(t: u8) -> u8 {
    if matches!(t, EN | AN | AL) {
        R
    } else {
        t
    }
}
