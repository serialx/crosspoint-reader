pub mod core;
mod output_buffer;
const TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS: i32 = -4;
const TINFL_STATUS_BAD_PARAM: i32 = -3;
const TINFL_STATUS_ADLER32_MISMATCH: i32 = -2;
const TINFL_STATUS_FAILED: i32 = -1;
const TINFL_STATUS_DONE: i32 = 0;
const TINFL_STATUS_NEEDS_MORE_INPUT: i32 = 1;
const TINFL_STATUS_HAS_MORE_OUTPUT: i32 = 2;
#[cfg(feature = "block-boundary")]
const TINFL_STATUS_BLOCK_BOUNDARY: i32 = 3;

/// Return status codes.
#[repr(i8)]
#[cfg_attr(not(feature = "rustc-dep-of-std"), derive(Hash, Debug))]
#[derive(Copy, Clone, PartialEq, Eq)]
#[non_exhaustive]
pub enum TINFLStatus {
    /// More input data was expected, but the caller indicated that there was no more data, so the
    /// input stream is likely truncated.
    ///
    /// This can't happen if you have provided the
    /// [`TINFL_FLAG_HAS_MORE_INPUT`][core::inflate_flags::TINFL_FLAG_HAS_MORE_INPUT] flag to the
    /// decompression.  By setting that flag, you indicate more input exists but is not provided,
    /// and so reaching the end of the input data without finding the end of the compressed stream
    /// would instead return a [`NeedsMoreInput`][Self::NeedsMoreInput] status.
    FailedCannotMakeProgress = TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS as i8,

    /// The output buffer is an invalid size; consider the `flags` parameter.
    BadParam = TINFL_STATUS_BAD_PARAM as i8,

    /// The decompression went fine, but the adler32 checksum did not match the one
    /// provided in the header.
    Adler32Mismatch = TINFL_STATUS_ADLER32_MISMATCH as i8,

    /// Failed to decompress due to invalid data.
    Failed = TINFL_STATUS_FAILED as i8,

    /// Finished decompression without issues.
    ///
    /// This indicates the end of the compressed stream has been reached.
    Done = TINFL_STATUS_DONE as i8,

    /// The decompressor needs more input data to continue decompressing.
    ///
    /// This occurs when there's no more consumable input, but the end of the stream hasn't been
    /// reached, and you have supplied the
    /// [`TINFL_FLAG_HAS_MORE_INPUT`][core::inflate_flags::TINFL_FLAG_HAS_MORE_INPUT] flag to the
    /// decompressor.  Had you not supplied that flag (which would mean you were asserting that you
    /// believed all the data was available) you would have gotten a
    /// [`FailedCannotMakeProcess`][Self::FailedCannotMakeProgress] instead.
    NeedsMoreInput = TINFL_STATUS_NEEDS_MORE_INPUT as i8,

    /// There is still pending data that didn't fit in the output buffer.
    HasMoreOutput = TINFL_STATUS_HAS_MORE_OUTPUT as i8,

    /// Reached the end of a deflate block, and the start of the next block.
    ///
    /// At this point, you can suspend decompression and later resume with a new `DecompressorOxide`.
    /// The only state that must be preserved is [`DecompressorOxide::block_boundary_state()`],
    /// plus the last 32KiB of the output buffer (or less if you know the stream was compressed with
    /// a smaller window size).
    ///
    /// This is only returned if you use the
    /// [`TINFL_FLAG_STOP_ON_BLOCK_BOUNDARY`][core::inflate_flags::TINFL_FLAG_STOP_ON_BLOCK_BOUNDARY] flag.
    #[cfg(feature = "block-boundary")]
    BlockBoundary = TINFL_STATUS_BLOCK_BOUNDARY as i8,
}

impl TINFLStatus {
    pub fn from_i32(value: i32) -> Option<TINFLStatus> {
        use self::TINFLStatus::*;
        match value {
            TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS => Some(FailedCannotMakeProgress),
            TINFL_STATUS_BAD_PARAM => Some(BadParam),
            TINFL_STATUS_ADLER32_MISMATCH => Some(Adler32Mismatch),
            TINFL_STATUS_FAILED => Some(Failed),
            TINFL_STATUS_DONE => Some(Done),
            TINFL_STATUS_NEEDS_MORE_INPUT => Some(NeedsMoreInput),
            TINFL_STATUS_HAS_MORE_OUTPUT => Some(HasMoreOutput),
            #[cfg(feature = "block-boundary")]
            TINFL_STATUS_BLOCK_BOUNDARY => Some(BlockBoundary),
            _ => None,
        }
    }
}
