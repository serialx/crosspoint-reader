# Offline graphics dependencies

The workspace pins embedded-graphics-core 0.4.0 (released 2023-05-14), az 1.2.1 and byteorder 1.5.0. Sources come from the crates.io release archives; licenses are beside each manifest. embedded-graphics-core licenses come from its `embedded-graphics-core-v0.4.0` Git tag because the crate archive omits them.

| Archive | SHA-256 |
| --- | --- |
| `embedded-graphics-core-0.4.0.crate` | `ba9ecd261f991856250d2207f6d8376946cd9f412a2165d3b75bc87a0bc7a044` |
| `az-1.2.1.crate` | `7b7e4c2464d97fe331d41de9d5db0def0a96f4d823b8b32a2efd503578988973` |
| `byteorder-1.5.0.crate` | `1fd0f2584146f6f2ef48085050886acf353beff7305ebd1ae69500e27c67f64b` |

Only no-std dependencies are enabled. Normalized manifests use local paths; optional defmt/nalgebra and dev-only dependencies are omitted. Original manifests and changelogs are retained. Upstream source is unchanged except:

- embedded-graphics-core: remove `self::` from `Points::new` for the newer compiler’s `unused_qualifications` lint.
- az: allow deprecated numeric associated functions and declare its existing `track_caller` configuration for current Rust lints.
- Rustfmt applies the workspace formatter’s whitespace layout.

Cargo uses no network for these crates, including the C3 build. No firmware feature selects the host-only `strip-spike` entry points.
