// Shared with xp_display.h and the SDK wait-tag mapping.
#[cfg(feature = "uc8279-x3")]
pub const TAG_UC8279_X3_PON: u32 = 30;
#[cfg(feature = "uc8279-x3")]
pub const TAG_UC8279_X3_POF: u32 = 31;
#[cfg(feature = "uc8279-x3")]
pub const TAG_UC8279_X3_DRF: u32 = 32;
#[cfg(feature = "uc8279-x3")]
pub const TAG_UC8279_X3_GRAY_PON: u32 = 33;
#[cfg(feature = "uc8279-x3")]
pub const TAG_UC8279_X3_GRAY_DRF: u32 = 34;
#[cfg(feature = "uc8279-x3")]
pub const TAG_UC8279_X3_DIRECT_GRAY_DRF: u32 = 35;
#[cfg(feature = "uc8279-x3")]
pub const TAG_UC8279_X3_GRAY_POF: u32 = 36;
#[cfg(feature = "uc8279-x3")]
pub const TAG_UC8279_X3_BW_TARGET_DRF: u32 = 37;
#[cfg(feature = "uc8279-x3")]
pub const TAG_UC8279_X3_POWER_DOWN: u32 = 38;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8279_X4_PON: u32 = 40;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8279_X4_POF: u32 = 41;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8279_X4_DRF: u32 = 42;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8279_X4_BW_TARGET_DRF: u32 = 43;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8279_X4_POWER_DOWN: u32 = 44;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_ABSOLUTE_PLANE: u32 = 45;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8279_X4_GRAY_PON: u32 = 46;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8279_X4_GRAY: u32 = 47;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8279_X4_DIRECT_GRAY_DRF: u32 = 48;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8279_X4_GRAY_POF: u32 = 49;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8279_X4_GRAY_BASE_READY: u32 = 50;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8279_X4_GRAY_BASE_POF: u32 = 51;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8279_X4_GRAY_PRE_READY: u32 = 52;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8279_X4_GRAY_PRE_PON: u32 = 53;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8279_X4_GRAY_PRE_DRF: u32 = 54;
#[cfg(feature = "uc8279c-a4")]
pub const TAG_A4_POWER_ON: u32 = 57;
#[cfg(feature = "uc8279c-a4")]
pub const TAG_A4_REFRESH: u32 = 58;
#[cfg(feature = "uc8279c-a4")]
pub const TAG_A4_POWER_OFF: u32 = 59;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_PON: u32 = 120;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_POF: u32 = 121;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_DRF: u32 = 122;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_BW_TARGET_DRF: u32 = 123;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_POWER_DOWN: u32 = 124;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_DIRECT_SETUP_POF: u32 = 125;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_DIRECT_EXIT_POF: u32 = 126;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_GRAY_BASE_READY: u32 = 127;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_GRAY_BASE_POF: u32 = 128;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_GRAY_PRE_READY: u32 = 129;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_GRAY_PRE_PON: u32 = 130;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_GRAY_PRE_DRF: u32 = 131;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_GRAY_LSB: u32 = 132;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_GRAY_MSB: u32 = 133;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_DIRECT_READY: u32 = 134;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_DIRECT_PON: u32 = 135;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_DIRECT_GRAY_DRF: u32 = 136;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_DIRECT_POF: u32 = 137;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_GRAY_READY: u32 = 138;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_GRAY_PON: u32 = 139;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_GRAY_SPLIT_DRF: u32 = 140;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_ABSOLUTE_POF: u32 = 141;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub const TAG_UC8179_GRAY_CLEANUP: u32 = 142;
