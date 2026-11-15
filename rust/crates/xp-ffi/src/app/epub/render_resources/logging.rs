use super::renderer::Context;
use core::fmt::{self, Write};
use xp_ui::reader::render::Log;

struct Message {
    bytes: [u8; 384],
    length: usize,
}
impl Write for Message {
    fn write_str(&mut self, text: &str) -> fmt::Result {
        let end = self.length + text.len();
        if end > self.bytes.len() {
            return Err(fmt::Error);
        }
        self.bytes[self.length..end].copy_from_slice(text.as_bytes());
        self.length = end;
        Ok(())
    }
}

// Charge the bounded formatting buffer on its own branch of the render chain.
#[inline(never)]
pub(crate) fn log(
    context: &impl Context,
    kind: Log,
    times: [u32; 10],
    images: bool,
    msb: bool,
    stride: u32,
) {
    let severity = match kind {
        Log::StripOom | Log::AbsoluteFailed | Log::ScratchOom | Log::StoreFailed => 1,
        _ => 0,
    };
    if !context.log_enabled(severity) {
        return;
    }
    let mut message = Message {
        bytes: [0; 384],
        length: 0,
    };
    let delta = |end: usize, start: usize| times[end].wrapping_sub(times[start]);
    let result = match kind {
        Log::StripOom => {
            #[cfg(feature = "reader-strips")]
            { write!(message, "OOM: reader strips ({} bytes); using framebuffer", 28480 + if images { 4096 } else { 0 }) }
            #[cfg(not(feature = "reader-strips"))]
            { let _ = images; return; }
        }
        Log::AbsoluteFailed => message.write_str("Could not start absolute image page; displaying B/W"),
        Log::AbsoluteQuality => message.write_str("UC8279 image page: absolute quality waveform"),
        Log::ScratchOom => write!(message, "OOM: grayscale strip scratch ({} bytes); skipping AA this page", stride.wrapping_mul(80) as i32),
        Log::StoreFailed => message.write_str("Failed to store BW buffer for grayscale render; skipping grayscale this page"),
        Log::TimingAsync => write!(message,
            "Page render (tiled async): prewarm={}ms bw_render={}ms display={}ms gray_render={}ms wait={}ms gray_write={}ms gray_display={}ms cleanup={}ms total={}ms (planes buffered: {})",
            delta(1,0), delta(2,1), delta(3,2), delta(4,3), delta(5,4), delta(6,5), delta(7,6), delta(8,7), delta(8,0), if msb { 2 } else { 1 }),
        Log::TimingTiled => write!(message,
            "Page render (tiled): prewarm={}ms bw_render={}ms display={}ms gray_lsb={}ms gray_msb={}ms gray_display={}ms cleanup={}ms total={}ms",
            delta(1,0), delta(2,1), delta(3,2), delta(4,3), delta(5,4), delta(6,5), delta(7,6), delta(8,0)),
        Log::TimingGray => write!(message,
            "Page render: prewarm={}ms bw_render={}ms display={}ms bw_store={}ms gray_lsb={}ms gray_msb={}ms gray_display={}ms bw_restore={}ms total={}ms",
            delta(1,0), delta(2,1), delta(3,2), delta(4,3), delta(5,4), delta(6,5), delta(7,6), delta(8,7), delta(9,0)),
        Log::TimingBw => write!(message, "Page render: prewarm={}ms bw_render={}ms display={}ms total={}ms", delta(1,0), delta(2,1), delta(3,2), delta(4,0)),
    };
    assert!(result.is_ok());
    context.log(severity, &message.bytes[..message.length]);
}
