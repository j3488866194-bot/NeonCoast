"""Summarize the current packaged render capture, bound to its verified SHA/run ID."""
import csv
import hashlib
import json
import math
import statistics
from pathlib import Path


def sha(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def percentile(values, proportion):
    data = sorted(values)
    position = (len(data) - 1) * proportion
    low = math.floor(position)
    high = math.ceil(position)
    return data[low] + (data[high] - data[low]) * (position - low)


def main():
    repo = Path(__file__).resolve().parents[1]
    report = json.loads((repo / "artifacts/ue-release-render-verification.json").read_text(encoding="utf-8-sig"))
    if report["status"] != "passed":
        raise RuntimeError("A successful current packaged render verification is required")
    profile = Path(report["profile"])
    if sha(profile) != report["profile_sha256"] or sha(Path(report["executable"])) != report["executable_sha256"]:
        raise RuntimeError("Capture or packaged executable changed since render verification")
    paks = Path(report["executable"]).parents[2] / "Content/Paks"
    for content in report["content_files"]:
        if sha(paks / content["name"]) != content["sha256"]:
            raise RuntimeError("Packaged content changed since render verification: " + content["name"])
    csv.field_size_limit(16 * 1024 * 1024)
    with profile.open(encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.reader(stream))
    if report["run_id"] not in rows[-1] or rows[-1][0] != "[HasHeaderRowAtEnd]":
        raise RuntimeError("Missing verification run metadata or final CSV header")
    # UE can add counters during capture. Its trailing header describes all rows.
    header, frames = rows[-2], rows[1:-2]
    if len(frames) != 720:
        raise RuntimeError(f"Expected 720 captured frames, got {len(frames)}")
    selection = frames[240:599]
    metrics = {}
    for name in ("FrameTime", "GameThreadTime", "RenderThreadTime", "GPUTime"):
        column = header.index(name)
        values = [float(row[column]) for row in selection]
        if any(not math.isfinite(value) or value <= 0 for value in values):
            raise RuntimeError("Invalid timing samples: " + name)
        metrics[name] = {"mean_ms": statistics.mean(values), "median_ms": statistics.median(values),
                         "p95_ms": percentile(values, .95), "max_ms": max(values)}
    result = {"status": "passed", "render_run_id": report["run_id"], "profile": str(profile),
              "profile_sha256": report["profile_sha256"], "executable_sha256": report["executable_sha256"],
              "screenshot_sha256": report["screenshot_sha256"], "resolution": [1600, 900],
              "fps_cap": 60, "sample_frames_zero_based": [240, 598], "sample_count": len(selection),
              "average_fps": 1000 / metrics["FrameTime"]["mean_ms"], "metrics": metrics,
              "scope": "Stationary arrival view in the packaged DX12 game. Warm frames before frame-600 screenshot; "
                       "not a driving-route, whole-city, long-session or uncapped performance guarantee."}
    output = repo / "artifacts/ue-render-performance.json"
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
