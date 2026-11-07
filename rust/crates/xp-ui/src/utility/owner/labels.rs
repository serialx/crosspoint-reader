use super::{
    storage::{Memory, Text},
    Host, Resources,
};
use crate::{
    scene::{Command, Error, Rect},
    utility::{self as u, Screen, State, Texts},
};
use core::ffi::CStr;
use xp_core::i18n::ids::*;
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ConfirmationResult {
    Confirmed,
    Cancelled,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum IntervalResult {
    Interval(u32),
    Cancelled,
}
#[derive(Clone, Copy)]
pub enum Label {
    Translation(u16),
    Literal(&'static CStr),
}
mod sealed {
    pub trait Sealed {}
}
pub trait Spec: sealed::Sealed {
    type Result;
    type Buffers;
    const COMMANDS: u16;
    const HITS: u16;
    const IMMEDIATE: bool;
    fn state(&self) -> State;
    fn result(cancelled: bool, value: i32) -> Self::Result;
    fn reader(&self) -> bool {
        false
    }
    fn prepare<H: Host>(&self, _: &H, _: &State, _: &mut Self::Buffers) {}
    fn text<H: Host, R>(
        &self,
        host: &H,
        buffers: &Self::Buffers,
        handle: u32,
        consume: impl FnOnce(&CStr) -> R,
    ) -> R;
    fn hints() -> [Label; 4];
}
pub struct Confirmation<M: Memory> {
    texts: [Text<M>; 4],
}
impl<M: Memory> sealed::Sealed for Confirmation<M> {}
impl<M: Memory> Confirmation<M> {
    pub(super) fn copy(heading: &[u8], body: &[u8], cancel: &[u8], confirm: &[u8]) -> Option<Self> {
        Some(Self {
            texts: [
                Text::copy(heading)?,
                Text::copy(body)?,
                Text::copy(cancel)?,
                Text::copy(confirm)?,
            ],
        })
    }
}
impl<M: Memory> Spec for Confirmation<M> {
    type Result = ConfirmationResult;
    type Buffers = ();
    const COMMANDS: u16 = 10;
    const HITS: u16 = 3;
    const IMMEDIATE: bool = true;
    fn state(&self) -> State {
        State {
            kind: Screen::Confirmation as u8,
            ..Default::default()
        }
    }
    fn result(cancelled: bool, _: i32) -> Self::Result {
        if cancelled {
            ConfirmationResult::Cancelled
        } else {
            ConfirmationResult::Confirmed
        }
    }
    fn text<H: Host, R>(
        &self,
        host: &H,
        _: &Self::Buffers,
        handle: u32,
        f: impl FnOnce(&CStr) -> R,
    ) -> R {
        if (u::TITLE..=u::CONFIRM).contains(&handle) {
            let text = c_text(self.texts[(handle - u::TITLE) as usize].terminated());
            if !text.is_empty() || handle < u::CANCEL {
                return f(text);
            }
        }
        default_text(host, handle, f)
    }
    fn hints() -> [Label; 4] {
        [
            Label::Translation(STR_BACK),
            Label::Translation(STR_SELECT),
            Label::Translation(STR_DIR_UP),
            Label::Translation(STR_DIR_DOWN),
        ]
    }
}
#[derive(Clone, Copy)]
pub struct Interval {
    pub title: u16,
    pub format: Option<u16>,
    pub boundary: Option<u16>,
    pub initial: i32,
    pub minimum: i32,
    pub maximum: i32,
    pub small_step: i32,
    pub large_step: i32,
    pub edge_buttons: bool,
    pub reader: bool,
}
impl sealed::Sealed for Interval {}
impl Spec for Interval {
    type Result = IntervalResult;
    type Buffers = Buffers;
    const COMMANDS: u16 = 18;
    const HITS: u16 = 5;
    const IMMEDIATE: bool = false;
    fn state(&self) -> State {
        State {
            kind: Screen::Interval as u8,
            value: self.initial,
            minimum: self.minimum,
            maximum: self.maximum,
            small_step: self.small_step,
            large_step: self.large_step,
            edge_buttons: self.edge_buttons as u8,
            ..Default::default()
        }
    }
    fn result(cancelled: bool, value: i32) -> Self::Result {
        if cancelled {
            IntervalResult::Cancelled
        } else {
            IntervalResult::Interval(value as u32)
        }
    }
    fn reader(&self) -> bool {
        self.reader
    }
    fn prepare<H: Host>(&self, host: &H, state: &State, out: &mut Self::Buffers) {
        for (buffer, value) in [
            (&mut out.readout[..], state.value),
            (&mut out.minimum[..], state.minimum),
            (&mut out.maximum[..], state.maximum),
        ] {
            if let Some(key) = self.boundary.filter(|_| value == state.maximum) {
                host.with_translation(key, |text| copy(buffer, text.to_bytes()));
            } else {
                host.format_number(
                    self.format.unwrap_or(STR_NONE_OPT),
                    value,
                    self.format.is_some(),
                    buffer,
                );
            }
        }
        for (i, step) in [state.small_step, state.large_step].into_iter().enumerate() {
            let mut text = [0; 24];
            host.format_number(
                self.format.unwrap_or(STR_NONE_OPT),
                step,
                self.format.is_some(),
                &mut text,
            );
            host.with_translation(
                if i == 0 {
                    STR_STEP_HINT_FRONT
                } else {
                    STR_STEP_HINT_SIDE
                },
                |prefix| {
                    let target = &mut out.hints[i];
                    let n = append(target, 0, prefix.to_bytes());
                    let n = append(target, n, b" ");
                    append(target, n, c_text(&text).to_bytes());
                },
            );
        }
    }
    fn text<H: Host, R>(
        &self,
        host: &H,
        b: &Self::Buffers,
        handle: u32,
        f: impl FnOnce(&CStr) -> R,
    ) -> R {
        match handle {
            u::TITLE => host.with_translation(self.title, f),
            u::READOUT => f(c_text(&b.readout)),
            u::MINIMUM => f(c_text(&b.minimum)),
            u::MAXIMUM => f(c_text(&b.maximum)),
            u::HINT1 => f(c_text(&b.hints[0])),
            u::HINT2 => f(c_text(&b.hints[1])),
            _ => default_text(host, handle, f),
        }
    }
    fn hints() -> [Label; 4] {
        [
            Label::Translation(STR_BACK),
            Label::Translation(STR_SELECT),
            Label::Literal(c"-"),
            Label::Literal(c"+"),
        ]
    }
}
#[doc(hidden)]
pub struct Buffers {
    readout: [u8; 64],
    hints: [[u8; 64]; 2],
    minimum: [u8; 32],
    maximum: [u8; 32],
}
fn c_text(text: &[u8]) -> &CStr {
    CStr::from_bytes_until_nul(text).expect("terminated dialog text")
}
fn append(out: &mut [u8], start: usize, text: &[u8]) -> usize {
    let n = text.len().min(out.len() - 1 - start);
    out[start..start + n].copy_from_slice(&text[..n]);
    out[start + n] = 0;
    start + n
}
fn copy(out: &mut [u8], text: &[u8]) {
    append(out, 0, text);
}
fn default_text<H: Host, R>(host: &H, handle: u32, f: impl FnOnce(&CStr) -> R) -> R {
    match handle {
        u::CANCEL => host.with_translation(STR_CANCEL, f),
        u::CONFIRM => host.with_translation(STR_CONFIRM, f),
        u::MINUS => f(c"-"),
        u::PLUS => f(c"+"),
        _ => f(c""),
    }
}
pub(super) struct View<'a, H: Host, S: Spec> {
    pub(super) host: &'a H,
    pub(super) spec: &'a S,
    pub(super) buffers: &'a S::Buffers,
}
impl<H: Host, S: Spec> Texts for View<'_, H, S> {
    fn present(&self, h: u32) -> Result<bool, Error> {
        if !(u::TITLE..=u::PLUS).contains(&h) {
            return Err(Error::Invalid);
        }
        Ok(self
            .spec
            .text(self.host, self.buffers, h, |s| !s.is_empty()))
    }
    fn height(&self, h: u32, font: i32, width: i16, style: u8, lines: u8) -> Result<i16, Error> {
        self.present(h)?;
        let height = self.spec.text(self.host, self.buffers, h, |text| {
            self.host.text_height(text, font, width, style, lines)
        });
        if (0..=4096).contains(&height) {
            Ok(height)
        } else {
            Err(Error::Invalid)
        }
    }
}
impl<H: Host, S: Spec> Resources for View<'_, H, S> {
    fn valid(&mut self, h: u32, g: u32) -> bool {
        g == 1 && ((u::TITLE..=u::PLUS).contains(&h) || h == u::FOOTER)
    }
    fn paint(&mut self, c: &Command, clip: Rect) {
        if c.resource == u::FOOTER {
            self.host.footer(c, clip, S::hints());
        } else {
            self.spec.text(self.host, self.buffers, c.resource, |text| {
                self.host.text(c, clip, text)
            });
        }
    }
}
