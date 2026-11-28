use xp_core::sdfont::control::reader_font;
use xp_layout::para::reclaim_caches;

#[test]
fn reader_font_preserves_all_point_sizes_and_loaded_family_precedence() {
    let ids = [101, 205, 309, 413];
    for points in 0..=255u8 {
        let expected = match points {
            0..=13 => 101,
            14..=15 => 205,
            16..=17 => 309,
            _ => 413,
        };
        assert_eq!(reader_font(&ids, points, 0), expected, "{points}");
        assert_eq!(reader_font(&ids, points, -77), -77);
        assert_eq!(reader_font(&ids, points, 900), 900);
    }
}

#[test]
fn reclaim_preserves_sink_priority_and_stops_on_reported_bytes() {
    for (have, render, vector, returns, expected) in [
        (100, true, true, [0, 0, 0], vec![]),
        (101, true, true, [0, 0, 0], vec![]),
        (80, true, true, [20, 99, 99], vec![2]),
        (80, true, true, [19, 1, 99], vec![2, 3]),
        (80, true, true, [0, 0, 20], vec![2, 3, 4]),
        (80, true, false, [0, 0, 20], vec![2, 3]),
        (80, false, true, [99, 20, 0], vec![3]),
        (80, false, false, [99, 0, 99], vec![3]),
    ] {
        let mut seen = Vec::new();
        reclaim_caches(100, have, render, vector, |operation| {
            seen.push(operation);
            returns[usize::from(operation - 2)]
        });
        assert_eq!(seen, expected);
    }
}

#[test]
fn reclaim_wraps_reported_totals_like_sdk_size_t() {
    let mut seen = Vec::new();
    reclaim_caches(usize::MAX, 0, true, true, |operation| {
        seen.push(operation);
        if operation == 2 {
            usize::MAX - 1
        } else {
            2
        }
    });
    assert_eq!(seen, [2, 3, 4]);
}
