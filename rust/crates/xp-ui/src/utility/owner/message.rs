extern crate alloc;
use super::{storage::Text, Host, Log, Memory, Resources};
use crate::{
    model,
    model::Input,
    runtime::SceneGate,
    scene::{Command, Rect, Refresh, Scene, MAX_TEXT_BYTES},
};
use alloc::boxed::Box;
use core::{alloc::Layout, cell::UnsafeCell, ptr};
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MessageResult {
    MessageDismissed,
}
pub struct Message<H: Host> {
    host: H,
    text: Text<H::Memory>,
    style: u8,
    refresh: Refresh,
    commands: UnsafeCell<[Command; 2]>,
    gate: UnsafeCell<SceneGate>,
    result: UnsafeCell<Option<MessageResult>>,
    exited: UnsafeCell<bool>,
    dismissed: UnsafeCell<bool>,
}
impl<H: Host> Message<H> {
    pub fn try_new(host: H, text: &[u8], style: u8, refresh: Refresh) -> Option<Box<Self>> {
        let out = H::Memory::owner(Layout::new::<Self>()).cast::<Self>();
        if out.is_null() {
            return None;
        }
        let Some(text) = Text::copy(text) else {
            unsafe { alloc::alloc::dealloc(out.cast(), Layout::new::<Self>()) };
            return None;
        };
        unsafe {
            ptr::addr_of_mut!((*out).host).write(host);
            ptr::addr_of_mut!((*out).text).write(text);
            ptr::addr_of_mut!((*out).style).write(style);
            ptr::addr_of_mut!((*out).refresh).write(refresh);
            ptr::addr_of_mut!((*out).commands).write_bytes(0, 1);
            ptr::addr_of_mut!((*out).gate).write(UnsafeCell::new(SceneGate::default()));
            ptr::addr_of_mut!((*out).result).write(UnsafeCell::new(None));
            ptr::addr_of_mut!((*out).exited).write(UnsafeCell::new(false));
            ptr::addr_of_mut!((*out).dismissed).write(UnsafeCell::new(false));
            Some(Box::from_raw(out))
        }
    }
    pub fn host(&self) -> &H {
        &self.host
    }
    /// Synchronous native onEnter; caller supplies the existing UI_10 font ID.
    pub fn enter(&self, font: i32) -> bool {
        self.host.log(Log::Enter);
        unsafe {
            *self.exited.get() = false;
        }
        let text = self.text.bytes();
        let screen = self.host.layout().screen;
        if text.len() >= MAX_TEXT_BYTES || text.contains(&0) || screen.w <= 0 || screen.h <= 0 {
            self.host.log(Log::MessageBuild);
            return false;
        }
        let commands = self.host.with_metrics(|metrics| {
            let width = metrics.width(font, text, self.style);
            let height = metrics.line_height(font);
            model::message_commands(screen, font, self.style, text.len(), width, height)
        });
        let Ok(commands) = commands else {
            self.host.log(Log::MessageBuild);
            return false;
        };
        unsafe {
            *self.commands.get() = commands;
        }
        let scene = Scene {
            commands: unsafe { &*self.commands.get() },
            text: self.text.terminated(),
            interactions: &[],
            generation: 0,
            refresh: self.refresh,
        };
        if !self.host.paint(&scene, &mut Empty) {
            self.host.log(Log::Paint);
            unsafe { &mut *self.gate.get() }.reject();
            return false;
        }
        unsafe { &mut *self.gate.get() }.publish(true);
        self.host.refresh(self.refresh);
        true
    }
    pub fn input(&self, _: Input) {}
    pub fn render(&self) {}
    /// Called only by an external transition, before result extraction/destruction.
    pub fn dismiss(&self) {
        if !unsafe { *self.dismissed.get() } {
            unsafe {
                *self.dismissed.get() = true;
                *self.result.get() = Some(MessageResult::MessageDismissed);
            }
        }
    }
    pub fn exit(&self) {
        self.dismiss();
        if !unsafe { *self.exited.get() } {
            unsafe {
                *self.exited.get() = true;
            }
            self.host.log(Log::Exit);
        }
    }
    pub fn take_result(&self) -> Option<MessageResult> {
        unsafe { &mut *self.result.get() }.take()
    }
}
struct Empty;
impl Resources for Empty {
    fn valid(&mut self, _: u32, _: u32) -> bool {
        false
    }
    fn paint(&mut self, _: &Command, _: Rect) {}
}
