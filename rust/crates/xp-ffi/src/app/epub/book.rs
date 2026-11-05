use super::{abi, app_abi, EpubSession};
use core::{ffi::c_void, ptr::NonNull};

type BookText = unsafe fn(*mut c_void, app_abi::TextSink, *mut c_void) -> u8;

// Native text remains borrowed only while its synchronous sink is running.
pub(super) fn with_book_text<R>(
    book: NonNull<c_void>,
    read: BookText,
    use_text: impl FnOnce(app_abi::Text) -> R,
) -> Option<R> {
    with_text(
        |sink, context| unsafe { read(book.as_ptr(), sink, context) },
        use_text,
    )
}

pub(super) fn with_text<R>(
    read: impl FnOnce(app_abi::TextSink, *mut c_void) -> u8,
    use_text: impl FnOnce(app_abi::Text) -> R,
) -> Option<R> {
    struct Call<F, R> {
        function: Option<F>,
        result: Option<R>,
    }
    unsafe extern "C" fn receive<F: FnOnce(app_abi::Text) -> R, R>(
        context: *mut c_void,
        text: app_abi::Text,
    ) -> u8 {
        let call = &mut *context.cast::<Call<F, R>>();
        call.result = Some(call.function.take().expect("one book text callback")(text));
        1
    }
    fn invoke<F: FnOnce(app_abi::Text) -> R, R>(
        read: impl FnOnce(app_abi::TextSink, *mut c_void) -> u8,
        function: F,
    ) -> Option<R> {
        let mut call = Call {
            function: Some(function),
            result: None,
        };
        read(receive::<F, R>, (&mut call as *mut Call<F, R>).cast());
        call.result
    }
    invoke(read, use_text)
}

impl EpubSession {
    pub(super) fn load_clippings(&self, book: NonNull<c_void>) -> bool {
        unsafe { super::session_book::load_clippings(book.as_ptr(), self.leaves) }
    }

    pub(super) fn book_factory(&self) -> &super::session_book::Factory {
        #[cfg(feature = "app-test")]
        {
            &self.book_factory
        }
        #[cfg(not(feature = "app-test"))]
        {
            &super::session_book::Factory::Reader
        }
    }

    pub(super) fn path(&self) -> &super::Text {
        match self.loop_book() {
            Some(book) => unsafe { super::session_book::path_text(book.as_ptr()) },
            None => unsafe { &*self.path.get() },
        }
    }

    pub(super) fn set_book_extractor(
        &self,
        access: &super::ResourceAccess<'_>,
        book: Option<NonNull<c_void>>,
    ) {
        let facade = book.map_or(core::ptr::null_mut(), |book| {
            assert_eq!(
                self.book(access),
                Some(book),
                "extractor must borrow this session's book"
            );
            unsafe { super::session_book::facade(book.as_ptr()) }
        });
        unsafe { (self.leaves.book_set_extractor)(facade) };
    }

    pub(super) fn add_recent(&self, book: NonNull<c_void>) {
        unsafe { super::session_book::add_recent(book.as_ptr(), self.leaves) };
    }

    pub(super) fn settings(&self, resolve_font: bool) -> abi::XpEpubSettings {
        let mut settings = core::mem::MaybeUninit::uninit();
        unsafe {
            (self.leaves.settings_snapshot)(u8::from(resolve_font), settings.as_mut_ptr());
            settings.assume_init()
        }
    }
}
