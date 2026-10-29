//! Download/update screen models and bounded scenes; services retain I/O ownership.
pub mod ffi;
pub mod state;
pub mod view;
use crate::{
    model::ListState,
    scene::{Error, Rect},
    settings::{hits, settings_ffi, view as settings_view},
};
use core::ffi::{c_char, c_void};
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub list: settings_view::Layout,
    pub progress_height: i16,
    pub popup_padding: i16,
    pub popup_gap: i16,
    pub popup_side_margin: i16,
    pub selection_padding: i16,
    pub popup_radius: u8,
    pub popup_border: u8,
    pub reserved: [u8; 2],
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub rows: settings_ffi::Services,
    pub context: *mut c_void,
    pub text: Option<unsafe extern "C" fn(*mut c_void, u32) -> *const c_char>,
    pub text_height: Option<unsafe extern "C" fn(*mut c_void, u32, i32, i16, u8, u8) -> i16>,
}
impl Services {
    fn text(&self, handle: u32) -> Result<*const c_char, Error> {
        let callback = self.text.ok_or(Error::Invalid)?;
        let text = unsafe { callback(self.context, handle) };
        if text.is_null() {
            Err(Error::Invalid)
        } else {
            Ok(text)
        }
    }
    fn present(&self, handle: u32) -> Result<bool, Error> {
        Ok(unsafe { *self.text(handle)? } != 0)
    }
    fn width(&self, text: *const c_char, font: i32, bold: u8) -> Result<i32, Error> {
        let callback = self.rows.width.ok_or(Error::Invalid)?;
        let width = unsafe { callback(self.rows.context, text, font, bold) };
        if (0..=32767).contains(&width) {
            Ok(width)
        } else {
            Err(Error::Invalid)
        }
    }
    fn height(
        &self,
        handle: u32,
        font: i32,
        width: i16,
        bold: u8,
        lines: u8,
    ) -> Result<i16, Error> {
        let callback = self.text_height.ok_or(Error::Invalid)?;
        let height = unsafe { callback(self.context, handle, font, width, bold, lines) };
        if (0..=4096).contains(&height) {
            Ok(height)
        } else {
            Err(Error::Invalid)
        }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Feedback {
    pub list: ListState,
    pub rows: hits::Layout,
    pub popup: Rect,
    pub options: [Rect; 2],
    pub last_percent: u32,
    pub rendered: u8,
    pub reserved: [u8; 3],
}
#[cfg(test)]
mod tests;

#[cfg(test)]
mod abi;

#[cfg(test)]
mod build_tests;
