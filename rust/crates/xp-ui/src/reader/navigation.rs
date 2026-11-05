//! Page and chapter navigation; the facade applies resource release under RenderLock.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Move {
    None,
    Page(i32),
    Chapter { spine: i32, last_page: bool },
    End(i32),
}

pub fn turn(
    spine: i32,
    spine_count: i32,
    page: i32,
    page_count: i32,
    building: bool,
    forward: bool,
) -> Move {
    if forward {
        if page < page_count - 1 || building {
            page.checked_add(1).map_or(Move::None, Move::Page)
        } else if spine.checked_add(1).is_some_and(|next| next < spine_count) {
            Move::Chapter {
                spine: spine + 1,
                last_page: false,
            }
        } else {
            Move::End(spine_count)
        }
    } else if page > 0 {
        Move::Page(page - 1)
    } else if spine > 0 {
        Move::Chapter {
            spine: spine - 1,
            last_page: true,
        }
    } else {
        Move::None
    }
}

pub fn skip(spine: i32, page: i32, amount: i32) -> Move {
    if amount > 0 {
        spine
            .checked_add(1)
            .map_or(Move::None, |spine| Move::Chapter {
                spine,
                last_page: false,
            })
    } else if page > 0 {
        Move::Page(0)
    } else if spine > 0 {
        Move::Chapter {
            spine: spine - 1,
            last_page: false,
        }
    } else {
        Move::None
    }
}
