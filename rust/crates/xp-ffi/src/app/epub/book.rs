use super::{abi, app_abi, EpubSession};
use core::{ffi::c_void, ptr::NonNull};

type BookText = unsafe extern "C" fn(*mut c_void, app_abi::TextSink, *mut c_void) -> u8;

// Native text remains borrowed only while its synchronous sink is running.
pub(super) fn with_book_text<R>(
    book: NonNull<c_void>,
    read: BookText,
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
        book: NonNull<c_void>,
        read: BookText,
        function: F,
    ) -> Option<R> {
        let mut call = Call {
            function: Some(function),
            result: None,
        };
        unsafe {
            read(
                book.as_ptr(),
                receive::<F, R>,
                (&mut call as *mut Call<F, R>).cast(),
            )
        };
        call.result
    }
    invoke(book, read, use_text)
}

impl EpubSession {
    pub(super) fn load_clippings(&self, book: NonNull<c_void>) -> bool {
        unsafe { (self.leaves.clippings_load)(book.as_ptr()) != 0 }
    }

    pub(super) fn settings(&self) -> abi::XpEpubSettings {
        let mut settings = core::mem::MaybeUninit::uninit();
        unsafe {
            (self.leaves.settings_snapshot)(settings.as_mut_ptr());
            settings.assume_init()
        }
    }
}
