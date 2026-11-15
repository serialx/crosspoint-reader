use super::{decimal::Decimal, Diagnostic};
use core::fmt::{self, Write};

struct Message {
    bytes: [u8; 256],
    length: usize,
}
impl Message {
    fn append(&mut self, bytes: &[u8]) {
        let bytes = &bytes[..bytes.iter().position(|b| *b == 0).unwrap_or(bytes.len())];
        let count = bytes.len().min(self.bytes.len() - self.length);
        self.bytes[self.length..self.length + count].copy_from_slice(&bytes[..count]);
        self.length += count;
    }
}
impl Write for Message {
    fn write_str(&mut self, text: &str) -> fmt::Result {
        self.append(text.as_bytes());
        Ok(())
    }
}
#[inline(never)]
pub(in super::super) fn emit(
    diagnostic: Diagnostic<'_>,
    xml_error: fn(u32) -> &'static str,
    mut sink: impl FnMut(u8, &[u8], &[u8]),
) {
    let mut out = Message {
        bytes: [0; 256],
        length: 0,
    };
    let mut severity = 0;
    let origin: &[u8] = match diagnostic {
        Diagnostic::ParserAllocation => {
            severity = 1;
            out.append(b"Failed to create XML parser");
            b"KOX"
        }
        Diagnostic::Xml { code, final_input } => {
            severity = 1;
            if final_input {
                out.append(b"Final ");
            }
            out.append(b"XML parse error: ");
            out.append(xml_error(code).as_bytes());
            b"KOX"
        }
        Diagnostic::Resolved {
            mode,
            spine,
            value,
            ratio,
            path,
        } => {
            match (mode, path.is_empty()) {
                (0, false) => {
                    let _ = write!(out, "Resolved paragraph {value} in spine {spine} -> ");
                }
                (0, true) => {
                    let _ = write!(out, "Paragraph {value} not found in spine {spine}");
                }
                (1, false) => {
                    let _ = write!(out, "Resolved visible offset {value} in spine {spine} -> ");
                }
                (1, true) => {
                    let _ = write!(out, "Visible offset {value} not found in spine {spine}");
                }
                (2, false) => {
                    let _ = write!(
                        out,
                        "Resolved progress {} in spine {spine} -> ",
                        Decimal::<3>(ratio)
                    );
                }
                (2, true) => {
                    let _ = write!(
                        out,
                        "Could not resolve progress {} in spine {spine}",
                        Decimal::<3>(ratio)
                    );
                }
                _ => return,
            }
            out.append(path);
            b"KOX"
        }
        Diagnostic::Saved {
            position: p,
            percentage,
            path,
        } => {
            let _ = write!(
                out,
                "-> Progress: spine={} page={}/{} {}% ",
                p.spine,
                p.page,
                p.pages,
                Decimal::<2>(percentage * 100.0)
            );
            out.append(path);
            b"PM"
        }
        Diagnostic::Map {
            position: p,
            event: e,
            text,
        } => {
            let anchor = &p.anchor[..p
                .anchor
                .iter()
                .position(|b| *b == 0)
                .unwrap_or(p.anchor.len())];
            match e.id {
                1 => {
                    out.append(b"XPath ancestry(");
                    out.append(text);
                    let _ = write!(
                        out,
                        "[{}])/text()[{}]+{} -> {}% (target={} total={} p~{} li~{} anchor=",
                        e.a,
                        e.b,
                        e.c,
                        Decimal::<1>(e.ratio * 100.0),
                        e.target,
                        e.total,
                        e.d,
                        if p.has_li != 0 { p.li } else { 0 }
                    );
                    out.append(if anchor.is_empty() { b"none" } else { anchor });
                    out.append(b")");
                }
                2 => {
                    let _ = write!(
                        out,
                        "XPath body/text()[{}]+{} -> offset={}",
                        e.b, e.c, p.offset
                    );
                }
                3 => {
                    let _ = write!(
                        out,
                        "XPath p[{}]/text()[{}]+{} -> {}% (target={} total={})",
                        e.a,
                        e.b,
                        e.c,
                        Decimal::<1>(e.ratio * 100.0),
                        e.target,
                        e.total
                    );
                }
                4 => {
                    out.append(b"Chapter-start XPath ");
                    out.append(text);
                    let _ = write!(out, " -> spine={} page start", p.spine);
                }
                5 => {
                    let _ = write!(
                        out,
                        "XPath content offset {} -> spine={} page={}/{}",
                        p.offset, p.spine, p.page, p.pages
                    );
                }
                6 => {
                    let _ = write!(
                        out,
                        "No page-offset LUT for spine={} offset={}; using percentage fallback",
                        p.spine, p.offset
                    );
                }
                7 => {
                    let _ = write!(out, "<- Progress: {}% ", Decimal::<2>(e.ratio * 100.0));
                    out.append(text);
                    let _ = write!(out, " -> spine={} page={}/{}", p.spine, p.page, p.pages);
                }
                8 => {
                    let _ = write!(out, "Li index {} -> page {} (was {})", p.li, e.a, p.page);
                }
                9 => {
                    let _ = write!(out, "Li index {} not found in section LUT", p.li);
                }
                10 | 11 => {
                    out.append(b"Anchor '");
                    out.append(anchor);
                    if e.id == 10 {
                        let _ = write!(out, "' -> page {} (was {})", e.a, p.page);
                    } else {
                        out.append(b"' not found in section cache");
                    }
                }
                12 => {
                    let _ = write!(
                        out,
                        "Paragraph {} -> LUT page {}, nextPara page ",
                        p.paragraph, e.a
                    );
                    if e.b >= 0 {
                        let _ = write!(out, "{}", e.b);
                    } else {
                        out.append(b"none");
                    }
                    let _ = write!(out, ", intra page {}, using {}", p.page, e.c);
                }
                13 => {
                    let _ = write!(out, "Paragraph {} not found in section LUT", p.paragraph);
                }
                _ => return,
            }
            b"PM"
        }
    };
    sink(severity, origin, &out.bytes[..out.length]);
}
