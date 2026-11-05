use super::abi;
use core::cell::UnsafeCell;

/// Only fixed state copies and arithmetic run inside this model critical section.
pub struct ModelCell<T> {
    value: UnsafeCell<T>,
    runtime: &'static abi::Runtime,
}

impl<T> ModelCell<T> {
    pub fn new(value: T, runtime: &'static abi::Runtime) -> Self {
        Self {
            value: UnsafeCell::new(value),
            runtime,
        }
    }

    pub fn with<R>(&self, apply: impl FnOnce(&mut T) -> R) -> R {
        struct Guard(&'static abi::Runtime);
        impl Drop for Guard {
            fn drop(&mut self) {
                unsafe { (self.0.model_unlock)() };
            }
        }
        unsafe { (self.runtime.model_lock)() };
        let _guard = Guard(self.runtime);
        apply(unsafe { &mut *self.value.get() })
    }
}
