import collections
import json
import re


def graph(d):
    rows = [json.loads(l) for l in (d / "resolved-calls.jsonl").read_text().splitlines()]
    F = {}
    for r in rows:
        if r["name"] not in F:
            F[r["name"]] = {**r, "calls": list(r["calls"])}
        else:
            F[r["name"]]["calls"] += r["calls"]
            F[r["name"]]["frame"] = max(F[r["name"]]["frame"], r["frame"])
    A = {int(r["address"], 16): r["name"] for r in rows}
    G = {n: set() for n in F}
    unknown = collections.defaultdict(list)
    blocks = {}
    for n, r in F.items():
        for c in r["calls"]:
            target = c.get("target")
            if target and int(target, 16) in A:
                G[n].add(A[int(target, 16)])
            elif not target:
                unknown[n].append(c)
    for b in re.split(r"\n(?=[0-9a-f]+ <)", (d / "all-disassembly.txt").read_text()):
        m = re.match(r"([0-9a-f]+) <(.+)>:", b)
        if not m:
            continue
        n = m[2]
        blocks[n] = blocks.get(n, "") + b
        for l in b.splitlines()[1:]:
            if re.search(r"\b(j|jr)\b", l):
                t = re.search(r"([0-9a-f]{8}) <", l)
                if t and int(t[1], 16) in A and A[int(t[1], 16)] != n:
                    G[n].add(A[int(t[1], 16)])
                elif t and int(t[1], 16) not in A:
                    label = re.search(r"<(.+)>", l)[1]
                    if label == n or label.startswith(n + "+0x"):
                        continue
                    F[n]["calls"].append(
                        dict(pc=l.split(":")[0], op="tail", target="0x" + t[1], name=label, frame=None)
                    )
    return F, G, unknown, blocks


def reachable(g, roots):
    todo = list(roots)
    seen = set(roots)
    for n in todo:
        for c in g.get(n, []):
            if c not in seen:
                seen.add(c)
                todo.append(c)
    return seen


def scc(g, nodes):
    index = {}
    low = {}
    stack = []
    active = set()
    out = []

    def visit(n):
        index[n] = low[n] = len(index)
        stack.append(n)
        active.add(n)
        for c in g.get(n, []):
            if c not in nodes:
                continue
            if c not in index:
                visit(c)
                low[n] = min(low[n], low[c])
            elif c in active:
                low[n] = min(low[n], index[c])
        if low[n] == index[n]:
            found = []
            while True:
                c = stack.pop()
                active.remove(c)
                found.append(c)
                if c == n:
                    break
            out.append(found)

    for n in sorted(nodes):
        if n not in index:
            visit(n)
    return out
