use core::fmt;

// The native diagnostic promotes f32 to double and prints %.2f, ties to even.
pub(super) struct Scale(pub(super) f32);

impl fmt::Display for Scale {
    fn fmt(&self, out: &mut fmt::Formatter<'_>) -> fmt::Result {
        let bits = self.0.to_bits();
        if bits >> 31 != 0 {
            out.write_str("-")?;
        }
        let magnitude = f32::from_bits(bits & 0x7fff_ffff);
        if magnitude.is_nan() {
            return out.write_str("nan");
        }
        if magnitude.is_infinite() {
            return out.write_str("inf");
        }
        // Above 2^24 every finite f32 is integral and fits in u128.
        if magnitude >= 16_777_216.0 {
            return write!(out, "{}.00", magnitude as u128);
        }
        // A 24-bit f32 significand times 100 is exact in an f64.
        let scaled = f64::from(magnitude) * 100.0;
        let floor = scaled as u64;
        let remainder = scaled - floor as f64;
        let cents =
            floor + u64::from(remainder > 0.5 || (remainder == 0.5 && !floor.is_multiple_of(2)));
        write!(out, "{}.{:02}", cents / 100, cents % 100)
    }
}
