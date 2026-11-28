use super::{Message, Phase};
use core::ffi::c_void;
#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Operation {
    Publish,
    Repaint,
    MarkReturn,
    ReturnReader,
    HashPrimary,
    HashAlternate,
    FetchPrimary,
    FetchAlternate,
    AdoptAlternate,
    LoadBook,
    MapPrimary,
    Compare,
    SaveProgress,
    ClipSync,
    UploadBuild,
    UploadMetadata,
    ReleaseBook,
    UploadSend,
    RadioStop,
    AuthRequest,
    Diagnostic,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub context: *mut c_void,
    pub call: Option<unsafe extern "C" fn(*mut c_void, u32, u32, u32) -> u32>,
}
impl Services {
    fn call(&self, op: Operation, a: u32, b: u32) -> u32 {
        self.call
            .map_or(0, |f| unsafe { f(self.context, op as u32, a, b) })
    }
    fn state(&self, phase: Phase, message: Message) {
        self.call(Operation::Publish, phase as u32, message as u32);
    }
    fn repaint(&self, mode: u32) {
        self.call(Operation::Repaint, mode, 0);
    }
    fn error(&self, result: u32, refresh: u32) {
        self.call(
            Operation::Publish,
            Phase::Failed as u32,
            (result << 8) | Message::ServiceError as u32,
        );
        self.repaint(refresh);
    }
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Options {
    pub local_percentage: f32,
    pub smart: u8,
    pub clippings: u8,
    pub rich: u8,
    pub metadata: u8,
    pub extended: u8,
    pub signup: u8,
    pub reserved: [u8; 2],
}
// The C++ owner rejects same-activity callback reentry on the activity task.
fn complete(s: &Services) {
    s.state(Phase::Synchronized, Message::None);
    s.call(Operation::MarkReturn, 0, 0);
    s.repaint(1);
}
fn save_inner(s: &Services) {
    if s.call(Operation::SaveProgress, 0, 0) == 0 {
        s.state(Phase::Failed, Message::SaveFailed);
        s.repaint(1);
    } else {
        s.call(Operation::ReturnReader, 0, 0);
    }
}
fn upload_inner(o: Options, s: &Services) {
    s.state(Phase::Uploading, Message::Upload);
    s.repaint(2);
    // Preserve the C++ float evaluation order and saturating boundary checks.
    let percentage = o.local_percentage.clamp(0.0, 1.0);
    let quantized = (percentage * 1_000_000.0 + 0.5) as u32;
    s.call(Operation::UploadBuild, u32::from(o.rich), quantized);
    if o.metadata != 0 {
        s.call(Operation::UploadMetadata, u32::from(o.extended), 0);
    }
    s.call(Operation::ReleaseBook, 0, 0);
    let result = s.call(Operation::UploadSend, 0, 0);
    s.call(Operation::RadioStop, 0, 0);
    if result != 0 {
        s.error(result, 0);
        return;
    }
    s.state(Phase::Uploaded, Message::None);
    s.call(Operation::MarkReturn, 0, 0);
    s.repaint(1);
}
pub fn upload(o: Options, s: Services) {
    upload_inner(o, &s);
}
pub fn save(s: Services) {
    save_inner(&s);
}
pub fn sync(o: Options, s: Services) {
    if s.call(Operation::HashPrimary, 0, 0) == 0 {
        s.state(Phase::Failed, Message::HashFailed);
        s.repaint(1);
        return;
    }
    if o.clippings != 0 {
        s.state(Phase::Syncing, Message::Clippings);
        s.repaint(2);
        if s.call(Operation::ClipSync, 0, 0) == 0 {
            s.state(Phase::Failed, Message::ClippingsFailed);
            s.repaint(1);
            return;
        }
    }
    s.state(Phase::Syncing, Message::Fetch);
    s.repaint(2);
    let mut result = s.call(Operation::FetchPrimary, 0, 0);
    let mut alternate = false;
    if o.smart != 0 && s.call(Operation::HashAlternate, 0, 0) != 0 {
        alternate = s.call(Operation::FetchAlternate, 0, 0) == 0;
    }
    if result == 6 && alternate {
        s.call(Operation::AdoptAlternate, 0, 0);
        alternate = false;
        result = 0;
    }
    if result == 6 {
        if o.smart != 0 {
            s.call(Operation::Diagnostic, 0, 0);
            upload_inner(o, &s);
        } else {
            s.state(Phase::Missing, Message::None);
            s.repaint(1);
        }
        return;
    }
    if result != 0 {
        s.error(result, 1);
        return;
    }
    if s.call(Operation::LoadBook, 0, 0) == 0 {
        s.state(Phase::Failed, Message::None);
        s.repaint(1);
        return;
    }
    // Mapping stays inside the original single framebuffer loan. The callback
    // maps both borrowed records and selects through the existing Rust service.
    s.call(Operation::MapPrimary, u32::from(alternate), 0);
    let comparison = s.call(Operation::Compare, 0, 0);
    if o.smart != 0 {
        s.call(Operation::Diagnostic, 1, comparison);
        match comparison {
            0 => {
                upload_inner(o, &s);
                return;
            }
            1 => {
                complete(&s);
                return;
            }
            2 => {
                save_inner(&s);
                return;
            }
            _ => {
                s.call(Operation::Diagnostic, 2, 0);
            }
        }
    }
    s.call(
        Operation::Publish,
        Phase::Result as u32,
        u32::from(comparison == 0) << 16,
    );
    s.repaint(1);
}
pub fn auth(signup: u8, s: Services) {
    let result = s.call(Operation::AuthRequest, u32::from(signup), 0);
    if result == 0 {
        s.state(Phase::Authenticated, Message::AuthOk);
    } else if result == 8 {
        s.state(Phase::Failed, Message::UserExists);
    } else {
        s.call(
            Operation::Publish,
            Phase::Failed as u32,
            (result << 8) | Message::ServiceError as u32,
        );
    }
    s.repaint(0);
}
