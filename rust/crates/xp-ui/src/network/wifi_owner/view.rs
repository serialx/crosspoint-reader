use super::super::{
    wifi_physical::{DrawKind, PaintData},
    wifi_resources::text,
};
use super::*;
use crate::{
    scene::Rect,
    settings::view::{Resources, Row as ViewRow},
};
use core::ffi::{c_char, CStr};
use xp_core::{
    glyph::GlyphTarget,
    i18n::{self, ids},
};
fn tr(language: u8, id: u16) -> *const c_char {
    i18n::get(language, id)
}
fn bytes(language: u8, id: u16) -> &'static [u8] {
    unsafe { CStr::from_ptr(tr(language, id)) }.to_bytes()
}
fn put(out: &mut [u8], offset: &mut usize, part: &[u8]) -> Result<(), Error> {
    let end = offset
        .checked_add(part.len())
        .filter(|n| *n < out.len())
        .ok_or(Error::Capacity)?;
    out[*offset..end].copy_from_slice(part);
    *offset = end;
    out[end] = 0;
    Ok(())
}
pub(super) fn count(out: &mut [u8; 64], language: u8, value: u16) -> Result<(), Error> {
    let format = bytes(language, ids::STR_NETWORKS_FOUND);
    let split = format
        .windows(3)
        .position(|p| p == b"%zu")
        .ok_or(Error::Invalid)?;
    let mut digits = [0; 5];
    let mut start = digits.len();
    let mut n = value;
    loop {
        start -= 1;
        digits[start] = b'0' + (n % 10) as u8;
        n /= 10;
        if n == 0 {
            break;
        }
    }
    let mut offset = 0;
    put(out, &mut offset, &format[..split])?;
    put(out, &mut offset, &digits[start..])?;
    put(out, &mut offset, &format[split + 3..])
}
pub(super) fn mac(out: &mut [u8; 64], language: u8, mac: &[u8; 6], success: bool) {
    let mut offset = 0;
    let _ = put(out, &mut offset, bytes(language, ids::STR_MAC_ADDRESS));
    let _ = put(out, &mut offset, b" ");
    if !success {
        let _ = put(out, &mut offset, b"--");
        return;
    }
    const HEX: &[u8; 16] = b"0123456789abcdef";
    for (i, byte) in mac.iter().enumerate() {
        if i != 0 {
            let _ = put(out, &mut offset, b"-");
        }
        let _ = put(
            out,
            &mut offset,
            &[HEX[usize::from(byte >> 4)], HEX[usize::from(byte & 15)]],
        );
    }
}
fn metric(value: i32) -> Result<i32, Error> {
    if (0..=32767).contains(&value) {
        Ok(value)
    } else {
        Err(Error::Invalid)
    }
}
pub(super) struct View<'a, H: Host> {
    pub owner: &'a Wifi<H>,
}
impl<H: Host> Wifi<H> {
    fn row_text(&self, id: u32) -> *const c_char {
        let Some(h) = id.checked_sub(1) else {
            return ptr::null();
        };
        let f = self.frame();
        let rows = unsafe { (*f).rows.as_slice() };
        if rows.is_empty() {
            return ptr::null();
        }
        let row = &rows[(h / 4) as usize % rows.len()];
        if u32::from(row.index) != h / 4 {
            return ptr::null();
        }
        match h % 4 {
            0 if row.hidden != 0 => tr(unsafe { (*f).language }, ids::STR_ADD_HIDDEN_NETWORK),
            0 => row.ssid.as_ptr().cast(),
            2 if row.status[0] != 0 => row.status.as_ptr().cast(),
            _ => ptr::null(),
        }
    }
    fn text(&self, id: u32) -> Result<*const c_char, Error> {
        use wifi_view::*;
        if id < HEADER {
            let text = self.row_text(id);
            return if text.is_null() {
                Err(Error::Invalid)
            } else {
                Ok(text)
            };
        }
        let f = self.frame();
        let lang = unsafe { (*f).language };
        let translation = match id {
            EMPTY => ids::STR_NO_NETWORKS,
            RETRY => ids::STR_RETRY,
            PRESS_SCAN => ids::STR_PRESS_OK_SCAN,
            SCANNING => ids::STR_SCANNING,
            FINDING_SAVED => ids::STR_FINDING_SAVED_WIFI,
            CONNECTING => ids::STR_CONNECTING,
            CONNECTING_SAVED => ids::STR_CONNECTING_SAVED_WIFI,
            PROMPT_CONNECTED | CONNECTED => ids::STR_CONNECTED,
            FAILED => ids::STR_CONNECTION_FAILED,
            PROMPT_FORGET | FORGET => ids::STR_FORGET_NETWORK,
            REMOVE => ids::STR_FORGET_AND_REMOVE,
            SAVE_PASSWORD => ids::STR_SAVE_PASSWORD,
            CANCEL => ids::STR_CANCEL,
            YES => ids::STR_YES,
            NO => ids::STR_NO,
            ERROR => return Ok(unsafe { (*self.owned()).error }),
            PROMPT_SSID | TO_SSID | NETWORK_SSID | IP => {
                let r = unsafe { &*self.owned() };
                let (prefix, source) = if id == IP {
                    (
                        ids::STR_IP_ADDRESS_PREFIX,
                        &r.ip[..r.ip.iter().position(|b| *b == 0).unwrap_or(15)],
                    )
                } else {
                    (
                        if id == TO_SSID {
                            ids::STR_TO_PREFIX
                        } else {
                            ids::STR_NETWORK_PREFIX
                        },
                        r.selected.as_bytes(),
                    )
                };
                let scratch = unsafe { &mut (*f).text };
                scratch
                    .write_joined(&[bytes(lang, prefix), source])
                    .map_err(|_| Error::Capacity)?;
                return Ok(scratch.as_z().as_ptr().cast());
            }
            _ => return Err(Error::Invalid),
        };
        Ok(tr(lang, translation))
    }
    fn paint_resource(&self, command: &Command, clip: Rect) {
        use wifi_view::*;
        let f = self.frame();
        let lang = unsafe { (*f).language };
        let t = |id| tr(lang, id);
        let mut data = PaintData {
            labels: [c"".as_ptr(); 4],
            kind: DrawKind::Text,
        };
        match command.resource {
            HEADER => {
                data.kind = DrawKind::Header;
                data.labels[0] = t(ids::STR_WIFI_NETWORKS);
                data.labels[1] = unsafe { ptr::addr_of!((*f).count_text).cast() };
            }
            SUBHEADER => {
                data.kind = DrawKind::Subheader;
                data.labels[0] = unsafe { ptr::addr_of!((*f).mac).cast() };
            }
            LIST_FOOTER | SAVED_FOOTER => {
                data.kind = DrawKind::Footer;
                data.labels = [
                    t(ids::STR_BACK),
                    t(ids::STR_CONNECT),
                    if command.resource == SAVED_FOOTER {
                        t(ids::STR_FORGET_BUTTON)
                    } else {
                        c"".as_ptr()
                    },
                    t(ids::STR_RETRY),
                ];
            }
            AUTO_FOOTER => {
                data.kind = DrawKind::Footer;
                data.labels[0] = t(ids::STR_CANCEL);
                data.labels[1] = t(ids::STR_SHOW_NETWORKS);
            }
            SAVE_FOOTER | FORGET_FOOTER => {
                data.kind = DrawKind::Footer;
                data.labels = [
                    t(if command.resource == SAVE_FOOTER {
                        ids::STR_CANCEL
                    } else {
                        ids::STR_BACK
                    }),
                    t(ids::STR_SELECT),
                    t(ids::STR_DIR_LEFT),
                    t(ids::STR_DIR_RIGHT),
                ];
            }
            CONNECTED_FOOTER => {
                data.kind = DrawKind::Footer;
                data.labels[1] = t(ids::STR_DONE);
            }
            FAILED_FOOTER => {
                data.kind = DrawKind::Footer;
                data.labels[0] = t(ids::STR_BACK);
                data.labels[1] = t(ids::STR_DONE);
            }
            LEGEND => {
                data.kind = DrawKind::Help;
                data.labels[0] = t(ids::STR_NETWORK_LEGEND);
            }
            TO_SSID | NETWORK_SSID => {
                let source = unsafe { (*self.owned()).selected.as_bytes() };
                let prefix = bytes(
                    lang,
                    if command.resource == TO_SSID {
                        ids::STR_TO_PREFIX
                    } else {
                        ids::STR_NETWORK_PREFIX
                    },
                );
                let scratch = unsafe { &mut (*f).text };
                let result = text::truncate_joined(
                    scratch,
                    prefix,
                    source,
                    i32::from(command.rect.w),
                    |text| unsafe {
                        (self.physical().width)(
                            self.object(),
                            text.as_ptr().cast(),
                            command.font,
                            0,
                        )
                    },
                );
                if result.is_err() {
                    return;
                }
                data.kind = DrawKind::Centered;
                data.labels[0] = scratch.as_z().as_ptr().cast();
            }
            id => {
                data.kind = match id {
                    SCANNING | FINDING_SAVED => DrawKind::Wrapped,
                    CONNECTING | CONNECTING_SAVED => DrawKind::WrappedBottom,
                    PROMPT_SSID | PROMPT_CONNECTED | PROMPT_FORGET => DrawKind::FlowText,
                    _ if command.max_lines == 0 && command.align == 1 => DrawKind::Centered,
                    _ => DrawKind::Text,
                };
                let Ok(text) = self.text(id) else {
                    return;
                };
                data.labels[0] = text;
                if matches!(data.kind, DrawKind::FlowText) {
                    // Headers have finished; build restores these buffers before the next frame.
                    data.labels[1] = unsafe { ptr::addr_of_mut!((*f).mac).cast() };
                    data.labels[2] = unsafe { ptr::addr_of_mut!((*f).count_text).cast() };
                }
            }
        }
        unsafe { (self.physical().paint)(self.object(), command, clip, &data) };
    }
}
impl<H: Host> Resources for View<'_, H> {
    fn window(&mut self, _: u16, _: u16) -> Result<(), Error> {
        Ok(())
    }
    fn row(&mut self, index: u16) -> Result<ViewRow, Error> {
        let o = self.owner;
        let f = o.frame();
        let capacity = unsafe { (*f).rows.as_slice().len() };
        if capacity == 0 {
            return Err(Error::Capacity);
        }
        let rows = unsafe { (*f).rows.as_mut_slice() };
        let row = &mut rows[usize::from(index) % capacity];
        if row.index != index {
            let source = unsafe { (*o.owned()).rows.as_slice() }
                .get(usize::from(index))
                .ok_or(Error::Invalid)?;
            copy_z(&mut row.ssid, source.ssid());
            row.status = source.status();
            row.hidden = source.is_hidden().into();
            row.index = index;
        }
        let id = u32::from(index) * 4 + 1;
        Ok(ViewRow {
            label: id,
            subtitle: 0,
            value: if row.status[0] == 0 { 0 } else { id + 2 },
            icon: 0,
            toggle: 0,
            checked: 0,
            enabled: 1,
            reserved: 0,
        })
    }
    fn width(&mut self, id: u32, font: i32, bold: u8) -> Result<i32, Error> {
        let o = self.owner;
        let text = o.text(id)?;
        metric(unsafe { (o.physical().width)(o.object(), text, font, bold) })
    }
    fn height(
        &mut self,
        id: u32,
        font: i32,
        bold: u8,
        lines: u8,
        width: i16,
    ) -> Result<i32, Error> {
        let o = self.owner;
        let text = o.text(id)?;
        metric(unsafe { (o.physical().height)(o.object(), text, font, bold, lines, width) })
    }
    fn line(&mut self, font: i32) -> Result<i32, Error> {
        let o = self.owner;
        metric(unsafe { (o.physical().line)(o.object(), font) })
    }
}
pub(super) fn bindings<H: Host>(owner: &Wifi<H>) -> Paint {
    unsafe extern "C" fn valid<H: Host>(context: *mut c_void, id: u32, generation: u32) -> u8 {
        let o = unsafe { &*context.cast::<Wifi<H>>() };
        (generation == 1
            && ((wifi_view::HEADER..=wifi_view::PROMPT_FORGET).contains(&id)
                || !o.row_text(id).is_null()))
        .into()
    }
    unsafe extern "C" fn paint<H: Host>(
        context: *mut c_void,
        _: *const GlyphTarget,
        command: *const Command,
        clip: Rect,
    ) {
        unsafe { &*context.cast::<Wifi<H>>() }.paint_resource(unsafe { &*command }, clip);
    }
    Paint {
        context: (owner as *const Wifi<H>).cast_mut().cast(),
        clear: None,
        text: None,
        resource_valid: Some(valid::<H>),
        resource: Some(paint::<H>),
    }
}
