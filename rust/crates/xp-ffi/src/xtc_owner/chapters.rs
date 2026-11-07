use super::abi::{XpXtcChapterRow, XpXtcLeaves};
use core::{mem::size_of, ptr};

#[repr(C)]
pub(super) struct Chapters {
    pub data: *mut XpXtcChapterRow,
    pub length: usize,
    capacity: usize,
}
impl Chapters {
    pub const fn new() -> Self {
        Self {
            data: ptr::null_mut(),
            length: 0,
            capacity: 0,
        }
    }
    unsafe fn local(row: *mut XpXtcChapterRow) -> *mut u8 {
        ptr::addr_of_mut!((*row).inline_name).cast()
    }
    pub unsafe fn clear(rows: *mut Self, leaves: &XpXtcLeaves) {
        let length = (*rows).length;
        // A release callback may inspect the view; no freed name remains published.
        (*rows).length = 0;
        for index in 0..length {
            let row = (*rows).data.add(index);
            if (*row).name != Self::local(row) {
                (leaves.release)((*row).name.cast_mut(), (*row).name_len + 1);
            }
        }
    }
    pub unsafe fn reserve(rows: *mut Self, count: usize, leaves: &XpXtcLeaves) {
        // The parser clears all rows before reserve, so no initialized self-pointer moves.
        assert_eq!((*rows).length, 0);
        if count > (*rows).capacity {
            let bytes = count
                .checked_mul(size_of::<XpXtcChapterRow>())
                .expect("XTC chapter capacity");
            let data = (leaves.allocate)(bytes, false).cast();
            if (*rows).capacity != 0 {
                (leaves.release)(
                    (*rows).data.cast(),
                    (*rows).capacity * size_of::<XpXtcChapterRow>(),
                );
            }
            (*rows).data = data;
            (*rows).capacity = count;
        }
    }
    pub unsafe fn append(
        rows: *mut Self,
        bytes: *const u8,
        length: usize,
        start: u16,
        end: u16,
        leaves: &XpXtcLeaves,
    ) {
        assert!((*rows).length < (*rows).capacity);
        let row = (*rows).data.add((*rows).length);
        ptr::addr_of_mut!((*row).inline_name).write([0; 16]);
        let data = if length <= 15 {
            Self::local(row)
        } else {
            (leaves.allocate)(length + 1, false)
        };
        if length != 0 {
            ptr::copy_nonoverlapping(bytes, data, length);
        }
        data.add(length).write(0);
        ptr::addr_of_mut!((*row).name).write(data);
        ptr::addr_of_mut!((*row).name_len).write(length);
        ptr::addr_of_mut!((*row).start_page).write(start);
        ptr::addr_of_mut!((*row).end_page).write(end);
        (*rows).length += 1;
    }
    pub unsafe fn release(rows: *mut Self, leaves: &XpXtcLeaves) {
        if (*rows).capacity != 0 {
            (leaves.release)(
                (*rows).data.cast(),
                (*rows).capacity * size_of::<XpXtcChapterRow>(),
            );
        }
    }
}
