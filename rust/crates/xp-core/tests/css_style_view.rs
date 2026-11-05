use xp_core::css::{Style, StyleView};

#[test]
fn view_preserves_every_scalar_and_length_without_native_bitfields() {
    let mut style = Style::default();
    style.0[..5].copy_from_slice(&[4, 2, 2, 3, 1]);
    for i in 0..12 {
        let value = -(i as f32) - 0.25;
        style.0[5 + 5 * i..9 + 5 * i].copy_from_slice(&value.to_le_bytes());
        style.0[9 + 5 * i] = (i % 5) as u8;
    }
    style.0[65..84].copy_from_slice(&[1, 2, 1, 1, 1, 1, 255, 4, 8, 3, 7, 2, 6, 1, 255, 1, 1, 6, 1]);
    style.0[84..].copy_from_slice(&0x7fff_ffffu32.to_le_bytes());
    let view = style.view().unwrap();
    assert_eq!(core::mem::size_of::<StyleView>(), 88);
    assert_eq!(view.defined_bits, 0x7fff_ffff);
    for i in 0..12 {
        assert_eq!(view.lengths[i], -(i as f32) - 0.25);
        assert_eq!(view.units[i], (i % 5) as u8);
    }
    assert_eq!(
        [
            view.text_align,
            view.font_style,
            view.font_weight,
            view.text_decoration,
            view.direction
        ],
        [4, 2, 2, 3, 1]
    );
    assert_eq!(
        [
            view.display,
            view.vertical_align,
            view.list_style_type,
            view.small_caps,
            view.page_break_before,
            view.page_break_after
        ],
        [1, 2, 1, 1, 1, 1]
    );
    assert_eq!(view.borders, [255, 4, 8, 3, 7, 2, 6, 1]);
    assert_eq!(
        [
            view.border_components,
            view.shaded,
            view.float_left,
            view.initial_letter,
            view.preserve_whitespace
        ],
        [255, 1, 1, 6, 1]
    );
}

#[test]
fn view_rejects_all_invalid_fields_and_nonfinite_lengths() {
    let limits = [
        (0, 4),
        (1, 2),
        (2, 2),
        (3, 3),
        (4, 1),
        (65, 1),
        (66, 2),
        (67, 1),
        (68, 1),
        (69, 1),
        (70, 1),
        (72, 4),
        (74, 4),
        (76, 4),
        (78, 4),
        (80, 1),
        (81, 1),
        (82, 6),
        (83, 1),
    ];
    for (offset, limit) in limits {
        for value in limit + 1..=255 {
            let mut style = Style::default();
            style.0[offset] = value;
            assert!(style.view().is_none(), "offset {offset} value {value}");
        }
    }
    for i in 0..12 {
        for unit in 5..=255 {
            let mut style = Style::default();
            style.0[9 + i * 5] = unit;
            assert!(style.view().is_none());
        }
        for value in [f32::INFINITY, f32::NEG_INFINITY, f32::NAN] {
            let mut style = Style::default();
            style.0[5 + i * 5..9 + i * 5].copy_from_slice(&value.to_le_bytes());
            assert!(style.view().is_none());
        }
    }
    let mut style = Style::default();
    style.0[87] = 128;
    assert!(style.view().is_none());
}
