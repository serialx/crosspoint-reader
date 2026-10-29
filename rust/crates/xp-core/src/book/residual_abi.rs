//! Numeric contracts mirrored by xp_book_residual.h; checked by book_io/abi.py.

// XpBookDocumentOp
pub const XP_BOOK_DOCUMENT_FIND: u32 = 0;
pub const XP_BOOK_DOCUMENT_BASE: u32 = 1;
pub const XP_BOOK_DOCUMENT_SIZE: u32 = 2;
pub const XP_BOOK_DOCUMENT_SETUP: u32 = 3;
pub const XP_BOOK_DOCUMENT_READ: u32 = 4;
pub const XP_BOOK_DOCUMENT_GUIDE_LOAD: u32 = 5;
pub const XP_BOOK_DOCUMENT_GUIDE_ASSIGN: u32 = 6;
pub const XP_BOOK_DOCUMENT_GUIDE_RELEASE: u32 = 7;
pub const XP_BOOK_DOCUMENT_CSS: u32 = 8;

// XpBookDocumentDiagnostic
pub const XP_BOOK_DOCUMENT_LOG_CONTAINER_SIZE: u32 = 0;
pub const XP_BOOK_DOCUMENT_LOG_CONTAINER_READ: u32 = 1;
pub const XP_BOOK_DOCUMENT_LOG_CONTAINER_ROOT: u32 = 2;
pub const XP_BOOK_DOCUMENT_LOG_FIND_OPF: u32 = 3;
pub const XP_BOOK_DOCUMENT_LOG_PARSING_OPF: u32 = 4;
pub const XP_BOOK_DOCUMENT_LOG_OPF_SIZE: u32 = 5;
pub const XP_BOOK_DOCUMENT_LOG_OPF_SETUP: u32 = 6;
pub const XP_BOOK_DOCUMENT_LOG_OPF_READ: u32 = 7;
pub const XP_BOOK_DOCUMENT_LOG_METADATA_PARSED: u32 = 8;
pub const XP_BOOK_DOCUMENT_LOG_GUIDE_FALLBACK: u32 = 9;
pub const XP_BOOK_DOCUMENT_LOG_GUIDE_FOUND: u32 = 10;
pub const XP_BOOK_DOCUMENT_LOG_OPF_PARSED: u32 = 11;
pub const XP_BOOK_DOCUMENT_LOG_NO_NCX: u32 = 12;
pub const XP_BOOK_DOCUMENT_LOG_PARSING_NCX: u32 = 13;
pub const XP_BOOK_DOCUMENT_LOG_NCX_SIZE: u32 = 14;
pub const XP_BOOK_DOCUMENT_LOG_NCX_SETUP: u32 = 15;
pub const XP_BOOK_DOCUMENT_LOG_NCX_READ: u32 = 16;
pub const XP_BOOK_DOCUMENT_LOG_NO_NAV: u32 = 17;
pub const XP_BOOK_DOCUMENT_LOG_PARSING_NAV: u32 = 18;
pub const XP_BOOK_DOCUMENT_LOG_NAV_SIZE: u32 = 19;
pub const XP_BOOK_DOCUMENT_LOG_NAV_SETUP: u32 = 20;
pub const XP_BOOK_DOCUMENT_LOG_NAV_READ: u32 = 21;
pub const XP_BOOK_DOCUMENT_LOG_NCX_PARSED: u32 = 22;
pub const XP_BOOK_DOCUMENT_LOG_NAV_PARSED: u32 = 23;

// XpBookCssOp
pub const XP_BOOK_CSS_FILE_CLEAR: u32 = 0;
pub const XP_BOOK_CSS_FILE_DEDUP: u32 = 1;
pub const XP_BOOK_CSS_FILE_HEAP: u32 = 2;
pub const XP_BOOK_CSS_FILE_SIZE: u32 = 3;
pub const XP_BOOK_CSS_FILE_OPEN_WRITE: u32 = 4;
pub const XP_BOOK_CSS_FILE_STREAM: u32 = 5;
pub const XP_BOOK_CSS_FILE_CLOSE: u32 = 6;
pub const XP_BOOK_CSS_FILE_REMOVE: u32 = 7;
pub const XP_BOOK_CSS_FILE_OPEN_READ: u32 = 8;
pub const XP_BOOK_CSS_FILE_PARSE: u32 = 9;
pub const XP_BOOK_CSS_FILE_DROP_TEMP: u32 = 10;
pub const XP_BOOK_CSS_FILE_EMPTY: u32 = 11;
pub const XP_BOOK_CSS_FILE_SAVE: u32 = 12;

// XpBookCssParseStatus
pub const XP_BOOK_CSS_PARSE_COMPLETE: u32 = 0;
pub const XP_BOOK_CSS_PARSE_PARTIAL: u32 = 1;
pub const XP_BOOK_CSS_PARSE_ERROR: u32 = 2;

// XpBookCssDiagnostic
pub const XP_BOOK_CSS_LOG_NO_FILES: u32 = 0;
pub const XP_BOOK_CSS_LOG_COUNT: u32 = 1;
pub const XP_BOOK_CSS_LOG_DEDUP_OOM: u32 = 2;
pub const XP_BOOK_CSS_LOG_PARSING: u32 = 3;
pub const XP_BOOK_CSS_LOG_LOW_HEAP: u32 = 4;
pub const XP_BOOK_CSS_LOG_OVERSIZE: u32 = 5;
pub const XP_BOOK_CSS_LOG_CREATE: u32 = 6;
pub const XP_BOOK_CSS_LOG_READ: u32 = 7;
pub const XP_BOOK_CSS_LOG_OPEN: u32 = 8;
pub const XP_BOOK_CSS_LOG_FAILED: u32 = 9;
pub const XP_BOOK_CSS_LOG_EMPTY_PARTIAL: u32 = 10;
pub const XP_BOOK_CSS_LOG_KEEP_PARTIAL: u32 = 11;
pub const XP_BOOK_CSS_LOG_SAVE: u32 = 12;
pub const XP_BOOK_CSS_LOG_LOADED: u32 = 13;

// XpBookZipIndexOp
pub const XP_BOOK_ZIP_INDEX_CACHE_SIZE: u32 = 0;
pub const XP_BOOK_ZIP_INDEX_OPEN: u32 = 1;
pub const XP_BOOK_ZIP_INDEX_DETAILS: u32 = 2;
pub const XP_BOOK_ZIP_INDEX_CENTRAL: u32 = 3;
pub const XP_BOOK_ZIP_INDEX_START: u32 = 4;
pub const XP_BOOK_ZIP_INDEX_CLEAR: u32 = 5;
pub const XP_BOOK_ZIP_INDEX_RESERVE: u32 = 6;
pub const XP_BOOK_ZIP_INDEX_CURSOR: u32 = 7;
pub const XP_BOOK_ZIP_INDEX_LAST_VALID: u32 = 8;
pub const XP_BOOK_ZIP_INDEX_CURSOR_VALID: u32 = 9;
pub const XP_BOOK_ZIP_INDEX_CURSOR_VALUE: u32 = 10;

// XpBookZipCacheOp
pub const XP_BOOK_ZIP_CACHE_FIND: u32 = 0;
pub const XP_BOOK_ZIP_CACHE_LAST: u32 = 1;
pub const XP_BOOK_ZIP_CACHE_REMEMBER: u32 = 2;
pub const XP_BOOK_ZIP_CACHE_INSERT: u32 = 3;
pub const XP_BOOK_ZIP_CACHE_PATHS: u32 = 4;
pub const XP_BOOK_ZIP_CACHE_VISIT: u32 = 5;

// XpBookZipIndexAction
pub const XP_BOOK_ZIP_LOOKUP: u32 = 0;
pub const XP_BOOK_ZIP_PATHS: u32 = 2;
pub const XP_BOOK_ZIP_ENTRIES: u32 = 3;

// XpBookZipDetailDiagnostic
pub const XP_BOOK_ZIP_DETAIL_LOG_SMALL: u32 = 0;
pub const XP_BOOK_ZIP_DETAIL_LOG_SIZE: u32 = 1;
pub const XP_BOOK_ZIP_DETAIL_LOG_ALLOCATION: u32 = 2;
pub const XP_BOOK_ZIP_DETAIL_LOG_SIGNATURE: u32 = 3;

// XpBookXtcOp
pub const XP_BOOK_XTC_EXISTS: u32 = 0;
pub const XP_BOOK_XTC_READY: u32 = 1;
pub const XP_BOOK_XTC_PAGES: u32 = 2;
pub const XP_BOOK_XTC_SETUP: u32 = 3;
pub const XP_BOOK_XTC_FIRST: u32 = 4;
pub const XP_BOOK_XTC_GENERATE_COVER: u32 = 5;
pub const XP_BOOK_XTC_OPEN_SOURCE: u32 = 6;
pub const XP_BOOK_XTC_OPEN_TARGET: u32 = 7;
pub const XP_BOOK_XTC_COPY: u32 = 8;
pub const XP_BOOK_XTC_LOAD: u32 = 9;
pub const XP_BOOK_XTC_OPEN_IMAGE: u32 = 10;
pub const XP_BOOK_XTC_RENDER: u32 = 11;

// XpBookXtcDiagnostic
pub const XP_BOOK_XTC_LOG_COVER_UNLOADED: u32 = 0;
pub const XP_BOOK_XTC_LOG_THUMB_UNLOADED: u32 = 1;
pub const XP_BOOK_XTC_LOG_NO_PAGES: u32 = 2;
pub const XP_BOOK_XTC_LOG_PAGE_INFO: u32 = 3;
pub const XP_BOOK_XTC_LOG_COPIED: u32 = 4;
pub const XP_BOOK_XTC_LOG_COVER_OOM: u32 = 5;
pub const XP_BOOK_XTC_LOG_THUMB_OOM: u32 = 6;
pub const XP_BOOK_XTC_LOG_COVER_READ: u32 = 7;
pub const XP_BOOK_XTC_LOG_THUMB_READ: u32 = 8;
pub const XP_BOOK_XTC_LOG_COVER_OPEN: u32 = 9;
pub const XP_BOOK_XTC_LOG_THUMB_OPEN: u32 = 10;
pub const XP_BOOK_XTC_LOG_COVER_DONE: u32 = 11;
pub const XP_BOOK_XTC_LOG_THUMB_DONE: u32 = 12;

// XpBookTxtMetadataOp
pub const XP_BOOK_TXT_META_TITLE: u32 = 0;
pub const XP_BOOK_TXT_META_LANGUAGE: u32 = 1;
pub const XP_BOOK_TXT_META_FIND_COVER: u32 = 2;
pub const XP_BOOK_TXT_META_COVER: u32 = 3;

// XpBookDirectoryOp
pub const XP_BOOK_DIRECTORY_EXISTS: u32 = 0;
pub const XP_BOOK_DIRECTORY_CREATE: u32 = 1;

// XpBookLookupDiagnostic
pub const XP_BOOK_LOOKUP_NONE: u32 = 0;
pub const XP_BOOK_LOOKUP_UNLOADED: u32 = 1;
pub const XP_BOOK_LOOKUP_RANGE: u32 = 2;
pub const XP_BOOK_LOOKUP_MISSING: u32 = 3;

// XpBookPath
pub const XP_BOOK_PATH_HTML_DIRECTORY: u32 = 0;
pub const XP_BOOK_PATH_SECTIONS_DIRECTORY: u32 = 1;
pub const XP_BOOK_PATH_HTML_DOCUMENT: u32 = 2;
pub const XP_BOOK_PATH_TEXT_HREF: u32 = 3;
pub const XP_BOOK_PATH_COVER: u32 = 4;
pub const XP_BOOK_PATH_THUMB_PATTERN: u32 = 5;
pub const XP_BOOK_PATH_THUMB_PREFIX: u32 = 6;

// XpBookDocumentField
pub const XP_BOOK_DOCUMENT_AUXILIARY: u32 = 100;
pub const XP_BOOK_DOCUMENT_CONTAINER_PATH: u32 = 0;

// XpBookDocumentKind
pub const XP_BOOK_DOCUMENT_CONTAINER: u32 = 1;
pub const XP_BOOK_DOCUMENT_NCX: u32 = 2;
pub const XP_BOOK_DOCUMENT_NAV: u32 = 3;
pub const XP_BOOK_DOCUMENT_OPF: u32 = 5;
