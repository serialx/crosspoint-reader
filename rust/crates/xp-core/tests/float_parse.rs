use xp_core::text::parse_f32;

#[test]
fn decimal_bits_match_core() {
    let cases = [
        "0", "-0", "+0", ".0", "1.", "-.5", "+12.5e-2",
        "inf", "Inf", "INFINITY", "-infinity", "NaN", "-nan", "+NaN",
        "1.0000000596046448", "1.000000059604644775390625",
        "1.000000059604644775390626", "1.4999999403953553",
        "7.00649232162408535461864791644958065640130970938257885878534141944895541342930300743319094181060791015625e-46",
        "1.401298464324817e-45", "1.17549435e-38", "3.402823466e38",
        "3.40282356779733661637539395458142568448e38", "1e39", "-1e-999",
        "0e999999999999999999999999", "1e999999999999999999999999",
        "-0e-999999999999999999999999",
    ];
    for input in cases {
        assert_eq!(
            parse_f32(input.as_bytes()).map(f32::to_bits),
            input.parse::<f32>().ok().map(f32::to_bits),
            "{input}"
        );
    }
    for length in [32, 256, 4096, 16000] {
        for input in [
            format!("1.{}1", "0".repeat(length)),
            format!("1{}e-{length}", "0".repeat(length)),
            "9".repeat(length),
        ] {
            assert_eq!(
                parse_f32(input.as_bytes()).map(f32::to_bits),
                input.parse::<f32>().ok().map(f32::to_bits)
            );
        }
    }
}

#[test]
fn full_consumption_and_decimal_grammar() {
    for input in [
        b"".as_slice(),
        b" ",
        b" 1",
        b"1 ",
        b"\t1",
        b"1\n",
        b"1\0",
        b"1\0junk",
        b"1\xff",
        b"++1",
        b"+-1",
        b"--1",
        b".",
        b"+.",
        b"1e",
        b"1e+",
        b"1..2",
        b"1_0",
        b"1,5",
        b"1px",
        b"0x1p0",
        b"0X1.2p-1",
        b"nan(123)",
        b"-nan(foo)",
        b"infinite",
    ] {
        assert_eq!(parse_f32(input), None, "{input:?}");
    }
}
