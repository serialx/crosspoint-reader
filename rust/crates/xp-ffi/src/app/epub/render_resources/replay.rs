//! Strip replay keeps no Rust slice loan across native drawing callbacks.
use super::{physical, PageGeometry, PageResources};
use core::{cell::Cell, ptr};

impl PageResources {
    /// The panel supplies the admitted scratch window and valid display rows.
    /// Page and framebuffer storage remain live through the last detach callback.
    pub(crate) unsafe fn replay(
        &self,
        physical: &impl physical::Physical,
        scratch: *mut u8,
        y: u16,
        rows: u16,
        highlights: impl FnOnce(),
        draw: impl FnOnce(),
    ) -> u32 {
        let frame = &self.frame;
        assert!(frame.active.get());
        unsafe {
            paint(
                physical,
                frame.page.get(),
                &frame.strips.committed_rows,
                (scratch, y, rows),
                highlights,
                draw,
            )
        }
    }
}

// The host pixel oracle uses this same replay with its existing retained-row
// control. Production calls it only through the active PageResources frame.
pub(crate) unsafe fn paint(
    physical: &impl physical::Physical,
    page: PageGeometry,
    committed_rows: &Cell<u16>,
    strip: (*mut u8, u16, u16),
    highlights: impl FnOnce(),
    draw: impl FnOnce(),
) -> u32 {
    let (scratch, y, rows) = strip;
    let stride = physical.query(physical::Query::WidthBytes) as usize;
    let bytes = usize::from(rows) * stride;
    let (pixels, length) = physical.framebuffer();
    let offset = usize::from(y) * stride;
    assert!(offset <= length && bytes <= length - offset);
    let retained = unsafe { pixels.add(offset) };
    let committed = committed_rows.get();
    if !page.images && y < committed {
        if u32::from(y) + u32::from(rows) > u32::from(committed) {
            return 1;
        }
        unsafe { ptr::copy_nonoverlapping(retained, scratch, bytes) };
        return 0;
    }
    if !page.images && y != committed {
        return 1;
    }
    let prior = physical.query(physical::Query::Mode);
    let mut op = physical::Op::new(physical::Kind::Mode);
    physical.operation(&op);
    op = physical::Op::new(physical::Kind::StripBegin);
    op.buffer = scratch;
    op.y = y;
    op.rows = rows;
    physical.operation(&op);
    op = physical::Op::new(physical::Kind::Clear);
    op.value = 255;
    physical.operation(&op);
    highlights();
    draw();
    op = physical::Op::new(physical::Kind::CopyStatus);
    op.value = page.status_top as u32;
    op.mode = u8::from(page.inverted);
    physical.operation(&op);
    physical.operation(&physical::Op::new(physical::Kind::StripEnd));
    if page.inverted {
        for index in 0..bytes {
            unsafe { *scratch.add(index) ^= 255 };
        }
    }
    op = physical::Op::new(physical::Kind::Mode);
    op.mode = prior as u8;
    physical.operation(&op);
    if page.images {
        for index in 0..bytes {
            if unsafe { *scratch.add(index) != *retained.add(index) } {
                return 1;
            }
        }
        return 0;
    }
    unsafe { ptr::copy_nonoverlapping(scratch, retained, bytes) };
    committed_rows.set(committed + rows);
    0
}
