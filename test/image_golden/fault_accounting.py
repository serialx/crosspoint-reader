"""Keep actual injected failures separate from requested fault settings."""
import json


def read_report(path, fault, index):
    result = json.loads(path.read_text())
    path.unlink()
    calls, hits = result["calls"], result["hits"]
    if fault == "none":
        assert hits == 0
        state = "baseline"
    elif fault == "heap":
        assert calls > 0 and hits == 0
        state = "heap-threshold-control"
    elif hits:
        state = "injected"
    else:
        assert fault in ("read", "write", "seek", "allocation") and index >= calls, (fault, index, result)
        state = "deliberately-unreachable-index"
    return dict(**result, state=state)


def summarize(results):
    injected = [row for row in results if "faults" in row]
    return {side: {
        "injected_settings": sum(row["faults"][side]["hits"] > 0 for row in injected),
        "fault_hits": sum(row["faults"][side]["hits"] for row in injected),
        "unreachable_settings": sum(row["faults"][side]["state"] == "deliberately-unreachable-index" for row in injected),
    } for side in ("native", "reader")}
