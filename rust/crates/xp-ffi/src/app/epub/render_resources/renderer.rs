//! Dormant implementation of the existing reader command stream.
use super::{abi, physical, FrameLease, PageGeometry, PageResources, PlaneBuffer};
use core::pin::Pin;
#[cfg(feature = "reader-strips")]
use core::{ffi::c_void, ptr};
use xp_ui::reader::render::{self, Buffer, Command, Plane, Query, Renderer};

pub(crate) trait Context {
    fn now(&self) -> u32;
    fn has_images(&self) -> bool;
    fn images_need_decode(&self) -> bool;
    fn elements(&self) -> u16;
    fn take_forced(&self) -> bool;
    fn counter(&self) -> u32;
    fn set_counter(&self, value: u32);
    fn frequency(&self) -> u32;
    fn text_aa(&self) -> bool;
    fn is_a4(&self) -> bool;
    fn image_reset(&self);
    fn paint(&self, mode: u8);
    fn highlights(&self);
    fn status(&self);
    fn log_enabled(&self, severity: u8) -> bool;
    fn log(&self, severity: u8, bytes: &[u8]);
}

// Separate from FrameLease: detach may re-enter this context during its Drop.
struct Replay<'a, P, C> {
    owner: Pin<&'a PageResources>,
    physical: &'a P,
    context: &'a C,
}

struct Call<'a, P, C> {
    frame: FrameLease<'a>,
    replay: &'a Replay<'a, P, C>,
    resources: &'static abi::Resources,
}

pub(crate) fn run<P: physical::Physical, C: Context>(
    owner: Pin<&PageResources>,
    arena: &[u8],
    physical: &P,
    context: &C,
    resources: &'static abi::Resources,
    geometry: PageGeometry,
    strips: bool,
) {
    let replay = core::pin::pin!(Replay {
        owner,
        physical,
        context
    });
    let frame = owner.begin_frame(arena, physical);
    frame.configure(geometry);
    let call = Call {
        frame,
        replay: &replay,
        resources,
    };
    render::render(&call, strips);
    // The pinned callback context outlives the lease's final detach callback.
    drop(call);
}

fn buffer(value: Buffer) -> PlaneBuffer {
    match value {
        Buffer::Lsb => PlaneBuffer::Lsb,
        Buffer::Msb => PlaneBuffer::Msb,
        Buffer::Scratch => PlaneBuffer::Scratch,
    }
}
fn plane(value: Plane) -> u8 {
    match value {
        Plane::Bw => 0,
        Plane::Lsb => 1,
        Plane::Msb => 2,
    }
}

#[cfg(feature = "reader-strips")]
unsafe extern "C" fn paint<P: physical::Physical, C: Context>(
    raw: *mut c_void,
    scratch: *mut u8,
    y: u16,
    rows: u16,
) -> u32 {
    let replay = unsafe { &*raw.cast::<Replay<'_, P, C>>() };
    unsafe {
        replay.owner.replay(
            replay.physical,
            scratch,
            y,
            rows,
            || replay.context.highlights(),
            || replay.context.paint(0),
        )
    }
}

impl<P: physical::Physical, C: Context> Renderer for Call<'_, P, C> {
    fn query(&self, query: Query) -> u32 {
        let context = self.replay.context;
        let physical = match query {
            Query::Now => return context.now(),
            Query::HasImages => {
                let frame = &self.frame.owner.frame;
                let images = context.has_images();
                frame.page.set(PageGeometry {
                    images,
                    ..frame.page.get()
                });
                return u32::from(images);
            }
            Query::ImagesNeedDecode => return u32::from(context.images_need_decode()),
            Query::TakeForced => return u32::from(context.take_forced()),
            Query::Counter => return context.counter(),
            Query::Frequency => return context.frequency(),
            Query::TextAa => return u32::from(context.text_aa()),
            Query::IsA4 => return u32::from(context.is_a4()),
            Query::Elements => return u32::from(context.elements()),
            Query::IsBw => physical::Query::IsBw,
            Query::IsX3 => physical::Query::IsX3,
            Query::IsUc8279 => physical::Query::IsUc8279,
            Query::AbsoluteSupported => physical::Query::AbsoluteSupported,
            Query::OverlayCaps => physical::Query::OverlayCaps,
            Query::AbsoluteCaps => physical::Query::AbsoluteCaps,
            Query::StripBoard => physical::Query::StripBoard,
            Query::WidthBytes => physical::Query::WidthBytes,
            Query::Height => physical::Query::Height,
            Query::FreeHeap => physical::Query::FreeHeap,
            Query::MaxBlock => physical::Query::MaxBlock,
        };
        self.replay.physical.query(physical)
    }

    fn execute(&self, command: Command) -> u32 {
        let physical = self.replay.physical;
        let context = self.replay.context;
        let mut op = physical::Op::new(physical::Kind::Clear);
        op.kind = match command {
            Command::ImageReset => {
                context.image_reset();
                return 0;
            }
            Command::PrewarmBegin => {
                self.frame.begin_prewarm();
                return 0;
            }
            Command::PrewarmEnd => {
                self.frame.finish_scan();
                return 0;
            }
            Command::Page => {
                context.paint(0);
                return 0;
            }
            Command::Images => {
                context.paint(1);
                return 0;
            }
            Command::Placeholders => {
                context.paint(2);
                return 0;
            }
            Command::Highlights => {
                context.highlights();
                return 0;
            }
            Command::Status => {
                context.status();
                return 0;
            }
            Command::SetCounter(value) => {
                context.set_counter(value);
                return 0;
            }
            Command::Allocate { buffer: id, bytes } => {
                return u32::from(
                    self.frame
                        .allocate(self.resources, buffer(id), bytes as usize),
                );
            }
            Command::BorrowScratch => {
                #[cfg(feature = "reader-strips")]
                return u32::from(self.frame.borrow_scratch());
                #[cfg(not(feature = "reader-strips"))]
                return 0;
            }
            Command::PrepareStrips { images } => {
                #[cfg(feature = "reader-strips")]
                return u32::from(self.frame.owner.prepare_strips(
                    self.resources,
                    physical.query(physical::Query::WidthBytes) as usize,
                    images,
                ));
                #[cfg(not(feature = "reader-strips"))]
                {
                    let _ = images;
                    return 0;
                }
            }
            Command::ImageWindow => {
                #[cfg(feature = "reader-strips")]
                {
                    let bytes = self.frame.owner.strips.image_window.pointer();
                    physical.image_window(bytes, if bytes.is_null() { 0 } else { 4096 });
                    self.frame.window_installed();
                }
                return 0;
            }
            Command::RecordBegin => {
                #[cfg(feature = "reader-strips")]
                {
                    let owner = self.frame.owner;
                    let plan = owner
                        .frame
                        .strips
                        .plan
                        .get()
                        .cast::<crate::page_render::Plan>();
                    unsafe {
                        plan.write(crate::page_render::Plan {
                            storage: owner.strips.commands.pointer().cast(),
                            capacity_bytes: 16384,
                            count: 0,
                            valid: 0,
                        });
                    }
                    physical.plan(
                        plan,
                        owner.strips.bounds.pointer(),
                        context.elements(),
                        true,
                    );
                    self.frame.plan_installed();
                }
                return 0;
            }
            Command::RecordEnd => {
                #[cfg(feature = "reader-strips")]
                {
                    let owner = self.frame.owner;
                    let plan = owner
                        .frame
                        .strips
                        .plan
                        .get()
                        .cast::<crate::page_render::Plan>();
                    let valid = unsafe { (*plan).valid } != 0;
                    physical.plan(
                        if valid { plan } else { ptr::null_mut() },
                        owner.strips.bounds.pointer(),
                        context.elements(),
                        false,
                    );
                    return u32::from(valid);
                }
                #[cfg(not(feature = "reader-strips"))]
                return 0;
            }
            Command::SetSource { scratch_bytes } => {
                #[cfg(feature = "reader-strips")]
                {
                    let owner = self.frame.owner;
                    let source = owner
                        .frame
                        .strips
                        .source
                        .get()
                        .cast::<crate::display::DisplaySource>();
                    unsafe {
                        source.write(crate::display::DisplaySource {
                            struct_size: core::mem::size_of::<crate::display::DisplaySource>()
                                as u32,
                            ctx: (self.replay as *const Replay<'_, P, C>).cast_mut().cast(),
                            scratch: owner.strips.scratch.pointer(),
                            capacity: scratch_bytes,
                            rows: 80,
                            width: physical.query(physical::Query::Width) as u16,
                            paint: Some(paint::<P, C>),
                        });
                    }
                    let installed = physical.source(source);
                    self.frame.source_installed(installed);
                    return u32::from(installed);
                }
                #[cfg(not(feature = "reader-strips"))]
                {
                    let _ = scratch_bytes;
                    return 0;
                }
            }
            Command::ClearPlan => {
                #[cfg(feature = "reader-strips")]
                {
                    physical.clear_plan();
                    self.frame.owner.strips.plan_loan.set(false);
                }
                return 0;
            }
            Command::ClearWindow => {
                #[cfg(feature = "reader-strips")]
                {
                    physical.clear_window();
                    self.frame.owner.strips.window_loan.set(false);
                }
                return 0;
            }
            Command::ReleaseStrips => {
                #[cfg(feature = "reader-strips")]
                self.frame.owner.release_idle_strips();
                return 0;
            }
            Command::Mark(index) => {
                let times = &self.frame.owner.frame.times;
                let mut value = times.get();
                value[index as usize] = context.now();
                times.set(value);
                return 0;
            }
            Command::Log(message) => {
                super::logging::log(
                    context,
                    message,
                    self.frame.owner.frame.times.get(),
                    self.frame.owner.frame.page.get().images,
                    !self.frame.buffer(PlaneBuffer::Msb).is_null(),
                    physical.query(physical::Query::WidthBytes),
                );
                return 0;
            }
            Command::Clear(value) => {
                op.value = u32::from(value);
                physical::Kind::Clear
            }
            Command::Display(mode) => {
                op.mode = mode as u8;
                physical::Kind::Display
            }
            Command::DisplayAsync(mode) => {
                op.mode = mode as u8;
                physical::Kind::DisplayAsync
            }
            Command::GrayBase(mode) => {
                op.mode = mode as u8;
                physical::Kind::GrayBase
            }
            Command::AbsoluteBase(mode) => {
                op.mode = mode as u8;
                physical::Kind::AbsoluteBase
            }
            Command::Precondition => physical::Kind::Precondition,
            Command::Mode(mode) => {
                op.mode = plane(mode);
                physical::Kind::Mode
            }
            Command::StripBegin {
                buffer: id,
                y,
                rows,
                offset,
            } => {
                op.buffer = unsafe { self.frame.buffer(buffer(id)).add(offset as usize) };
                op.y = y;
                op.rows = rows;
                physical::Kind::StripBegin
            }
            Command::StripEnd => physical::Kind::StripEnd,
            Command::Wait => physical::Kind::Wait,
            Command::WritePlane {
                buffer: id,
                plane: mode,
                y,
                rows,
            } => {
                op.buffer = self.frame.buffer(buffer(id));
                op.mode = plane(mode);
                op.y = y;
                op.rows = rows;
                physical::Kind::WritePlane
            }
            Command::GrayDisplay => physical::Kind::GrayDisplay,
            Command::Cleanup => physical::Kind::Cleanup,
            Command::StoreBw => physical::Kind::StoreBw,
            Command::RestoreBw => physical::Kind::RestoreBw,
            Command::CopyLsb => physical::Kind::CopyLsb,
            Command::CopyMsb => physical::Kind::CopyMsb,
        } as u8;
        physical.operation(&op)
    }
}
