# I18n compatibility oracle

Run `python3 test/i18n_oracle/run.py` from any directory. The test generates and
compiles the public C++ facade in three configurations: all languages/keys,
EN+FR only, and unused keys stripped. The Rust archive is rebuilt with matching
options for each case.

The compressed byte streams were captured from the C++ implementation at
539ebafc before the Rust port. They cover every language and string ID, metadata,
character sets, language-code round trips, invalid IDs/languages/codes, ignored
invalid language selections, picker order, and the V1 migration table. Strings
are length-prefixed so embedded newlines cannot hide differences.

`--capture` replaces the goldens; use it only when an intentional compatibility
change has been approved. A one-line change to the old invalid-ID fallback was
verified to fail this oracle before porting.
