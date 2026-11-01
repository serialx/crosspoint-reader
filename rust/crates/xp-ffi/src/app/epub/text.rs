use super::{app_abi, EpubSession};
use core::fmt;

pub(super) fn sanitize_summary(input: &[u8], output: &mut [u8; 72]) -> usize {
    let mut previous_space = false;
    let mut length = 0;
    let mut trimmed_length = 0;
    for &byte in input {
        let space = matches!(byte, b'\t'..=b'\r' | b' ');
        if previous_space && space {
            continue;
        }
        previous_space = space;
        if byte == b'\n' || (length == 0 && space) {
            continue;
        }
        if length < output.len() {
            output[length] = byte;
            length += 1;
        }
        if !space {
            trimmed_length = length;
        }
    }
    trimmed_length
}

struct LogText {
    bytes: [u8; 256],
    len: usize,
}

impl LogText {
    fn append(&mut self, bytes: &[u8]) {
        let count = bytes.len().min(self.bytes.len() - self.len);
        self.bytes[self.len..self.len + count].copy_from_slice(&bytes[..count]);
        self.len += count;
    }
}

impl fmt::Write for LogText {
    fn write_str(&mut self, text: &str) -> fmt::Result {
        self.append(text.as_bytes());
        Ok(())
    }
}

impl EpubSession {
    pub(super) fn loop_log_parts(&self, severity: u8, parts: &[&[u8]]) {
        self.loop_log_at(severity, b"ERS", parts);
    }

    #[inline(never)]
    pub(super) fn loop_log_at(&self, severity: u8, origin: &[u8], parts: &[&[u8]]) {
        let mut text = LogText {
            bytes: [0; 256],
            len: 0,
        };
        for part in parts {
            text.append(part);
        }
        unsafe {
            (self.leaves.log)(
                severity,
                app_abi::Text::from_bytes(origin),
                app_abi::Text::from_bytes(&text.bytes[..text.len]),
            );
        }
    }

    // This formatter is used only on the loop task. The native log prefix reduces the
    // visible message below this capacity; truncation remains native-owned.
    #[inline(never)]
    pub(super) fn loop_log(&self, prefix: fmt::Arguments<'_>, suffix: &[u8]) {
        let mut text = LogText {
            bytes: [0; 256],
            len: 0,
        };
        let _ = fmt::write(&mut text, prefix);
        text.append(suffix);
        unsafe {
            (self.leaves.log)(
                0,
                app_abi::Text::from_bytes(b"ERS"),
                app_abi::Text::from_bytes(&text.bytes[..text.len]),
            );
        }
    }
}
