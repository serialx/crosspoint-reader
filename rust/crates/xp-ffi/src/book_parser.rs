//! Incremental chapter file lifecycle over borrowed parser/file/buffer owners.
use core::ffi::c_void;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    ctx: *mut c_void,
    operation: Option<unsafe extern "C" fn(*mut c_void, u32, u32) -> u32>,
    millis: Option<unsafe extern "C" fn(*mut c_void) -> u32>,
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32)>,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum Op {
    Ensure,
    BeginAssembly,
    Oom,
    AllocateBuffer,
    SetCapacity,
    Open,
    HasPopup,
    Size,
    Popup,
    StartClock,
    Stopped,
    Read,
    Available,
    Feed,
    FeedError,
    Destroy,
    ReleaseBuffer,
    IsOpen,
    Close,
    HasChapter,
    FinishAssembly,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum Event {
    ParserOom,
    BufferOom,
    LayoutOom,
    ReadError,
    ParseError,
    Time,
    LayoutAfterFeed,
    LayoutBeforeFinish,
    LayoutAfterFinish,
}
impl Services {
    fn call(&self, op: Op, arg: u32) -> u32 {
        self.operation
            .map_or(0, |call| unsafe { call(self.ctx, op as u32, arg) })
    }
    fn get(&self, op: Op) -> u32 {
        self.call(op, 0)
    }
    fn log(&self, event: Event) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, event as u32, 0) };
        }
    }
}
fn abort(s: &Services) {
    use Op::*;
    if s.get(HasChapter) != 0 {
        s.get(Destroy);
    }
    s.get(ReleaseBuffer);
    s.call(SetCapacity, 0);
    if s.get(IsOpen) != 0 {
        s.get(Close);
    }
}
fn begin(s: &Services) -> bool {
    use Op::*;
    if s.get(Ensure) == 0 {
        s.log(Event::ParserOom);
        return false;
    }
    s.get(BeginAssembly);
    if s.get(Oom) != 0 {
        abort(s);
        return false;
    }
    if s.call(AllocateBuffer, 1024) == 0 {
        s.log(Event::BufferOom);
        abort(s);
        return false;
    }
    s.call(SetCapacity, 1024);
    if s.get(Open) == 0 {
        abort(s);
        return false;
    }
    if s.get(HasPopup) != 0 && s.get(Size) >= 10 * 1024 {
        s.get(Popup);
    }
    s.get(StartClock);
    true
}
fn step(s: &Services) -> u32 {
    use Op::*;
    if s.get(Stopped) != 0 {
        return 2;
    }
    if s.get(Oom) != 0 {
        s.log(Event::LayoutOom);
        return 2;
    }
    if s.call(Read, 1024) == 0 && s.get(Available) > 0 {
        s.log(Event::ReadError);
        return 2;
    }
    let done = s.get(Available) == 0;
    let status = s.call(Feed, done as u32);
    if s.get(FeedError) != 0 {
        s.log(Event::ParseError);
    } else if status == 2 && s.get(Oom) != 0 {
        s.log(Event::LayoutAfterFeed);
    }
    match status {
        0 | 1 => status,
        _ => 2,
    }
}
fn finish(s: &Services) -> bool {
    use Op::*;
    if s.get(Stopped) != 0 {
        return false;
    }
    if s.get(Oom) != 0 {
        s.log(Event::LayoutBeforeFinish);
        return false;
    }
    if s.get(HasChapter) != 0 {
        s.log(Event::Time);
    }
    s.get(ReleaseBuffer);
    s.call(SetCapacity, 0);
    s.get(Close);
    let success = s.get(FinishAssembly) != 0;
    if s.get(Stopped) == 0 && s.get(Oom) != 0 {
        s.log(Event::LayoutAfterFinish);
    }
    s.get(Destroy);
    success
}
/// # Safety
/// Services own their context and all parser state for the synchronous call.
/// Feed may invoke separate callbacks; none may reenter this lifecycle.
#[no_mangle]
pub unsafe extern "C" fn xp_book_parse(services: *const Services, action: u32) -> u32 {
    let Some(s) = services.as_ref().copied().filter(|s| s.operation.is_some()) else {
        return 0;
    };
    match action {
        0 => begin(&s) as u32,
        1 => step(&s),
        2 => {
            abort(&s);
            0
        }
        3 => finish(&s) as u32,
        4 => {
            if !begin(&s) {
                return 0;
            }
            loop {
                match step(&s) {
                    2 => {
                        abort(&s);
                        return 0;
                    }
                    1 => break,
                    _ => {}
                }
            }
            finish(&s) as u32
        }
        _ => 0,
    }
}

#[repr(u32)]
#[derive(Clone, Copy)]
enum ProbeOp {
    Resolve,
    Supported,
    PrepareCache,
    Probe,
    RetryProbe,
    HasPopup,
    PopupShown,
    SetPopup,
    Popup,
    Open,
    Extract,
    Flush,
    Close,
    Decoder,
    Dimensions,
    Delay,
    StoreSize,
    Accept,
    Remove,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum ProbeEvent {
    Found,
    ExtractFailed,
    Dimensions,
    DimensionsFailed,
}
impl Services {
    fn probe(&self, op: ProbeOp, arg: u32) -> u32 {
        self.operation
            .map_or(0, |call| unsafe { call(self.ctx, op as u32, arg) })
    }
    fn probe_log(&self, event: ProbeEvent) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, event as u32, 0) };
        }
    }
}
/// # Safety
/// Services exclusively own the temporary probes/file/renderer loans until return.
#[no_mangle]
pub unsafe extern "C" fn xp_book_probe_image(services: *const Services) -> u8 {
    use ProbeOp::*;
    let Some(s) = services.as_ref().copied().filter(|s| s.operation.is_some()) else {
        return 0;
    };
    let get = |op| s.probe(op, 0);
    s.probe_log(ProbeEvent::Found);
    get(Resolve);
    if get(Supported) == 0 {
        return 0;
    }
    get(PrepareCache);
    let mut dimensions = get(Probe) != 0;
    if !dimensions {
        dimensions = get(RetryProbe) != 0;
    }
    if !dimensions {
        if get(HasPopup) != 0 && get(PopupShown) == 0 {
            get(SetPopup);
            get(Popup);
        }
        let mut extracted = false;
        if get(Open) != 0 {
            extracted = get(Extract) != 0;
            get(Flush);
            get(Close);
        }
        if extracted {
            let decoder = get(Decoder) != 0;
            for attempt in 0..3 {
                if attempt > 0 {
                    s.probe(Delay, 50);
                }
                dimensions = decoder && get(Dimensions) != 0;
                if dimensions {
                    break;
                }
            }
        } else {
            s.probe_log(ProbeEvent::ExtractFailed);
        }
    }
    get(StoreSize);
    if dimensions {
        s.probe_log(ProbeEvent::Dimensions);
        get(Accept);
        return 2;
    }
    s.probe_log(ProbeEvent::DimensionsFailed);
    get(Remove);
    1
}

#[repr(u32)]
#[derive(Clone, Copy)]
enum JpegOp {
    Heap,
    Allocate,
    Open,
    Validate,
    Progressive,
    Drawing,
    PipelineInit,
    Prepare,
    Denominator,
    Configure,
    HasCache,
    BeginCache,
    BeginTime,
    Decode,
    EndTime,
    Finish,
    Success,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum JpegEvent {
    Heap,
    DimensionAllocation,
    DimensionOpen,
    Dimensions,
    Render,
    Allocation,
    Open,
    Progressive,
    Cache,
    Decode,
    Time,
    DimensionHeap,
}
impl Services {
    fn jpeg(&self, op: JpegOp, arg: u32) -> u32 {
        self.operation
            .map_or(0, |call| unsafe { call(self.ctx, op as u32, arg) })
    }
    fn jpeg_log(&self, event: JpegEvent, arg: u32) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, event as u32, arg) };
        }
    }
}
/// # Safety
/// Services own JPEGDEC and pipeline lifetimes until return; decoder close precedes pipeline drop.
#[no_mangle]
pub unsafe extern "C" fn xp_book_jpeg(services: *const Services, render: u8) -> u8 {
    use JpegOp::*;
    let Some(s) = services.as_ref().copied().filter(|s| s.operation.is_some()) else {
        return 0;
    };
    let get = |op| s.jpeg(op, 0);
    let render = render != 0;
    if render {
        s.jpeg_log(JpegEvent::Render, 0);
    }
    let heap = get(Heap);
    if heap < 36 * 1024 {
        s.jpeg_log(
            if render {
                JpegEvent::Heap
            } else {
                JpegEvent::DimensionHeap
            },
            heap,
        );
        return 0;
    }
    if get(Allocate) == 0 {
        s.jpeg_log(
            if render {
                JpegEvent::Allocation
            } else {
                JpegEvent::DimensionAllocation
            },
            0,
        );
        return 0;
    }
    if render {
        get(Drawing);
    }
    if s.jpeg(Open, render as u32) == 0 {
        s.jpeg_log(
            if render {
                JpegEvent::Open
            } else {
                JpegEvent::DimensionOpen
            },
            0,
        );
        return 0;
    }
    if get(Validate) == 0 {
        return 0;
    }
    if !render {
        s.jpeg_log(JpegEvent::Dimensions, 0);
        return 1;
    }
    let progressive = get(Progressive);
    if progressive != 0 {
        s.jpeg_log(JpegEvent::Progressive, 0);
    }
    if s.jpeg(PipelineInit, progressive) == 0 || get(Prepare) == 0 {
        return 0;
    }
    s.jpeg(Configure, get(Denominator));
    if get(HasCache) != 0 && get(BeginCache) == 0 {
        s.jpeg_log(JpegEvent::Cache, 0);
    }
    get(BeginTime);
    let decoded = get(Decode) != 0;
    get(EndTime);
    if !decoded {
        s.jpeg_log(JpegEvent::Decode, 0);
        return 0;
    }
    s.jpeg_log(JpegEvent::Time, 0);
    get(Finish);
    (get(Success) != 0) as u8
}
