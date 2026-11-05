//! Allocation-free streaming inflate; the caller owns state and output history.
use miniz_oxide::inflate::core::{
    decompress, decompress_with_limit, inflate_flags, DecompressorOxide, FastTables,
    HuffmanStorage, SmallTables,
};
use miniz_oxide::inflate::TINFLStatus;

pub type InflateState = DecoderState<FastTables>;
pub type SmallInflateState = DecoderState<SmallTables>;

pub struct DecoderState<T: HuffmanStorage> {
    decoder: DecompressorOxide<T>,
    zlib_wrapped: bool,
}

impl<T: HuffmanStorage> DecoderState<T> {
    /// # Safety
    /// `state` must point to aligned, writable storage for one Self, exclusively
    /// borrowed during initialization and subsequent decoding.
    pub unsafe fn init_in_place(state: *mut Self, zlib_wrapped: bool) {
        // Pinned miniz_oxide 0.9.1 contains only integers, integer arrays and a
        // fieldless State enum whose Start discriminant is zero. The sealed
        // table policies contain only integer arrays. All-zero is
        // its Default representation. Initialize in place to avoid an 8 KiB
        // constructor temporary on the firmware task stack.
        state.write_bytes(0, 1);
        (*state).decoder.init();
        (*state).zlib_wrapped = zlib_wrapped;
    }

    pub fn step(
        &mut self,
        input: &[u8],
        more_input: bool,
        output: &mut [u8],
        out_pos: usize,
        wrapping_window: bool,
    ) -> (i32, usize, usize) {
        self.step_with_limit(
            input,
            more_input,
            output,
            out_pos,
            wrapping_window,
            usize::MAX,
        )
    }

    pub fn step_with_limit(
        &mut self,
        input: &[u8],
        more_input: bool,
        output: &mut [u8],
        out_pos: usize,
        wrapping_window: bool,
        out_max: usize,
    ) -> (i32, usize, usize) {
        let flags = if self.zlib_wrapped {
            inflate_flags::TINFL_FLAG_PARSE_ZLIB_HEADER
        } else {
            0
        } | if more_input {
            inflate_flags::TINFL_FLAG_HAS_MORE_INPUT
        } else {
            0
        } | if wrapping_window {
            0
        } else {
            inflate_flags::TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF
        };
        let (status, consumed, produced) = if out_max == usize::MAX {
            decompress(&mut self.decoder, input, output, out_pos, flags)
        } else {
            decompress_with_limit(&mut self.decoder, input, output, out_pos, out_max, flags)
        };
        let status = match status {
            TINFLStatus::NeedsMoreInput => 0,
            TINFLStatus::Done => 1,
            TINFLStatus::HasMoreOutput => 2,
            error => error as i32,
        };
        (status, consumed, produced)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::mem::MaybeUninit;

    #[test]
    fn state_fits_tinfl_budget() {
        assert_eq!(
            core::mem::size_of::<InflateState>(),
            if cfg!(target_pointer_width = "64") {
                8336
            } else {
                8328
            }
        );
        assert!(core::mem::size_of::<SmallInflateState>() <= 1088);
    }

    #[test]
    fn malformed_input_stays_bounded_in_both_output_modes() {
        exercise_mutations::<FastTables>();
        exercise_mutations::<SmallTables>();
    }

    fn exercise_mutations<T: HuffmanStorage>() {
        let mut random = 0x6001_u32;
        let mut storage = MaybeUninit::<DecoderState<T>>::uninit();
        let mut input = [0_u8; 64];
        let mut output = [0_u8; 32768];
        for case in 0..50000 {
            for byte in &mut input {
                random ^= random << 13;
                random ^= random >> 17;
                random ^= random << 5;
                *byte = random as u8;
            }
            let length = (random as usize) % (input.len() + 1);
            for wrapping in [false, true] {
                for wrapped in [false, true] {
                    unsafe { DecoderState::<T>::init_in_place(storage.as_mut_ptr(), wrapped) };
                    let state = unsafe { storage.assume_init_mut() };
                    let chunk = [1, 7, 64][case % 3];
                    let mut used = 0;
                    let mut position = 0;
                    for _ in 0..256 {
                        let end = (used + chunk).min(length);
                        let (status, consumed, produced) = state.step(
                            &input[used..end],
                            end != length,
                            &mut output,
                            position,
                            wrapping,
                        );
                        assert!(consumed <= end - used);
                        assert!(produced <= output.len() - position);
                        used += consumed;
                        position += produced;
                        if status < 0 || status == 1 || consumed + produced == 0 {
                            break;
                        }
                        if position == output.len() {
                            if !wrapping {
                                break;
                            }
                            position = 0;
                        }
                    }
                }
            }
        }
    }

    #[test]
    fn caller_storage_can_be_reinitialized() {
        let mut storage = MaybeUninit::<InflateState>::uninit();
        for wrapped in [false, true, false] {
            unsafe { InflateState::init_in_place(storage.as_mut_ptr(), wrapped) };
            let state = unsafe { storage.assume_init_mut() };
            let input: &[u8] = if wrapped {
                &[0x78, 0x9c, 3, 0, 0, 0, 0, 1]
            } else {
                &[3, 0]
            };
            let mut output = [0; 1];
            assert_eq!(
                state.step(input, false, &mut output, 0, false),
                (1, input.len(), 0)
            );
        }
    }

    #[test]
    fn distinguishes_input_output_and_checksum_errors() {
        let mut storage = MaybeUninit::<InflateState>::uninit();
        unsafe { InflateState::init_in_place(storage.as_mut_ptr(), true) };
        let state = unsafe { storage.assume_init_mut() };
        let mut output = [0; 1];
        assert_eq!(state.step(&[], true, &mut output, 0, false).0, 0);
        assert_eq!(
            state
                .step(
                    &[0x78, 0x9c, 3, 0, 0, 0, 0, 2],
                    false,
                    &mut output,
                    0,
                    false
                )
                .0,
            -2
        );
        unsafe { InflateState::init_in_place(storage.as_mut_ptr(), false) };
        let state = unsafe { storage.assume_init_mut() };
        assert_eq!(state.step(&[], false, &mut output, 0, false).0, -4);
    }
}
