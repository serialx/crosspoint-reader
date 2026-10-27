use super::*;
use builder::{Builder, Content, PackedLine};
#[test]
fn all_elements_round_trip_unaligned_and_capacity_failures() {
    let mut wire = [0; 2048];
    let mut builder = Builder::new(&mut wire).unwrap();
    let mut block = section::Record {
        kind: Kind::BlockStyle as u32,
        ..Default::default()
    };
    block.values[15] = 42;
    let line = PackedLine {
        text: b"one\0two\0",
        offsets: &[0, 4],
        positions: &[-3, 20],
        styles: &[128, 1],
        boundaries: &[1, 0],
        suffixes: &[4, 0],
        sources: &[0, 3, 4, 7],
        ruby: &&[b"ruby".as_slice(), b"".as_slice()][..],
        paragraph_start: 0,
        block,
        fonts: Some(([99, 0, 0], &[0x81, 0])),
    };
    builder.append(-5, 8, Content::Line(&line)).unwrap();
    builder
        .append(
            1,
            2,
            Content::Image {
                path: b"x.png",
                source: b"img/x.png",
                width: 30,
                height: -2,
            },
        )
        .unwrap();
    builder
        .append(
            3,
            4,
            Content::Rule {
                width: 19,
                thickness: 2,
            },
        )
        .unwrap();
    builder
        .append(
            0,
            9,
            Content::DropCap {
                font: -3,
                scale: 256,
                style: 2,
                text: b"Ab",
            },
        )
        .unwrap();
    builder
        .append(
            0,
            0,
            Content::Border {
                width: 4,
                height: 8,
                sides: [1, 2, 3, 4, 0, 0, 0, 0],
                shaded: true,
            },
        )
        .unwrap();
    builder.footnote(&[1; 32], &[2; 256]).unwrap();
    let mut href = [0; 256];
    href[..2].copy_from_slice(b"#x");
    builder
        .link(Link {
            href: &href,
            x: -1,
            y: 2,
            width: 10,
            height: 11,
        })
        .unwrap();
    let wire = builder.finish().unwrap();
    let size = inspect(wire).unwrap().arena_size;
    let mut arena = [0xa5; 4096];
    assert_eq!(
        decode(wire, 13, &mut arena[1..size]),
        Err(Error::NeedOutput(size))
    );
    assert!(arena.iter().all(|b| *b == 0xa5));
    decode(wire, 13, &mut arena[1..]).unwrap();
    let page = Page::open(&arena[1..]).unwrap();
    assert_eq!(page.visible_offset(), 13);
    assert_eq!(page.element_count(), 5);
    let line = page.element(0).unwrap().line().unwrap();
    let word = line.word(0, 7).unwrap();
    assert_eq!(word.text, b"one");
    assert_eq!(word.font, 99);
    assert_eq!(word.style, 128);
    assert!(word.discretionary_hyphen && word.small_caps && word.starts_paragraph);
    assert_eq!(word.focus_suffix, 4);
    assert_eq!(line.ruby(0).unwrap(), b"ruby");
    assert_eq!(line.word(1, 7).unwrap().font, 42);
    let mut output = [0; 2048];
    let len = page.encode(&mut output).unwrap();
    assert_eq!(&output[..len], wire);
    for cut in 0..wire.len() {
        assert!(inspect(&wire[..cut]).is_err(), "cut {cut}");
    }
}
#[test]
fn builder_failure_is_atomic_and_retryable() {
    let mut data = [0xa5; 14];
    let mut b = Builder::new(&mut data).unwrap();
    assert!(b
        .append(
            0,
            0,
            Content::Rule {
                width: 0,
                thickness: 1
            }
        )
        .is_err());
    b.append(
        0,
        0,
        Content::Rule {
            width: 1,
            thickness: 1,
        },
    )
    .unwrap();
    assert!(matches!(
        b.append(
            0,
            0,
            Content::Rule {
                width: 2,
                thickness: 1
            }
        ),
        Err(Error::NeedOutput(_))
    ));
    let bytes = b.finish().unwrap();
    assert_eq!(inspect(bytes).unwrap().elements, 1);
}
#[test]
fn malformed_counts_and_trailing_bytes() {
    assert_eq!(inspect(&[0, 0, 0, 0, 0, 0, 42]).unwrap().consumed, 6);
    assert!(inspect(&[0, 0, 17, 0, 0, 0]).is_err());
    assert!(inspect(&[0, 0, 0, 0, 33, 0]).is_err());
    for a in 0..=255 {
        for b in 0..=255 {
            let _ = inspect(&[a, b, a, b, a, b]);
        }
    }
}

#[test]
fn layout_spans_share_arena_but_are_not_serialized() {
    let mut wire = [0; 256];
    let mut b = Builder::new(&mut wire).unwrap();
    let line = PackedLine {
        text: b"a\0",
        offsets: &[0],
        positions: &[0],
        styles: &[0],
        boundaries: &[],
        suffixes: &[],
        sources: &[0, 1],
        ruby: &&[][..],
        paragraph_start: 0,
        block: section::Record {
            kind: Kind::BlockStyle as u32,
            ..Default::default()
        },
        fonts: None,
    };
    b.append(0, 0, Content::Line(&line)).unwrap();
    let wire = b.finish().unwrap();
    let mut arena = [0; 1024];
    let info = decode(wire, 7, &mut arena).unwrap();
    let link = crate::para::paragraph_extract::Link {
        href: [42; 256],
        x: -8,
        width: 20,
        top_lift: 3,
    };
    let size = info.arena_size + 262;
    assert_eq!(
        attach_line_links(&mut arena[..size - 1], 0, core::slice::from_ref(&link)),
        Err(Error::NeedOutput(size))
    );
    assert_eq!(
        attach_line_links(&mut arena, 0, core::slice::from_ref(&link)),
        Ok(size)
    );
    let page = Page::open(&arena).unwrap();
    let line = page.element(0).unwrap().line().unwrap();
    assert_eq!(line.link_count(), 1);
    assert_eq!(line.link_span(0).unwrap(), (&link.href[..], -8, 20, 3));
    let mut encoded = [0; 256];
    assert_eq!(page.encode(&mut encoded), Ok(wire.len()));
    assert_eq!(&encoded[..wire.len()], wire);
}
