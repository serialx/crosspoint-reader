//! Book-facing scalar policies; buffers and object lifetimes stay borrowed.
use core::{ffi::c_char, slice};

// Entity values retained from the original CrossPoint table, based on
// atomic14/diy-esp32-epub-reader (2c2f57fdd7e2a788d14a0bcb26b9e845a47aac42).
static ENTITY_BYTES: &[u8] = b"&AElig;\xc3\x86\x00&Aacute;\xc3\x81\x00&Acirc;\xc3\x82\x00&Agrave;\xc3\x80\x00&Alpha;\xce\x91\x00&Aring;\xc3\x85\x00&Atilde;\xc3\x83\x00&Auml;\xc3\x84\x00&Beta;\xce\x92\x00&Ccedil;\xc3\x87\x00&Chi;\xce\xa7\x00&Dagger;\xe2\x80\xa1\x00&Delta;\xce\x94\x00&ETH;\xc3\x90\x00&Eacute;\xc3\x89\x00&Ecirc;\xc3\x8a\x00&Egrave;\xc3\x88\x00&Epsilon;\xce\x95\x00&Eta;\xce\x97\x00&Euml;\xc3\x8b\x00&Gamma;\xce\x93\x00&Iacute;\xc3\x8d\x00&Icirc;\xc3\x8e\x00&Igrave;\xc3\x8c\x00&Iota;\xce\x99\x00&Iuml;\xc3\x8f\x00&Kappa;\xce\x9a\x00&Lambda;\xce\x9b\x00&Mu;\xce\x9c\x00&Ntilde;\xc3\x91\x00&Nu;\xce\x9d\x00&OElig;\xc5\x92\x00&Oacute;\xc3\x93\x00&Ocirc;\xc3\x94\x00&Ograve;\xc3\x92\x00&Omega;\xce\xa9\x00&Omicron;\xce\x9f\x00&Oslash;\xc3\x98\x00&Otilde;\xc3\x95\x00&Ouml;\xc3\x96\x00&Phi;\xce\xa6\x00&Pi;\xce\xa0\x00&Prime;\xe2\x80\xb3\x00&Psi;\xce\xa8\x00&Rho;\xce\xa1\x00&Scaron;\xc5\xa0\x00&Sigma;\xce\xa3\x00&THORN;\xc3\x9e\x00&Tau;\xce\xa4\x00&Theta;\xce\x98\x00&Uacute;\xc3\x9a\x00&Ucirc;\xc3\x9b\x00&Ugrave;\xc3\x99\x00&Upsilon;\xce\xa5\x00&Uuml;\xc3\x9c\x00&Xi;\xce\x9e\x00&Yacute;\xc3\x9d\x00&Yuml;\xc5\xb8\x00&Zeta;\xce\x96\x00&aacute;\xc3\xa1\x00&acirc;\xc3\xa2\x00&acute;\xc2\xb4\x00&aelig;\xc3\xa6\x00&agrave;\xc3\xa0\x00&alefsym;\xe2\x84\xb5\x00&alpha;\xce\xb1\x00&amp;&\x00&and;\xe2\x88\xa7\x00&ang;\xe2\x88\xa0\x00&apos;'\x00&aring;\xc3\xa5\x00&asymp;\xe2\x89\x88\x00&atilde;\xc3\xa3\x00&auml;\xc3\xa4\x00&bdquo;\xe2\x80\x9e\x00&beta;\xce\xb2\x00&brvbar;\xc2\xa6\x00&bull;\xe2\x80\xa2\x00&cap;\xe2\x88\xa9\x00&ccedil;\xc3\xa7\x00&cedil;\xc2\xb8\x00&cent;\xc2\xa2\x00&chi;\xcf\x87\x00&circ;\xcb\x86\x00&clubs;\xe2\x99\xa3\x00&cong;\xe2\x89\x85\x00&copy;\xc2\xa9\x00&crarr;\xe2\x86\xb5\x00&cup;\xe2\x88\xaa\x00&curren;\xc2\xa4\x00&dArr;\xe2\x87\x93\x00&dagger;\xe2\x80\xa0\x00&darr;\xe2\x86\x93\x00&deg;\xc2\xb0\x00&delta;\xce\xb4\x00&diams;\xe2\x99\xa6\x00&divide;\xc3\xb7\x00&eacute;\xc3\xa9\x00&ecirc;\xc3\xaa\x00&egrave;\xc3\xa8\x00&empty;\xe2\x88\x85\x00&emsp; \x00&ensp;&epsilon;\xce\xb5\x00&equiv;\xe2\x89\xa1\x00&eta;\xce\xb7\x00&eth;\xc3\xb0\x00&euml;\xc3\xab\x00&euro;\xe2\x82\xac\x00&exist;\xe2\x88\x83\x00&fnof;\xc6\x92\x00&forall;\xe2\x88\x80\x00&frac12;\xc2\xbd\x00&frac14;\xc2\xbc\x00&frac34;\xc2\xbe\x00&frasl;\xe2\x81\x84\x00&gamma;\xce\xb3\x00&ge;\xe2\x89\xa5\x00&gt;>\x00&hArr;\xe2\x87\x94\x00&harr;\xe2\x86\x94\x00&hearts;\xe2\x99\xa5\x00&hellip;\xe2\x80\xa6\x00&iacute;\xc3\xad\x00&icirc;\xc3\xae\x00&iexcl;\xc2\xa1\x00&igrave;\xc3\xac\x00&image;\xe2\x84\x91\x00&infin;\xe2\x88\x9e\x00&int;\xe2\x88\xab\x00&iota;\xce\xb9\x00&iquest;\xc2\xbf\x00&isin;\xe2\x88\x88\x00&iuml;\xc3\xaf\x00&kappa;\xce\xba\x00&lArr;\xe2\x87\x90\x00&lambda;\xce\xbb\x00&lang;\xe3\x80\x88\x00&laquo;\xc2\xab\x00&larr;\xe2\x86\x90\x00&lceil;\xe2\x8c\x88\x00&ldquo;\xe2\x80\x9c\x00&le;\xe2\x89\xa4\x00&lfloor;\xe2\x8c\x8a\x00&lowast;\xe2\x88\x97\x00&loz;\xe2\x97\x8a\x00&lrm;\xe2\x80\x8e\x00&lsaquo;\xe2\x80\xb9\x00&lsquo;\xe2\x80\x98\x00&lt;<\x00&macr;\xc2\xaf\x00&mdash;\xe2\x80\x94\x00&micro;\xc2\xb5\x00&middot;\xc2\xb7\x00&minus;\xe2\x88\x92\x00&mu;\xce\xbc\x00&nabla;\xe2\x88\x87\x00&nbsp;\xc2\xa0\x00&ndash;\xe2\x80\x93\x00&ne;\xe2\x89\xa0\x00&ni;\xe2\x88\x8b\x00&not;\xc2\xac\x00&notin;\xe2\x88\x89\x00&nsub;\xe2\x8a\x84\x00&ntilde;\xc3\xb1\x00&nu;\xce\xbd\x00&oacute;\xc3\xb3\x00&ocirc;\xc3\xb4\x00&oelig;\xc5\x93\x00&ograve;\xc3\xb2\x00&oline;\xe2\x80\xbe\x00&omega;\xcf\x89\x00&omicron;\xce\xbf\x00&oplus;\xe2\x8a\x95\x00&or;\xe2\x88\xa8\x00&ordf;\xc2\xaa\x00&ordm;\xc2\xba\x00&oslash;\xc3\xb8\x00&otilde;\xc3\xb5\x00&otimes;\xe2\x8a\x97\x00&ouml;\xc3\xb6\x00&para;\xc2\xb6\x00&part;\xe2\x88\x82\x00&permil;\xe2\x80\xb0\x00&perp;\xe2\x8a\xa5\x00&phi;\xcf\x86\x00&pi;\xcf\x80\x00&piv;\xcf\x96\x00&plusmn;\xc2\xb1\x00&pound;\xc2\xa3\x00&prime;\xe2\x80\xb2\x00&prod;\xe2\x88\x8f\x00&prop;\xe2\x88\x9d\x00&psi;\xcf\x88\x00&quot;\x22\x00&rArr;\xe2\x87\x92\x00&radic;\xe2\x88\x9a\x00&rang;\xe3\x80\x89\x00&raquo;\xc2\xbb\x00&rarr;\xe2\x86\x92\x00&rceil;\xe2\x8c\x89\x00&rdquo;\xe2\x80\x9d\x00&real;\xe2\x84\x9c\x00&reg;\xc2\xae\x00&rfloor;\xe2\x8c\x8b\x00&rho;\xcf\x81\x00&rlm;\xe2\x80\x8f\x00&rsaquo;\xe2\x80\xba\x00&rsquo;\xe2\x80\x99\x00&sbquo;\xe2\x80\x9a\x00&scaron;\xc5\xa1\x00&sdot;\xe2\x8b\x85\x00&sect;\xc2\xa7\x00&shy;\xc2\xad\x00&sigma;\xcf\x83\x00&sigmaf;\xcf\x82\x00&sim;\xe2\x88\xbc\x00&spades;\xe2\x99\xa0\x00&sub;\xe2\x8a\x82\x00&sube;\xe2\x8a\x86\x00&sum;\xe2\x88\x91\x00&sup1;\xc2\xb9\x00&sup2;\xc2\xb2\x00&sup3;\xc2\xb3\x00&sup;\xe2\x8a\x83\x00&supe;\xe2\x8a\x87\x00&szlig;\xc3\x9f\x00&tau;\xcf\x84\x00&there4;\xe2\x88\xb4\x00&theta;\xce\xb8\x00&thetasym;\xcf\x91\x00&thinsp;&thorn;\xc3\xbe\x00&tilde;\xcb\x9c\x00&times;\xc3\x97\x00&trade;\xe2\x84\xa2\x00&uArr;\xe2\x87\x91\x00&uacute;\xc3\xba\x00&uarr;\xe2\x86\x91\x00&ucirc;\xc3\xbb\x00&ugrave;\xc3\xb9\x00&uml;\xc2\xa8\x00&upsih;\xcf\x92\x00&upsilon;\xcf\x85\x00&uuml;\xc3\xbc\x00&weierp;\xe2\x84\x98\x00&xi;\xce\xbe\x00&yacute;\xc3\xbd\x00&yen;\xc2\xa5\x00&yuml;\xc3\xbf\x00&zeta;\xce\xb6\x00&zwj;\xe2\x80\x8d\x00&zwnj;\xe2\x80\x8c\x00";
static ENTITIES: &[[u16; 3]] = &[
    [0, 7, 7],
    [10, 8, 18],
    [21, 7, 28],
    [31, 8, 39],
    [42, 7, 49],
    [52, 7, 59],
    [62, 8, 70],
    [73, 6, 79],
    [82, 6, 88],
    [91, 8, 99],
    [102, 5, 107],
    [110, 8, 118],
    [122, 7, 129],
    [132, 5, 137],
    [140, 8, 148],
    [151, 7, 158],
    [161, 8, 169],
    [172, 9, 181],
    [184, 5, 189],
    [192, 6, 198],
    [201, 7, 208],
    [211, 8, 219],
    [222, 7, 229],
    [232, 8, 240],
    [243, 6, 249],
    [252, 6, 258],
    [261, 7, 268],
    [271, 8, 279],
    [282, 4, 286],
    [289, 8, 297],
    [300, 4, 304],
    [307, 7, 314],
    [317, 8, 325],
    [328, 7, 335],
    [338, 8, 346],
    [349, 7, 356],
    [359, 9, 368],
    [371, 8, 379],
    [382, 8, 390],
    [393, 6, 399],
    [402, 5, 407],
    [410, 4, 414],
    [417, 7, 424],
    [428, 5, 433],
    [436, 5, 441],
    [444, 8, 452],
    [455, 7, 462],
    [465, 7, 472],
    [475, 5, 480],
    [483, 7, 490],
    [493, 8, 501],
    [504, 7, 511],
    [514, 8, 522],
    [525, 9, 534],
    [537, 6, 543],
    [546, 4, 550],
    [553, 8, 561],
    [564, 6, 570],
    [573, 6, 579],
    [582, 8, 590],
    [593, 7, 600],
    [603, 7, 610],
    [613, 7, 620],
    [623, 8, 631],
    [634, 9, 643],
    [647, 7, 654],
    [657, 5, 662],
    [664, 5, 669],
    [673, 5, 678],
    [682, 6, 688],
    [690, 7, 697],
    [700, 7, 707],
    [711, 8, 719],
    [722, 6, 728],
    [731, 7, 738],
    [742, 6, 748],
    [751, 8, 759],
    [762, 6, 768],
    [772, 5, 777],
    [781, 8, 789],
    [792, 7, 799],
    [802, 6, 808],
    [811, 5, 816],
    [819, 6, 825],
    [828, 7, 835],
    [839, 6, 845],
    [849, 6, 855],
    [858, 7, 865],
    [869, 5, 874],
    [878, 8, 886],
    [889, 6, 895],
    [899, 8, 907],
    [911, 6, 917],
    [921, 5, 926],
    [929, 7, 936],
    [939, 7, 946],
    [950, 8, 958],
    [961, 8, 969],
    [972, 7, 979],
    [982, 8, 990],
    [993, 7, 1000],
    [1004, 6, 1010],
    [1012, 6, 1010],
    [1018, 9, 1027],
    [1030, 7, 1037],
    [1041, 5, 1046],
    [1049, 5, 1054],
    [1057, 6, 1063],
    [1066, 6, 1072],
    [1076, 7, 1083],
    [1087, 6, 1093],
    [1096, 8, 1104],
    [1108, 8, 1116],
    [1119, 8, 1127],
    [1130, 8, 1138],
    [1141, 7, 1148],
    [1152, 7, 1159],
    [1162, 4, 1166],
    [1170, 4, 1174],
    [1176, 6, 1182],
    [1186, 6, 1192],
    [1196, 8, 1204],
    [1208, 8, 1216],
    [1220, 8, 1228],
    [1231, 7, 1238],
    [1241, 7, 1248],
    [1251, 8, 1259],
    [1262, 7, 1269],
    [1273, 7, 1280],
    [1284, 5, 1289],
    [1293, 6, 1299],
    [1302, 8, 1310],
    [1313, 6, 1319],
    [1323, 6, 1329],
    [1332, 7, 1339],
    [1342, 6, 1348],
    [1352, 8, 1360],
    [1363, 6, 1369],
    [1373, 7, 1380],
    [1383, 6, 1389],
    [1393, 7, 1400],
    [1404, 7, 1411],
    [1415, 4, 1419],
    [1423, 8, 1431],
    [1435, 8, 1443],
    [1447, 5, 1452],
    [1456, 5, 1461],
    [1465, 8, 1473],
    [1477, 7, 1484],
    [1488, 4, 1492],
    [1494, 6, 1500],
    [1503, 7, 1510],
    [1514, 7, 1521],
    [1524, 8, 1532],
    [1535, 7, 1542],
    [1546, 4, 1550],
    [1553, 7, 1560],
    [1564, 6, 1570],
    [1573, 7, 1580],
    [1584, 4, 1588],
    [1592, 4, 1596],
    [1600, 5, 1605],
    [1608, 7, 1615],
    [1619, 6, 1625],
    [1629, 8, 1637],
    [1640, 4, 1644],
    [1647, 8, 1655],
    [1658, 7, 1665],
    [1668, 7, 1675],
    [1678, 8, 1686],
    [1689, 7, 1696],
    [1700, 7, 1707],
    [1710, 9, 1719],
    [1722, 7, 1729],
    [1733, 4, 1737],
    [1741, 6, 1747],
    [1750, 6, 1756],
    [1759, 8, 1767],
    [1770, 8, 1778],
    [1781, 8, 1789],
    [1793, 6, 1799],
    [1802, 6, 1808],
    [1811, 6, 1817],
    [1821, 8, 1829],
    [1833, 6, 1839],
    [1843, 5, 1848],
    [1851, 4, 1855],
    [1858, 5, 1863],
    [1866, 8, 1874],
    [1877, 7, 1884],
    [1887, 7, 1894],
    [1898, 6, 1904],
    [1908, 6, 1914],
    [1918, 5, 1923],
    [1926, 6, 1932],
    [1934, 6, 1940],
    [1944, 7, 1951],
    [1955, 6, 1961],
    [1965, 7, 1972],
    [1975, 6, 1981],
    [1985, 7, 1992],
    [1996, 7, 2003],
    [2007, 6, 2013],
    [2017, 5, 2022],
    [2025, 8, 2033],
    [2037, 5, 2042],
    [2045, 5, 2050],
    [2054, 8, 2062],
    [2066, 7, 2073],
    [2077, 7, 2084],
    [2088, 8, 2096],
    [2099, 6, 2105],
    [2109, 6, 2115],
    [2118, 5, 2123],
    [2126, 7, 2133],
    [2136, 8, 2144],
    [2147, 5, 2152],
    [2156, 8, 2164],
    [2168, 5, 2173],
    [2177, 6, 2183],
    [2187, 5, 2192],
    [2196, 6, 2202],
    [2205, 6, 2211],
    [2214, 6, 2220],
    [2223, 5, 2228],
    [2232, 6, 2238],
    [2242, 7, 2249],
    [2252, 5, 2257],
    [2260, 8, 2268],
    [2272, 7, 2279],
    [2282, 10, 2292],
    [2295, 8, 1010],
    [2303, 7, 2310],
    [2313, 7, 2320],
    [2323, 7, 2330],
    [2333, 7, 2340],
    [2344, 6, 2350],
    [2354, 8, 2362],
    [2365, 6, 2371],
    [2375, 7, 2382],
    [2385, 8, 2393],
    [2396, 5, 2401],
    [2404, 7, 2411],
    [2414, 9, 2423],
    [2426, 6, 2432],
    [2435, 8, 2443],
    [2447, 4, 2451],
    [2454, 8, 2462],
    [2465, 5, 2470],
    [2473, 6, 2479],
    [2482, 6, 2488],
    [2491, 5, 2496],
    [2500, 6, 2506],
];
/// # Safety
/// Input is readable for length. The returned NUL-terminated pointer is static.
#[no_mangle]
pub unsafe extern "C" fn xp_book_entity(input: *const u8, length: usize) -> *const c_char {
    if input.is_null() || length == 0 {
        return core::ptr::null();
    }
    let input = slice::from_raw_parts(input, length);
    ENTITIES
        .binary_search_by(|entry| {
            let begin = usize::from(entry[0]);
            let end = begin + usize::from(entry[1]);
            ENTITY_BYTES[begin..end].cmp(input)
        })
        .map_or(core::ptr::null(), |i| {
            ENTITY_BYTES
                .as_ptr()
                .add(usize::from(ENTITIES[i][2]))
                .cast()
        })
}
/// # Safety
/// Fields contains twelve initialized values in legacy signature order.
#[no_mangle]
pub unsafe extern "C" fn xp_book_layout_signature(
    fields: *const [u32; 12],
    compression: f32,
) -> u32 {
    let Some(fields) = fields.as_ref() else {
        return 1;
    };
    let mut signature = 2166136261u32;
    for (i, &field) in fields.iter().enumerate() {
        let value = if i == 1 {
            (compression * 1000.0) as u32
        } else {
            field
        };
        signature = (signature ^ value).wrapping_mul(16777619);
    }
    if signature == 0 {
        1
    } else {
        signature
    }
}
/// # Safety
/// Bounds points to four initialized i16 values: x, y, width, height.
#[no_mangle]
pub unsafe extern "C" fn xp_book_link_contains(
    bounds: *const [i16; 4],
    x: i32,
    y: i32,
    slop: i32,
    minimum: i32,
) -> u8 {
    let Some(b) = bounds.as_ref() else {
        return 0;
    };
    let [bx, by, width, height] = b.map(i32::from);
    let horizontal = slop.max(minimum.wrapping_sub(width) / 2);
    (x >= bx.wrapping_sub(horizontal)
        && x < bx.wrapping_add(width).wrapping_add(horizontal)
        && y >= by.wrapping_sub(slop)
        && y < by.wrapping_add(height).wrapping_add(slop)) as u8
}
/// # Safety
/// Name and tag point to their stated readable lengths (or null when empty).
#[no_mangle]
pub unsafe extern "C" fn xp_book_tag_equals(
    name: *const u8,
    length: usize,
    tag: *const u8,
    tag_length: usize,
) -> u8 {
    if length != tag_length {
        return 0;
    }
    if length == 0 {
        return 1;
    }
    if name.is_null() || tag.is_null() {
        return 0;
    }
    let name = slice::from_raw_parts(name, length);
    let tag = slice::from_raw_parts(tag, tag_length);
    name.iter()
        .zip(tag)
        .all(|(a, b)| a.to_ascii_lowercase() == *b) as u8
}
/// # Safety
/// Name points to length readable bytes (or null when empty).
#[no_mangle]
pub unsafe extern "C" fn xp_book_tag_hidden(name: *const u8, length: usize) -> u8 {
    if length == 0 || name.is_null() {
        return 0;
    }
    let name = slice::from_raw_parts(name, length);
    [b"head".as_slice(), b"style", b"script", b"title", b"rp"]
        .iter()
        .any(|tag| name.eq_ignore_ascii_case(tag)) as u8
}
#[no_mangle]
pub extern "C" fn xp_book_image_dimensions(width: i64, height: i64) -> u8 {
    if width <= 0 || height <= 0 {
        1
    } else if width > 32767 || height > 32767 {
        2
    } else if width * height > 8388608 {
        3
    } else {
        0
    }
}
/// # Safety
/// Last is one initialized exclusive clock value.
#[no_mangle]
pub unsafe extern "C" fn xp_book_image_yield(last: *mut u32, now: u32) -> u8 {
    let Some(last) = last.as_mut() else {
        return 0;
    };
    if now.wrapping_sub(*last) < 250 {
        return 0;
    }
    *last = now;
    1
}

#[repr(C)]
pub struct MutableSpan {
    data: *mut u8,
    length: usize,
}
#[repr(C)]
pub struct ExtensionServices {
    ctx: *mut core::ffi::c_void,
    view: unsafe extern "C" fn(*mut core::ffi::c_void) -> MutableSpan,
    suffix: unsafe extern "C" fn(*mut core::ffi::c_void, usize),
}
/// # Safety
/// View lends mutable string bytes until suffix; suffix changes storage and ends that loan.
#[no_mangle]
pub unsafe extern "C" fn xp_book_image_decoder(services: *const ExtensionServices) -> u8 {
    let Some(s) = services.as_ref() else {
        return 0;
    };
    let view = (s.view)(s.ctx);
    let offset = if view.length == 0 {
        usize::MAX
    } else {
        slice::from_raw_parts(view.data, view.length)
            .iter()
            .rposition(|c| *c == b'.')
            .unwrap_or(usize::MAX)
    };
    (s.suffix)(s.ctx, offset);
    let view = (s.view)(s.ctx);
    let extension = if view.length == 0 {
        &mut []
    } else {
        slice::from_raw_parts_mut(view.data, view.length)
    };
    extension.make_ascii_lowercase();
    match &*extension {
        b".jpg" | b".jpeg" => 1,
        b".png" => 2,
        b".gif" => 3,
        _ => 0,
    }
}
/// # Safety
/// Rectangle callback borrows its context for the call and never retains pointers.
#[no_mangle]
pub unsafe extern "C" fn xp_book_image_placeholder(
    x: i32,
    y: i32,
    width: i32,
    height: i32,
    ctx: *mut core::ffi::c_void,
    rectangle: unsafe extern "C" fn(*mut core::ffi::c_void, i32, i32, i32, i32, u8),
) {
    rectangle(ctx, x, y, width, height, 1);
    if width > 2 && height > 2 {
        rectangle(
            ctx,
            x.wrapping_add(1),
            y.wrapping_add(1),
            width - 2,
            height - 2,
            0,
        );
    }
}

#[no_mangle]
pub extern "C" fn xp_book_page_allocation(
    header: usize,
    arena: usize,
    alignment: usize,
    elements: usize,
    element_bytes: usize,
) -> usize {
    if alignment == 0 || !alignment.is_power_of_two() {
        return usize::MAX;
    }
    let Some(padded) = header
        .checked_add(arena)
        .and_then(|n| n.checked_add(alignment - 1))
    else {
        return usize::MAX;
    };
    let offset = padded & !(alignment - 1);
    elements
        .checked_mul(element_bytes)
        .and_then(|n| offset.checked_add(n))
        .unwrap_or(usize::MAX)
}
/// # Safety
/// Arena is a stable decoded page; optional callback is borrowed and cannot mutate it.
#[no_mangle]
pub unsafe extern "C" fn xp_book_page_images(
    arena: *const u8,
    length: usize,
    ctx: *mut core::ffi::c_void,
    needs_decode: Option<unsafe extern "C" fn(*mut core::ffi::c_void, u16) -> u8>,
) -> u8 {
    if arena.is_null() {
        return 0;
    }
    let Ok(page) = xp_core::page::Page::open(slice::from_raw_parts(arena, length)) else {
        return 0;
    };
    for i in 0..page.element_count() {
        let Ok(element) = page.element(i) else {
            return 0;
        };
        if element.tag() == Ok(2) && needs_decode.is_none_or(|needs| needs(ctx, i) != 0) {
            return 1;
        }
    }
    0
}
