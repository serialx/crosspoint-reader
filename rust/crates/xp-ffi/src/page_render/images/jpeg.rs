//! JPEGDEC is a physical stream; Rust owns its pipeline and release order.
use super::{decode::*, *};
use xp_core::image::{Config as ImageConfig, Pipeline};

#[repr(C)]
struct Native {
    decoder: *mut c_void,
    path: *const u8,
    rows: *mut c_void,
    width: i32,
    height: i32,
    result: i32,
    error: i32,
    progressive: u8,
}
#[repr(u32)]
enum Operation {
    Allocate,
    Open,
    Facts,
    Configure,
    Decode,
    Close,
    Free,
}
extern "C" {
    fn xp_native_page_jpeg(native: *mut Native, operation: Operation, argument: u32) -> u32;
    fn xp_native_page_jpeg_log(native: *const Native, event: u32, argument: u32);
}
struct Rows<'b, 'c> {
    decode: *mut Decode<'b, 'c>,
    drawing: Option<crate::image::Context>,
}
struct Jpeg<'b, 'c> {
    rows: *mut Rows<'b, 'c>,
    native: Native,
    config: ImageConfig,
    opened: bool,
    started: u32,
    elapsed: u32,
}
impl Jpeg<'_, '_> {
    fn native(&mut self, operation: Operation, argument: u32) -> u32 {
        unsafe { xp_native_page_jpeg(&mut self.native, operation, argument) }
    }
    unsafe extern "C" fn diagnostic(raw: *mut c_void, event: u32, argument: u32) {
        let this = &*raw.cast::<Self>();
        xp_native_page_jpeg_log(
            &this.native,
            event,
            if event == 10 { this.elapsed } else { argument },
        );
    }
    unsafe extern "C" fn operation(raw: *mut c_void, operation: u32, argument: u32) -> u32 {
        let this = &mut *raw.cast::<Self>();
        // Values are the unchanged xp_book_jpeg operation table.
        match operation {
            0 => (*(*this.rows).decode).physical.scalar(FileOp::FreeHeap, 0) as u32,
            1 => this.native(Operation::Allocate, 0),
            2 => {
                this.opened = true;
                this.native.rows = this.rows.cast();
                this.native(Operation::Open, 0)
            }
            3 => {
                this.native(Operation::Facts, 0);
                let status = crate::book_policy::xp_book_image_dimensions(
                    this.native.width as i64,
                    this.native.height as i64,
                );
                if status != 0 {
                    xp_native_page_jpeg_log(&this.native, 12, status as u32);
                }
                u32::from(status == 0)
            }
            4 => this.native.progressive as u32,
            5 => 0,
            6 => {
                let mut callbacks = Decode::callbacks((*this.rows).decode);
                (*this.rows).drawing = Pipeline::new(
                    this.config,
                    this.native.width,
                    this.native.height,
                    argument != 0,
                    &mut callbacks,
                )
                .map(|pipeline| crate::image::Context {
                    pipeline,
                    callbacks,
                });
                u32::from((*this.rows).drawing.is_some())
            }
            7 => {
                let drawing = (*this.rows)
                    .drawing
                    .as_mut()
                    .expect("initialized JPEG pipeline");
                u32::from(drawing.pipeline.prepare(&mut drawing.callbacks))
            }
            8 => {
                (*this.rows)
                    .drawing
                    .as_ref()
                    .expect("initialized JPEG pipeline")
                    .pipeline
                    .plan
                    .denominator as u32
            }
            9 => {
                this.native.rows = this.rows.cast();
                this.native(Operation::Configure, argument)
            }
            10 => u32::from(*(*(*this.rows).decode).log.cache != 0),
            11 => {
                let drawing = (*this.rows)
                    .drawing
                    .as_mut()
                    .expect("initialized JPEG pipeline");
                u32::from(drawing.pipeline.start_cache(&mut drawing.callbacks))
            }
            12 => {
                this.started = (*(*this.rows).decode).physical.scalar(FileOp::Now, 0) as u32;
                (*(*this.rows).decode).last_yield = this.started;
                0
            }
            13 => this.native(
                Operation::Decode,
                (*this.rows)
                    .drawing
                    .as_ref()
                    .expect("initialized JPEG pipeline")
                    .pipeline
                    .plan
                    .denominator as u32,
            ),
            14 => {
                this.elapsed = ((*(*this.rows).decode).physical.scalar(FileOp::Now, 0) as u32)
                    .wrapping_sub(this.started);
                0
            }
            15 => {
                let drawing = (*this.rows)
                    .drawing
                    .as_mut()
                    .expect("initialized JPEG pipeline");
                u32::from(drawing.pipeline.finish(&mut drawing.callbacks))
            }
            16 => u32::from(
                (*this.rows)
                    .drawing
                    .as_ref()
                    .expect("initialized JPEG pipeline")
                    .pipeline
                    .success(),
            ),
            _ => 0,
        }
    }
}
impl Drop for Jpeg<'_, '_> {
    fn drop(&mut self) {
        if self.opened {
            self.native(Operation::Close, 0);
        }
        if let Some(drawing) = unsafe { (*self.rows).drawing.as_mut() } {
            drawing.pipeline.abort(&mut drawing.callbacks);
        }
        unsafe { (*self.rows).drawing = None };
        self.native(Operation::Free, 0);
    }
}
#[no_mangle]
unsafe extern "C" fn xp_page_image_jpeg_row(
    raw: *mut c_void,
    row: *const crate::image::Row,
) -> i32 {
    let this = &mut *raw.cast::<Rows<'_, '_>>();
    Decode::progress(this.decode.cast());
    this.drawing.as_mut().map_or(0, |drawing| {
        i32::from(crate::image::xp_image_row(
            ptr::from_mut(drawing).cast(),
            row,
        ))
    })
}
#[inline(never)]
pub(super) fn render(decode: &mut Decode<'_, '_>, config: ImageConfig) -> bool {
    // Rows stay disjoint from the native operation record during decoder callbacks.
    let mut rows = Rows {
        decode: ptr::from_mut(decode),
        drawing: None,
    };
    let mut context = Jpeg {
        native: Native {
            decoder: ptr::null_mut(),
            path: unsafe { (*rows.decode).physical.image.path },
            rows: ptr::null_mut(),
            width: 0,
            height: 0,
            result: 0,
            error: 0,
            progressive: 0,
        },
        rows: ptr::from_mut(&mut rows),
        config,
        opened: false,
        started: 0,
        elapsed: 0,
    };
    let services = crate::book_parser::Services {
        ctx: ptr::from_mut(&mut context).cast(),
        operation: Some(Jpeg::operation),
        millis: None,
        diagnostic: Some(Jpeg::diagnostic),
    };
    unsafe { crate::book_parser::xp_book_jpeg(&services, 1) != 0 }
}
