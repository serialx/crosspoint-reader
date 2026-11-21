# SYNC-200 linked X3 stack routes

The qualification harness reads its routes and callback attestations from the
external evidence directory. Apply each patch to its corresponding input and
pass the patched copies to the linked audit.

| Patch | Input under `layout-split/` | Input SHA-256 |
| --- | --- | --- |
| `x3-default.patch` | `routes-candidate.json` | `2e80d63347eeb9d8f34e543ab460563cd57ded96ed50540db7e9eb18e7df77a8` |
| `x3-default-attestations.patch` | `attestations-candidate.json` | `ce3cb32b1e9a0f49bcaf842fddf025f19f4ca9977b6cc78f73da656d53a37342` |

The X3 image uses the `font::loaded::ensure` closure form on the 20 font routes
and calls `std::__throw_length_error` directly on the 20 HTTP reallocation routes.
The bookmark input prologue uses `OUTLINED_FUNCTION_7` in the 112-byte
`Layout::hit` frame. The dictionary routes bind the bounded IFO scanner,
grouped lookup, header input, and separate styled and plain viewer paths. The
111 book file GET routes use the linked GET closure, transfer facade, and
progress invoker. The Control Center launch routes include the shared
`open_control_center` helper. The former definition-only frontlight-availability
probe was removed for the lightless Control Center. The rename, move, and delete
routes follow the POST handlers at their new linked closure ordinals.

`x3-default-sleep-event.json` lists the sleep-entry emitter and metadata
callback paths. The audit adds the linked physical continuation to the emitter
prefix and checks its complete bound against the loop-task gate.

See `sync-200/integrate-C` in the evidence directory for the call-site
instructions, callback targets, and route totals.
