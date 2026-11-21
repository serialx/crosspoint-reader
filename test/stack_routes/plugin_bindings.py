import json


def bind_file_get(spec, linked, work, attestations):
    owner = "PluginCatalogActivity::downloadBook(PluginCatalogActivity::Item const&)"
    old_closure = owner + "::{lambda()#1}::operator()() const"

    def select(caller, prefix):
        targets = {edge["name"] for edge in linked[caller]["calls"] if edge["name"].startswith(prefix)}
        (target,) = targets
        return target

    closures = {
        edge["name"] for edge in linked[owner]["calls"] if edge["name"].startswith(owner + "::{lambda")
    }
    (closure,) = [
        name
        for name in closures
        if any(edge["name"].startswith("CatalogActivity::downloadFile(") for edge in linked[name]["calls"])
    ]
    catalog = select(closure, "CatalogActivity::downloadFile(")
    get = select(catalog, "HttpDownloader::downloadToFile(")
    entry = select(get, "xp_http_transfer_retire_n5_n6_c2_c4_b2")
    (progress,) = [
        n
        for n in linked
        if n.startswith("std::_Function_handler<void (unsigned int, unsigned int), " + catalog)
        and "#2}>::_M_invoke" in n
    ]

    original = json.loads(attestations.read_text())
    seen = {(edge["caller"], edge["callee"]): edge for edge in original["edges"]}
    changed = {}
    for label, route in spec["routes"].items():
        before = route["segments"]
        if old_closure not in before:
            continue
        replacements = {old_closure: closure}
        for name in before:
            if name.startswith("CatalogActivity::downloadFile("):
                replacements[name] = catalog
            if name.startswith(("HttpDownloader::postToFile(", "HttpDownloader::downloadToFile(")):
                replacements[name] = get
            if name.startswith(
                "std::_Function_handler<void (unsigned int, unsigned int), CatalogActivity::downloadFile("
            ):
                replacements[name] = progress
        after = [replacements.get(name, name) for name in before]
        route["segments"] = after
        changed[label] = dict(before=before, after=after)
        for a, b in zip(before, before[1:]):
            new_a, new_b = replacements.get(a, a), replacements.get(b, b)
            if (new_a, new_b) in seen or (a, b) not in seen:
                continue
            prior = seen[a, b]
            record = {**prior, "caller": new_a, "callee": new_b}
            if new_a == progress or new_b == progress:
                record["source"] = (
                    "src/activities/CatalogActivity.cpp, GET progress lambda; src/network/HttpDownloader.h, synchronous callback dispatch"
                )
                record["mechanism"] = (
                    "The selected file download lambda calls the GET facade with the GET progress owner. The runtime null POST-body branch installs this invoker."
                )
            seen[new_a, new_b] = record
            original["edges"].append(record)
    assert len(changed) == 111
    attestations.write_text(json.dumps(original, indent=2) + "\n")
    proof = {
        name: {
            **linked[name],
            "calls": [
                edge
                for edge in linked[name]["calls"]
                if edge["name"] in {owner, closure, catalog, get, entry, progress}
            ],
        }
        for name in (owner, closure, catalog, get, entry, progress)
    }
    (work / "file-get-bindings.json").write_text(
        json.dumps(dict(proof=proof, routes=changed), indent=2) + "\n"
    )
