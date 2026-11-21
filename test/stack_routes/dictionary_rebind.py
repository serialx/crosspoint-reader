"""Bind changed dictionary call edges against this ELF, without editing the ledger."""

import json


def rebind(spec, calls, source_attestations, output):
    by_name = {row["name"]: row for row in calls}
    original = json.loads(source_attestations.read_text())
    edges = original["edges"]
    carried = {(edge["caller"], edge["callee"]): edge for edge in edges}
    changes = []

    def attest(caller, callee, source, mechanism):
        key = caller, callee
        if key in carried:
            return
        evidence = dict(
            caller=caller, callee=callee, kind="current typed callback", source=source, mechanism=mechanism
        )
        edges.append(evidence)
        carried[key] = evidence

    def current(old):
        if old in by_name:
            return old
        if old == "<xp_ffi::app::epub::book_owner::Protection>::stream":
            new = "xp_ffi::app::epub::book_owner::protection::stream"
        elif "StableOwner<" in old and "DefinitionSession> as core::ops::drop" in old:
            new = "core::ptr::drop_glue::<xp_ffi::app::epub::children::definition_owned::DefinitionOwner>"
        elif "buildDictionaryHtmlPages(GfxRenderer&, " in old:
            new = old.replace(
                "buildDictionaryHtmlPages(GfxRenderer&, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&",
                "buildDictionaryHtmlPages(GfxRenderer&, std::basic_string_view<char, std::char_traits<char> >",
            )
        elif old.startswith(("htmlToPlainText(", "(anonymous namespace)::writeNormalizedXhtml(")):
            new = old.replace(
                "std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&",
                "std::basic_string_view<char, std::char_traits<char> >",
                1,
            )
        elif old.startswith("Dictionary::readDefinition("):
            new = old.replace(
                "Dictionary::LookupResult*, bool)", "Dictionary::LookupResult*, bool, bool*)", 1
            )
        elif old.startswith("Dictionary::lookup(") and old.endswith("::_FUN(void*, unsigned int, bool)"):
            new = old.replace("void*, unsigned int, bool)", "void*, unsigned int, bool, unsigned int*)")
        elif old.startswith("(anonymous namespace)::readIfoFacts("):
            new = "xp_dictionary_ifo"
        elif old.startswith("Dictionary::lookup("):
            new = old
        else:
            return old
        if new.startswith(("Dictionary::readDefinition(", "Dictionary::lookup(")):
            new = new.replace(
                "std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >&",
                "DictionaryBuffer&",
                1,
            )
        assert new in by_name, (old, new)
        return new

    combine = "xp_ffi::dictionary::lookup::xp_dictionary_lookup::combine"
    header = "<xp_ffi::app::epub::children::definition_owned::DefinitionSession>::input_header"
    read_file = "dictionaryFile(HalFile&)::{lambda(void*, unsigned char*, unsigned int)#1}::_FUN(void*, unsigned char*, unsigned int)"
    for name, route in spec["routes"].items():
        old_route = route["segments"]
        new_route = []
        for old in old_route:
            new = current(old)
            if new_route:
                before = new_route[-1]
                if before.startswith("DefinitionPayload::drawBody(") and new.startswith(
                    "GfxRenderer::drawText("
                ):
                    plain = "DefinitionPayload::drawPlainBody(GfxRenderer&, DefinitionPayload::Slot const&, int, int, int, int)"
                    if plain in by_name:
                        new_route.append(plain)
                if (
                    before.startswith("DefinitionPayload::prepareDefinition(")
                    and new == "xp_font_cache_release"
                ):
                    fallback = (
                        "DefinitionPayload::convertHtmlToPlain(GfxRenderer&, DefinitionPayload::Slot&)"
                    )
                    if fallback in by_name:
                        new_route.append(fallback)
                if before == "xp_dictionary_ifo" and new == "HalFile::read(void*, unsigned int)":
                    assert read_file in by_name
                    new_route.append(read_file)
                    attest(
                        before,
                        read_file,
                        "src/util/Dictionary.cpp:43-48 and src/util/DictionaryIo.h:5-10; rust/crates/xp-ffi/src/dictionary/engine.rs",
                        "The full IFO scanner uses the XpDictionaryFile read callback, whose native body forwards to HalFile::read.",
                    )
                if (
                    before == "xp_dictionary_lookup"
                    and new.startswith("Dictionary::lookup(")
                    and combine in by_name
                ):
                    new_route.append(combine)
                    attest(
                        combine,
                        new,
                        "rust/crates/xp-ffi/src/dictionary/lookup.rs:148-242; src/util/Dictionary.cpp",
                        "The multi-entry lookup dispatches the same immutable Definition read callback inside combine. Charge that frame for the longer path.",
                    )
                if (
                    before.endswith("DefinitionSession>::input")
                    and "lambda(XpUiDefinitionLayout*, long, long)" in new
                    and header in by_name
                ):
                    new_route.append(header)
                    attest(
                        header,
                        new,
                        "rust/crates/xp-ffi/src/app/epub/children/definition_input.rs:10-16; src/activities/reader/RustEpubResources.cpp",
                        "The header sampler makes the same DefinitionPhysical layout call. Charge the sampler and native layout frames.",
                    )
            new_route.append(new)
        if new_route != old_route:
            changes.append(dict(route=name, before=old_route, after=new_route))
            route["segments"] = new_route
        for old_caller, old_callee in zip(old_route, old_route[1:]):
            prior = carried.get((old_caller, old_callee))
            if prior is None:
                continue
            caller, callee = current(old_caller), current(old_callee)
            if (caller, callee) != (old_caller, old_callee) and (caller, callee) not in carried:
                # An old callback may keep its target through signature or inline ownership changes.
                # Direct calls are independently verified by the linked auditor.
                if not (caller == "xp_dictionary_lookup" and combine in new_route):
                    attest(
                        caller,
                        callee,
                        prior.get("source") or prior["prior"],
                        "The same callback binding is emitted at the adjusted source or ABI boundary.",
                    )
    (output / "dictionary-route-rebindings.json").write_text(json.dumps(changes, indent=2) + "\n")
    attestations = output / "attestations-dictionary.json"
    attestations.write_text(json.dumps(original, indent=2) + "\n")
    return attestations
