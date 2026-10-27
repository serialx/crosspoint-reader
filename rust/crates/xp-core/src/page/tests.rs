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
fn direct_decode_matches_copy_and_preserves_invalid_input() {
    let mut wire = [0; 256];
    let mut b = Builder::new(&mut wire).unwrap();
    b.append(
        3,
        -4,
        Content::Image {
            path: b"image",
            source: b"source",
            width: 10,
            height: 20,
        },
    )
    .unwrap();
    let wire = b.finish().unwrap();
    let sized = probe::probe(wire.len(), |at, out| {
        out.copy_from_slice(&wire[at..at + out.len()]);
        true
    })
    .unwrap();
    assert_eq!(sized, inspect(wire).unwrap());
    let mut copied = [0; 512];
    decode(wire, 7, &mut copied).unwrap();
    let mut direct = [0xa5; 512];
    let offset = sized.arena_size - sized.consumed;
    direct[offset..sized.arena_size].copy_from_slice(wire);
    decode_in_place(&mut direct[..sized.arena_size], offset, wire.len(), 7).unwrap();
    assert_eq!(&direct[..sized.arena_size], &copied[..sized.arena_size]);
    direct[offset + 2] = 99;
    let before = direct;
    assert!(decode_in_place(&mut direct, offset, wire.len(), 7).is_err());
    assert_eq!(direct, before);
    assert!(probe::probe(wire.len(), |_, _| false).is_err());
    for len in 0..wire.len() {
        assert!(probe::probe(len, |at, out| {
            out.copy_from_slice(&wire[at..at + out.len()]);
            true
        })
        .is_err());
    }
}
