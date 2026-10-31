//! Synchronous toolbar view construction; strings and drawing remain borrowed resources.
use core::ffi::c_void;
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Query {
    HasBook = 0,
    HasSection = 1,
    HasToolbar = 2,
    Mode = 3,
    Cursor = 4,
    Focus = 5,
    ChapterTitle = 6,
    PageCount = 7,
    CurrentPage = 8,
    Progress = 9,
    Touch = 10,
    Index = 11,
    TocCount = 12,
    MoreCount = 13,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct View {
    pub active_tool: i32,
    pub selected_index: i32,
    pub item_count: i32,
    pub progress_permille: i32,
    pub kind: u8,
    pub dense: u8,
    pub reserved: [u8; 2],
    pub page_info: [u8; 40],
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub context: *mut c_void,
    pub query: Option<unsafe extern "C" fn(*mut c_void, u8, u32) -> u32>,
    pub paint: Option<unsafe extern "C" fn(*mut c_void, *const View)>,
}
fn number(out: &mut [u8], value: i32) -> usize {
    let mut digits = [0u8; 10];
    let mut n = value.unsigned_abs();
    let mut count = 0;
    loop {
        digits[count] = b'0' + (n % 10) as u8;
        count += 1;
        n /= 10;
        if n == 0 {
            break;
        }
    }
    let mut used = 0;
    if value < 0 {
        out[0] = b'-';
        used = 1;
    }
    for digit in digits[..count].iter().rev() {
        out[used] = *digit;
        used += 1;
    }
    used
}
/// # Safety
/// Services remain valid for this call, do not reenter, and retain no view pointer.
/// The caller holds the render lock and does not hold a model lock across callbacks.
pub unsafe fn run(s: Services) {
    let (Some(query), Some(paint)) = (s.query, s.paint) else {
        return;
    };
    let q = |kind: Query| query(s.context, kind as u8, 0);
    if q(Query::HasBook) == 0 || q(Query::HasSection) == 0 || q(Query::HasToolbar) == 0 {
        return;
    }
    let mode = q(Query::Mode);
    let mut view = View {
        active_tool: if mode == 1 && q(Query::Cursor) == 0 {
            -1
        } else {
            q(Query::Focus) as i32
        },
        selected_index: -1,
        item_count: 0,
        progress_permille: 0,
        kind: 1,
        dense: 0,
        reserved: [0; 2],
        page_info: [0; 40],
    };
    if mode == 1 {
        q(Query::ChapterTitle);
        let count = q(Query::PageCount) as i32;
        let page = (q(Query::CurrentPage) as i32).wrapping_add(1);
        let chapter = if count > 0 {
            page as f32 / count as f32
        } else {
            0.0
        };
        let progress = f32::from_bits(query(s.context, Query::Progress as u8, chapter.to_bits()));
        let mut used = number(&mut view.page_info, page);
        view.page_info[used] = b'/';
        used += 1;
        used += number(&mut view.page_info[used..], count);
        view.page_info[used..used + 3].copy_from_slice(b"   ");
        used += 3;
        used += number(
            &mut view.page_info[used..],
            ((progress * 100.0 + 0.5) as i32).clamp(0, 100),
        );
        view.page_info[used] = b'%';
        view.progress_permille = (progress * 1000.0 + 0.5) as i32;
    } else {
        view.dense = u8::from(q(Query::Touch) == 0);
        view.selected_index = if q(Query::Cursor) != 0 {
            q(Query::Index) as i32
        } else {
            -1
        };
        (view.kind, view.item_count) = match mode {
            2 => (2, q(Query::TocCount) as i32),
            3 => (3, super::epub_overlay::TEXT_ROWS),
            _ => (4, q(Query::MoreCount) as i32),
        };
    }
    paint(s.context, &view);
}
const _: () = {
    use core::mem::{align_of, offset_of, size_of};
    assert!(size_of::<View>() == 60 && align_of::<View>() == 4);
    assert!(offset_of!(View, active_tool) == 0 && offset_of!(View, selected_index) == 4);
    assert!(offset_of!(View, item_count) == 8 && offset_of!(View, progress_permille) == 12);
    assert!(offset_of!(View, kind) == 16 && offset_of!(View, dense) == 17);
    assert!(offset_of!(View, reserved) == 18 && offset_of!(View, page_info) == 20);
    assert!(size_of::<Services>() == 3 * size_of::<usize>());
    assert!(align_of::<Services>() == align_of::<usize>());
    assert!(offset_of!(Services, context) == 0);
    assert!(offset_of!(Services, query) == size_of::<usize>());
    assert!(offset_of!(Services, paint) == 2 * size_of::<usize>());
};

/// Row decoration selector; native resources supply the current setting value.
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Decoration {
    None = 0,
    Focus = 1,
    Night = 2,
    Bluetooth = 3,
    Frontlight = 4,
}
pub fn checkbox(mode: u8, row_or_action: i32) -> u8 {
    (match (mode, row_or_action) {
        (3, 4) => Decoration::Focus,
        (4, 3) => Decoration::Night,
        (4, 5) => Decoration::Bluetooth,
        (4, 4) => Decoration::Frontlight,
        _ => Decoration::None,
    }) as u8
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn decimal_extremes_fit_the_view_buffer() {
        for (value, expected) in [
            (i32::MIN, "-2147483648"),
            (-1, "-1"),
            (0, "0"),
            (1, "1"),
            (i32::MAX, "2147483647"),
        ] {
            let mut buffer = [0u8; 40];
            let used = number(&mut buffer, value);
            assert_eq!(&buffer[..used], expected.as_bytes());
            assert_eq!(buffer[used], 0);
        }
    }
    #[test]
    fn only_toggle_rows_request_settings() {
        for row in -1..20 {
            assert_eq!(checkbox(3, row) != 0, row == 4);
            assert_eq!(checkbox(4, row) != 0, (3..=5).contains(&row));
            assert_eq!(checkbox(2, row), 0);
        }
    }
}
