use xp_ui::reader::status::*;
unsafe extern "C" {
    fn snprintf(out: *mut u8, n: usize, format: *const u8, ...) -> i32;
}
fn native(page: i32, pages: i32, percent: f32, count: bool, show_percent: bool) -> Vec<u8> {
    let mut out = [0u8; 32];
    let format = match (count, show_percent) {
        (true, true) => b"%d/%d  %.0f%%\0".as_slice(),
        (true, false) => b"%d/%d\0".as_slice(),
        (false, true) => b"%.0f%%\0".as_slice(),
        _ => return vec![0],
    };
    unsafe {
        if count {
            if show_percent {
                snprintf(
                    out.as_mut_ptr(),
                    32,
                    format.as_ptr(),
                    page,
                    pages,
                    percent as f64,
                );
            } else {
                snprintf(out.as_mut_ptr(), 32, format.as_ptr(), page, pages);
            }
        } else {
            snprintf(out.as_mut_ptr(), 32, format.as_ptr(), percent as f64);
        }
    }
    out[..=out.iter().position(|&x| x == 0).unwrap()].to_vec()
}
#[test]
fn progress_rounds_and_truncates_like_native() {
    let mut values = vec![
        -0.0,
        0.0,
        f32::INFINITY,
        f32::NEG_INFINITY,
        f32::NAN,
        f32::MAX,
        -f32::MAX,
        16_777_216.0,
    ];
    for i in -201..=201 {
        let v = i as f32 / 2.0;
        values.extend([v, v.next_down(), v.next_up()]);
    }
    for p in values {
        for count in [false, true] {
            for show in [false, true] {
                for (page, pages) in [(1, 123), (i32::MIN, i32::MAX)] {
                    assert_eq!(
                        progress_text(page, pages, p, count, show).as_z(),
                        native(page, pages, p, count, show),
                        "{p:?} {count} {show}"
                    );
                }
            }
        }
    }
}
#[test]
fn positions_preserve_cluster_spacing() {
    let f = Facts {
        screen_width: 528,
        screen_height: 792,
        status_height: 19,
        margin_left: 3,
        margin_right: 7,
        margin_bottom: 4,
        horizontal_margin: 5,
        corner: 12,
        offset_y: 2,
        ..Facts::default()
    };
    let mut l = Layout::new(&f);
    assert_eq!((l.text_y, l.left, l.right), (767, 18, 511));
    assert_eq!(l.progress(80, 7, true), 422);
    assert_eq!(l.right_width, 89);
    l.battery(15, 20, true);
    assert_eq!(l.left_width, 39);
    assert_eq!(l.clock(30, 2), 67);
    assert_eq!(l.left_width, 79);
    assert_eq!(l.bookmark(), 101);
    assert_eq!(l.left_width, 99);
    assert_eq!(l.title_space(&f, 100), (129, 250));
    assert_eq!(l.title_space(&f, 260), (129, 260));
}
#[test]
fn bar_uses_unsigned_order_and_float_chapter_fraction() {
    let mut f = Facts {
        screen_width: 528,
        screen_height: 792,
        margin_bottom: 4,
        bar_height: 6,
        bar_mode: 0,
        ..Facts::default()
    };
    assert_eq!(bar(&f, 1, 3, 33.9), Some((0, 783, 174, 9)));
    f.screen_width = i32::MAX;
    assert_eq!(bar(&f, 1, 3, 100.0), Some((0, 783, 42_949_671, 9)));
    f.screen_width = 528;
    f.bar_mode = 1;
    assert_eq!(bar(&f, 2, 3, 0.0), Some((0, 783, 348, 9)));
    assert_eq!(bar(&f, 1, 0, 0.0), Some((0, 783, 0, 9)));
    f.bar_mode = 2;
    assert_eq!(bar(&f, 1, 3, 33.9), None);
}
