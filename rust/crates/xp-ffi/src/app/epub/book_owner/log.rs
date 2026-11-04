use super::{app_abi, XpEpubLeaves};
use core::fmt::{self, Write};

pub(in crate::app::epub) fn message(
    leaves: &XpEpubLeaves,
    severity: u8,
    origin: &[u8],
    arguments: fmt::Arguments<'_>,
) {
    with_bytes(leaves, severity, origin, &[], arguments);
}

pub(super) fn with_bytes(
    leaves: &XpEpubLeaves,
    severity: u8,
    origin: &[u8],
    parts: &[&[u8]],
    arguments: fmt::Arguments<'_>,
) {
    if unsafe { (leaves.log_enabled)(severity) } != 0 {
        formatted(leaves, severity, origin, parts, arguments, &[]);
    }
}

pub(super) fn with_tail(
    leaves: &XpEpubLeaves,
    severity: u8,
    origin: &[u8],
    arguments: fmt::Arguments<'_>,
    tail: &[&[u8]],
) {
    if unsafe { (leaves.log_enabled)(severity) } != 0 {
        formatted(leaves, severity, origin, &[], arguments, tail);
    }
}

// Charge this bounded buffer and the native logger together to the calling task.
#[inline(never)]
fn formatted(
    leaves: &XpEpubLeaves,
    severity: u8,
    origin: &[u8],
    parts: &[&[u8]],
    arguments: fmt::Arguments<'_>,
    tail: &[&[u8]],
) {
    struct Message {
        bytes: [u8; 256],
        length: usize,
    }
    impl Message {
        fn append(&mut self, bytes: &[u8]) {
            let count = bytes.len().min(self.bytes.len() - self.length);
            self.bytes[self.length..self.length + count].copy_from_slice(&bytes[..count]);
            self.length += count;
        }
    }
    impl Write for Message {
        fn write_str(&mut self, text: &str) -> fmt::Result {
            self.append(text.as_bytes());
            Ok(())
        }
    }
    let mut message = Message {
        bytes: [0; 256],
        length: 0,
    };
    for part in parts {
        message.append(part);
    }
    let _ = message.write_fmt(arguments);
    for part in tail {
        message.append(part);
    }
    unsafe {
        (leaves.log)(
            severity,
            app_abi::Text::from_bytes(origin),
            app_abi::Text::from_bytes(&message.bytes[..message.length]),
        );
    }
}
