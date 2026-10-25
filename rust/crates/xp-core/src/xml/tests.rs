extern crate std;
use super::*;
use std::{string::String, vec, vec::Vec};

struct Expected {
    kind: Kind,
    byte: i64,
    line: u64,
    fields: &'static [&'static str],
}
struct ExpectedRun {
    events: &'static [Expected],
    error: u32,
    byte: i64,
    line: u64,
}
struct Case {
    name: &'static str,
    input: &'static [u8],
    variants: &'static [ExpectedRun],
    splits: &'static [(usize, usize)],
    chunks: &'static [usize],
}
#[derive(Debug, PartialEq)]
struct Recorded {
    kind: Kind,
    byte: i64,
    line: u64,
    fields: Vec<String>,
}

fn drain(parser: &mut Parser, events: &mut Vec<Recorded>) -> Option<(u32, i64, u64)> {
    loop {
        let e = parser.next();
        match e.kind {
            Kind::NeedInput => return None,
            Kind::Done => return Some((0, e.byte as i64, e.line)),
            Kind::Error => return Some((e.error, e.byte as i64, e.line)),
            _ => {}
        }
        let fields: Vec<String> = e
            .fields
            .iter()
            .map(|f| String::from_utf8(e.data[f.range.clone()].to_vec()).unwrap())
            .collect();
        if e.kind == Kind::Text && events.last().is_some_and(|e| e.kind == Kind::Text) {
            events.last_mut().unwrap().fields[0].push_str(&fields[0]);
        } else {
            events.push(Recorded {
                kind: e.kind,
                byte: e.byte as i64,
                line: e.line,
                fields,
            });
        }
    }
}
fn replay(case: &Case, lengths: &[usize], variant: usize) -> Result<(), String> {
    let expected = &case.variants[variant];
    let mut parser = Parser::new();
    let mut events = vec![];
    let mut pos = 0;
    let mut final_state = None;
    for (index, &length) in lengths.iter().enumerate() {
        let end = pos + length;
        loop {
            let consumed = parser.feed(&case.input[pos..end], index == lengths.len() - 1);
            pos += consumed;
            final_state = drain(&mut parser, &mut events);
            if final_state.is_some() || pos == end {
                break;
            }
            if consumed == 0 {
                return Err(String::from("no progress"));
            }
        }
        if final_state.is_some() {
            break;
        }
    }
    let wanted: Vec<_> = expected
        .events
        .iter()
        .map(|e| Recorded {
            kind: e.kind,
            byte: e.byte,
            line: e.line,
            fields: e.fields.iter().map(|s| String::from(*s)).collect(),
        })
        .collect();
    if events != wanted || final_state != Some((expected.error, expected.byte, expected.line)) {
        return Err(std::format!(
            "actual={events:?} state={final_state:?}\nexpected={wanted:?} state={:?}",
            (expected.error, expected.byte, expected.line)
        ));
    }
    Ok(())
}

#[test]
fn oracle_replay() {
    let cases: &[Case] = include!("../../../../../test/xml_oracle/rust_goldens.rs");
    let mut failures = 0;
    let mut schedules = 0;
    for case in cases {
        let mut failure = None;
        let mut split = 0;
        for &(count, variant) in case.splits {
            for _ in 0..count {
                if let Err(error) = replay(case, &[split, case.input.len() - split], variant) {
                    failure = Some(std::format!("split {split}: {error}"));
                    break;
                }
                schedules += 1;
                split += 1;
            }
            if failure.is_some() {
                break;
            }
        }
        if failure.is_none() {
            for (index, size) in [1, 7, 512, 1024].iter().copied().enumerate() {
                let mut lengths = vec![];
                let mut left = case.input.len();
                while left > 0 {
                    let n = left.min(size);
                    lengths.push(n);
                    left -= n;
                }
                if lengths.is_empty() {
                    lengths.push(0);
                }
                if let Err(error) = replay(case, &lengths, case.chunks[index]) {
                    failure = Some(std::format!("chunks {size}: {error}"));
                    break;
                }
                schedules += 1;
            }
        }
        if let Some(error) = failure {
            std::eprintln!("{}: {}", case.name, error);
            failures += 1;
        }
    }
    std::eprintln!(
        "{schedules} schedules matched; {failures}/{} inputs failed",
        cases.len()
    );
    assert_eq!(failures, 0);
}

#[test]
fn stop_is_latched_and_completion_does_not_accept_more_input() {
    let mut parser = Parser::new();
    assert_eq!(parser.feed(b"<r/>", true), 4);
    assert_eq!(parser.next().kind, Kind::Start);
    parser.stop();
    assert_eq!(parser.next().kind, Kind::Stopped);
    assert_eq!(parser.feed(b"<more/>", true), 0);
    assert_eq!(parser.next().kind, Kind::Stopped);
    let mut parser = Parser::new();
    parser.feed(b"<r/>", true);
    assert_eq!(parser.next().kind, Kind::Start);
    assert_eq!(parser.next().kind, Kind::End);
    assert_eq!(parser.next().kind, Kind::Done);
    assert_eq!(parser.feed(b"<more/>", true), 0);
    assert_eq!(parser.next().kind, Kind::Done);
}

#[test]
fn streaming_text_and_cdata_do_not_retain_the_document() {
    for cdata in [false, true] {
        let mut parser = Parser::new();
        parser.feed(if cdata { b"<r><![CDATA[" } else { b"<r>" }, false);
        while parser.next().kind != Kind::NeedInput {}
        let block = [b'x'; 1024];
        for _ in 0..2048 {
            assert_eq!(parser.feed(&block, false), block.len());
            let mut text = 0;
            loop {
                let event = parser.next();
                match event.kind {
                    Kind::Text => text += event.fields[0].range.len(),
                    Kind::NeedInput => break,
                    other => panic!("{other:?}"),
                }
            }
            assert_eq!(text, 1024);
            assert!(parser.input.capacity() <= 4096);
            assert!(parser.token.capacity() <= 4096);
        }
        let tail = if cdata {
            b"]]></r>".as_slice()
        } else {
            b"</r>".as_slice()
        };
        parser.feed(tail, true);
        loop {
            match parser.next().kind {
                Kind::CdataEnd | Kind::End => {}
                Kind::Done => break,
                other => panic!("{other:?}"),
            }
        }
    }
}
