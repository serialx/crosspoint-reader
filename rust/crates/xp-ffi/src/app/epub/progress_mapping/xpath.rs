use super::alloc::{alloc::alloc, boxed::Box};
use super::{Chapter, Diagnostic, Source, Text};
use core::{alloc::Layout, ptr::NonNull};
use xp_core::sync::xpath::{Mode, XPath};

#[inline(never)]
fn parser(source: &impl Source, mode: Mode, spine: i32) -> Option<Box<XPath>> {
    let Some(raw) = NonNull::new(unsafe { alloc(Layout::new::<XPath>()) }.cast::<XPath>()) else {
        source.log(Diagnostic::ParserAllocation);
        return None;
    };
    unsafe {
        raw.as_ptr().write(XPath::new(mode, spine));
        Some(Box::from_raw(raw.as_ptr()))
    }
}
fn read(source: &impl Source, chapter: &impl Chapter, parser: &mut XPath) -> bool {
    let mut valid = true;
    if !chapter.read(&mut |bytes| {
        if valid {
            let code = parser.feed(bytes, false);
            if code != 0 {
                source.log(Diagnostic::Xml {
                    code,
                    final_input: false,
                });
                valid = false;
            }
        }
    }) {
        return false;
    }
    if valid {
        let code = parser.feed(&[], true);
        if code != 0 {
            source.log(Diagnostic::Xml {
                code,
                final_input: true,
            });
            valid = false;
        }
    }
    valid
}
fn body(spine: i32) -> Result<Text, ()> {
    let mut text = Text::default();
    let mut digits = [0; 10];
    let mut number = spine as u32 + 1;
    let mut start = digits.len();
    loop {
        start -= 1;
        digits[start] = b'0' + (number % 10) as u8;
        number /= 10;
        if number == 0 {
            break;
        }
    }
    text.assign_parts(&[b"/body/DocFragment[", &digits[start..], b"]/body"])
        .map_err(|_| ())?;
    Ok(text)
}
fn target(total: usize, ratio: f32) -> usize {
    let product = ratio.clamp(0.0, 1.0) * total as f32;
    let n = product as usize;
    let ceiling = if (n as f32) < product {
        n.saturating_add(1)
    } else {
        n
    };
    ceiling.min(total).max(1)
}
#[inline(never)]
pub(in super::super) fn resolve(
    source: &impl Source,
    spine: i32,
    mode: u32,
    value: u32,
    ratio: f32,
) -> Result<Text, ()> {
    if (mode == 0 && value as u16 == 0) || spine < 0 || spine >= source.spine_count() {
        return Ok(Text::default());
    }
    let Some(chapter) = source.chapter(spine) else {
        return Ok(Text::default());
    };
    let mut counter = None;
    let parse_mode = match mode {
        0 => Mode::Paragraph(value as u16),
        1 => Mode::Offset {
            target: value as usize,
            inclusive: false,
        },
        2 => {
            if ratio.is_nan() || ratio <= 0.0 {
                return body(spine);
            }
            let Some(mut count) = parser(source, Mode::Count, 0) else {
                return Ok(Text::default());
            };
            if !read(source, &chapter, &mut count) || count.count() == 0 {
                return Ok(Text::default());
            }
            let wanted = target(count.count(), ratio);
            counter = Some(count);
            Mode::Offset {
                target: wanted,
                inclusive: true,
            }
        }
        _ => return Ok(Text::default()),
    };
    let Some(mut resolver) = parser(source, parse_mode, spine) else {
        return Ok(Text::default());
    };
    if !read(source, &chapter, &mut resolver) {
        return Ok(Text::default());
    }
    let output = Text::copy(resolver.output()).map_err(|_| ())?;
    source.log(Diagnostic::Resolved {
        mode,
        spine,
        value,
        ratio,
        path: output.bytes(),
    });
    // The native progress resolver keeps both parsers alive through the second pass.
    drop(resolver);
    drop(counter);
    Ok(output)
}
