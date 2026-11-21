# SYNC-200 linked X3 stack routes

The qualification harness reads its route table from the external evidence
directory. Apply `x3-default.patch` to the `layout-split/routes-candidate.json`
input with SHA-256
`2e80d63347eeb9d8f34e543ab460563cd57ded96ed50540db7e9eb18e7df77a8`.
Write the result to a new file and use it as the linked audit's route input.

The 20 font lookup paths use the linked 16-byte `Owner::ensure` frame.
The 20 HTTP reallocation paths call `std::__throw_length_error` directly.
The bookmark input path uses `OUTLINED_FUNCTION_21` inside the existing
112-byte `Layout::hit` frame. Those last two bindings match the input table.
See `sync-200/join` in the evidence directory for the ELF call edges and
the route-by-route totals.
