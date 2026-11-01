# Home ownership tests

`differential_oracle.py` compares nine frozen traces for Home input, standard
and grid layout, touch, cover caching and shared file/library views. The adapters
call production Rust; typed Home resources are decoded only to print the original
trace. Do not regenerate the frozen goldens to accommodate a port.

`app_owner` compiles the production `xp-ffi/src/app` module with host allocation
and fake physical/resource leaves. It checks library duplicate/missing filtering
before corrupt metadata, prepared-title validation without wrapping, early render
during entry, and Home build/paint rejection with the original input matrix.
`home_app_owners` runs these cases through CTest. The portable navigation, owned
result and Home timing tests live in `xp-ui::app`.

`facade_oracle.py` retains the FileBrowser/Library native binding assertions and
runs Home resource validation through the production Rust owner. The USB manager
and remote-input oracles likewise drive the real Rust application. The real
xp-ffi host archive enables `app-test` for four read-only probes; these symbols
must be absent from every firmware image.

Run `python3 test/home_ui/differential_oracle.py`, then the complete host CTest
suite. These tests do not establish hardware behavior: verify repeated
Home/book/Home, all themes/polarities, remapping and orientations, touch/held
input, popup close, sleep/wake and physical Home/power on a device.
