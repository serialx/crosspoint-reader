use core::fmt;

// The native diagnostic promotes f32 to double and prints %.2f, ties to even.
pub(super) struct Scale(pub(super) f32);

// Finite f32 values need at most five base-10^9 limbs (39 decimal digits).
#[inline(never)]
fn large_integer(out: &mut fmt::Formatter<'_>, significand: u32, shift: u32) -> fmt::Result {
    let mut limbs = [significand, 0, 0, 0, 0];
    for _ in 0..shift {
        let mut carry = 0;
        for limb in &mut limbs {
            let doubled = *limb * 2 + carry;
            carry = u32::from(doubled >= 1_000_000_000);
            *limb = doubled - carry * 1_000_000_000;
        }
    }
    let mut digits = limbs.iter().rev().skip_while(|&&limb| limb == 0);
    write!(out, "{}", digits.next().expect("nonzero significand"))?;
    for limb in digits {
        write!(out, "{limb:09}")?;
    }
    out.write_str(".00")
}

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
        // Above 2^24 every finite f32 is integral.
        if magnitude >= 16_777_216.0 {
            let significand = (bits & 0x7f_ffff) | 0x80_0000;
            let shift = ((bits >> 23) & 0xff) - 150;
            return if shift <= 40 {
                write!(out, "{}.00", u64::from(significand) << shift)
            } else {
                large_integer(out, significand, shift)
            };
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
