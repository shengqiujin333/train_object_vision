"""Bounded local contract/schema checks. Does not execute product or contact devices."""
from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.metadata
import json
from pathlib import Path
import platform
import re
import subprocess
import sys

import yaml
from jsonschema import Draft202012Validator

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def load_json(path):
    def bad_number(value):
        raise ValueError(f"non-finite JSON constant: {value}")
    return json.loads(path.read_text(encoding="utf-8"), parse_constant=bad_number)


def semantic_errors(v, cameras, thresholds, app):
    """Cross-field invariants supplement JSON Schema; not an implementation oracle."""
    errors = []
    kind = v["kind"]
    cam = cameras.get(v.get("camera_id"))
    if "camera_id" in v and cam is None:
        errors.append("registered_camera")
    if kind == "FrameDescriptor":
        w, h, planes = v["width"], v["height"], v["planes"]
        if v["format"] == "NV12":
            if w % 2 or h % 2:
                errors.append("nv12_even")
            heights, min_stride = [h, h // 2], w
        else:
            heights, min_stride = [h], w * 3
        if len(planes) != len(heights):
            errors.append("plane_count")
        for p, rows in zip(planes, heights):
            if p["stride_bytes"] < min_stride or p["size_bytes"] < p["stride_bytes"] * rows:
                errors.append("plane_stride")
            if p["offset"] + p["size_bytes"] > v["buffer_bytes"]:
                errors.append("plane_bounds")
        ranges = sorted((p["offset"], p["offset"] + p["size_bytes"]) for p in planes)
        if any(a[1] > b[0] for a, b in zip(ranges, ranges[1:])):
            errors.append("plane_overlap")
    elif kind == "InferenceEnvelope":
        if cam and (not cam["ai_enabled"] or cam["ai_profile"] != v["profile"]):
            errors.append("profile_match")
        delta = v["result_mono_us"] - v["capture_mono_us"]
        if delta < 0 or v["latency_ms"] != delta // 1000:
            errors.append("result_time")
        if v["availability"] == "unavailable":
            if v["boxes"]:
                errors.append("unavailable_boxes")
            if v["reason"] == "NONE":
                errors.append("availability_reason")
        elif v["reason"] != "NONE" or not v["model_version"]:
            errors.append("availability_reason")
        for box in v["boxes"]:
            if box["x"] + box["w"] > 1 + 1e-9 or box["y"] + box["h"] > 1 + 1e-9:
                errors.append("box_bounds")
    elif kind == "ClipRequest":
        trigger = v["trigger_wall_ms"]
        if not (v["t0_wall_ms"] < trigger < v["t1_wall_ms"]):
            errors.append("clip_window")
        if (trigger - v["t0_wall_ms"] != thresholds["clip_pre_seconds"] * 1000
                or v["t1_wall_ms"] - trigger != thresholds["clip_post_seconds"] * 1000):
            errors.append("clip_config_window")
    elif kind == "ProviderReply":
        if v["provider"] == "nvr":
            config = next((i for i in app.get("integrations", []) if i["id"] == "NVR"), {})
            configured = bool(config.get("protocol") and config.get("channel_mapping"))
            if not configured and (v["status"] != "unavailable" or v["reason"] != "UNCONFIGURED"):
                errors.append("nvr_config")
        if v["status"] in ("unavailable", "error"):
            if v["reason"] == "NONE" or v["segments"]:
                errors.append("provider_unavailable")
        if v["status"] == "ok" and (v["reason"] != "NONE" or v["gaps"]):
            errors.append("provider_ok")
        if v["status"] == "partial" and not v["gaps"]:
            errors.append("partial_gap")
        for item in v["segments"] + v["gaps"]:
            if item["camera_id"] not in cameras or item["t0_wall_ms"] >= item["t1_wall_ms"]:
                errors.append("provider_interval")
    elif kind == "ActionReply":
        if (v["phase"] == "failed") == (v["reason"] == "NONE"):
            errors.append("action_reason")
    elif kind == "StreamSnapshot":
        last = v["last_frame_mono_us"]
        if (last is None) != (v["last_frame_wall_ms"] is None):
            errors.append("stream_clock_pair")
        if v["presentable"] and not (
            v["state"] in ("live", "degraded") and last is not None
            and 0 <= v["now_mono_us"] - last < thresholds["freeze_timeout_ms"] * 1000
        ):
            errors.append("stream_freshness")
    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    schema = load_json(HERE / "interface.schema.json")
    examples = load_json(HERE / "examples.json")
    Draft202012Validator.check_schema(schema)
    validator = Draft202012Validator(schema)
    read_paths = [HERE / "INTERFACE_CONTRACT.md", HERE / "interface.schema.json", HERE / "examples.json", Path(__file__)]

    def read(rel):
        path = ROOT / rel
        read_paths.append(path)
        return path.read_text(encoding="utf-8")

    cameras_raw = yaml.safe_load(read("app/config/cameras.yaml"))["cameras"]
    cameras = {c["id"]: c for c in cameras_raw}
    app = yaml.safe_load(read("app/config/app.yaml"))["app"]
    thresholds = yaml.safe_load(read("app/config/thresholds.yaml"))["thresholds"]
    rois = yaml.safe_load(read("app/config/roi.yaml"))["roi"]
    checks = []

    def record(name, ok, detail):
        checks.append({"name": name, "passed": bool(ok), "detail": detail})

    record("schema_meta", True, "Draft 2020-12 schema is valid")
    valid_values = {case["name"]: case["value"] for case in examples["valid"]}

    def errors(value):
        schema_errors = list(validator.iter_errors(value))
        if schema_errors:
            return ["schema"]
        return semantic_errors(value, cameras, thresholds, app)

    for name, value in valid_values.items():
        found = errors(value)
        record(f"valid:{name}", not found, found)
    for case in examples["invalid"]:
        value = copy.deepcopy(valid_values[case["base"]])
        value.update(case["patch"])
        found = errors(value)
        record(f"reject:{case['name']}", case["expected"] in found,
               {"expected": case["expected"], "observed": found})

    # Compare design vectors only. This does not run or certify product ordering logic.
    frame = valid_values["padded_nv12"]
    for case in examples["sequence_cases"]:
        result = copy.deepcopy(valid_values["tracked_person"])
        result.update({k: case[k] for k in ("session_id", "generation", "seq") if k in case})
        same_stream = all(result[k] == frame[k] for k in ("session_id", "camera_id", "generation"))
        verdict = "accept" if same_stream and result["seq"] > case["last_seq"] else "STALE"
        record(f"vector:{case['name']}", verdict == case["expected"], verdict)
    wall_ref, mono_ref, mono_value = 1760000000000, 1000000, 1040000
    record("vector:clock_units", wall_ref + (mono_value - mono_ref) // 1000 == 1760000000040,
           "1000000us→1040000us corresponds to 40ms; reference formula only")
    record("vector:adjacent_not_total_window", all(b - a <= thresholds["confirm_window_ms"]
           for a, b in zip([0, 400], [400, 800])), "0/400/800ms is three adjacent hits under current source semantics")
    record("vector:suppress_boundary", not (3000 < thresholds["suppress_window_ms"]),
           "At default 3000ms equality, previous event is no longer suppressed")

    record("config:six_visible_udp", len(cameras) == len(cameras_raw) == 6 and all(
        c["spectrum"] == "visible" and c["transport"] == "udp" and "@" not in c["rtsp_url"]
        and c["credential_env"] for c in cameras_raw), "No credentials resolved or printed")
    record("config:profiles", [c["ai_profile"] for c in cameras_raw] ==
           ["front", "none", "none", "pantograph", "wheel_rail", "cab_door"], "Current baseline profile order")
    record("config:roi_bounds", all(r["camera"] in cameras and 0 <= r["x"] < r["x"] + r["w"] <= 1
           and 0 <= r["y"] < r["y"] + r["h"] <= 1 for r in rois), "ROI camera references and normalized bounds")
    record("config:runtime_intent", app["mode"] == "visible" and app["render_path"] == "cpu"
           and app["simulation"] is False, "Configuration intent; not measured availability")

    # Narrow source fingerprint assertions: fail on drift and require renewed architecture review.
    source_probes = [
        ("app/src/core/include/railhmi/core/inference.h", "struct FrameRef", "frame_ref_exists"),
        ("app/src/media/src/ai_worker.cpp", "queues_[i].push(FrameRef{c.camera_id, seq_, clock_ms_})", "synthetic_tick_present"),
        ("app/src/media/src/camera_stream.cpp", "gst_sample_unref(s)", "sample_release_present"),
        ("app/src/core/src/stream_state.cpp", "(now_ms - last_frame_ms_) < th_.freeze_timeout_ms", "freshness_strict_less"),
        ("app/src/core/src/rules.cpp", "d.t_ms - cs.last_ms > cfg_.confirm_window_ms", "adjacent_confirmation_window"),
        ("app/src/core/src/rules.cpp", "d.t_ms - le->second < cfg_.suppress_window_ms", "suppression_strict_less"),
        ("app/src/core/src/rules.cpp", "ack_state = AckState::Acked", "ack_state_update"),
        ("app/src/core/src/timebase.cpp", "ref_wall_ms_ + (mono_us - ref_mono_us_) / 1000", "timebase_formula"),
        ("app/src/platform/src/sqlite_store.cpp", "PRAGMA synchronous=NORMAL", "sqlite_normal"),
        ("app/src/platform/src/file_store.cpp", "std::rename(tmp_path.c_str(), path.c_str())", "rename_not_durability"),
        ("app/src/ui/src/main.cpp", "build_channel_views(registry, {}, 0)", "initial_empty_channels"),
        ("app/src/ui/qml/components/VideoSurface.qml", 'objectName: "videoLayer"', "video_layer_placeholder"),
        ("code/rknpu2_postprocess.cc", "for (int i = 0; i < 13; i++)", "segmentation_13_outputs"),
        ("code/main.cc", "st == IM_STATUS_NOERROR || st == IM_STATUS_SUCCESS", "rga_success_codes")
    ]
    for path, snippet, name in source_probes:
        source = read(path)
        line = next((i for i, s in enumerate(source.splitlines(), 1) if snippet in s), None)
        record(f"source:{name}", line is not None, {"path": path, "line": line})

    rules = read("app/src/core/src/rules.cpp")
    mapping = re.search(r"AlarmLevel map_level\(.*?\n}\n", rules, re.S).group(0)
    expected = {
        "L1": ["PersonOnRail", "FrontObstacle", "WheelDerail"],
        "L2": ["PantographAbnormal", "DoorAjar", "FrontCameraDevice", "Device"],
        "L3": ["SideCameraDevice"]
    }
    for level, names in expected.items():
        actual = re.search(r"((?:\s*case AlarmType::\w+:)+)\s*return AlarmLevel::" + level, mapping)
        record(f"source:alarm_mapping_{level}", actual is not None and
               set(re.findall(r"AlarmType::(\w+)", actual.group(1))) == set(names), names)

    cmd = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True, capture_output=True, timeout=15)
    record("provenance:git_head", cmd.returncode == 0, cmd.stderr.strip())
    document = (HERE / "INTERFACE_CONTRACT.md").read_text(encoding="utf-8")
    for index in range(1, 11):
        record(f"document:IF-{index:02}" if index < 10 else "document:INV-10",
               (f"IF-{index:02}" if index < 10 else "INV-10") in document, "Declared boundary exists")

    digests = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
               for p in sorted(set(read_paths))}
    failures = [c["name"] for c in checks if not c["passed"]]
    report = {
        "evidence_type": "ContractValidationEvidence", "status": "PASS" if not failures else "FAIL",
        "scope": "local schema + fixture semantic invariants + selected source/config correspondence",
        "contract_version": "1.0.0", "source_head": cmd.stdout.strip(),
        "command": "python docs/interfaces/validate_contract.py --output docs/interfaces/VALIDATION.json",
        "versions": {"python": platform.python_version(), "PyYAML": importlib.metadata.version("PyYAML"),
                     "jsonschema": importlib.metadata.version("jsonschema")},
        "summary": {"checks": len(checks), "passed": len(checks) - len(failures), "failed": failures,
                    "positive_examples": len(valid_values), "negative_examples": len(examples["invalid"]),
                    "sequence_vectors": len(examples["sequence_cases"])},
        "checks": checks, "input_sha256": digests,
        "findings": [
            {"id": "D-01", "state": "implementation_handoff", "fact": "stats-only media API; no pixel lease to UI/AI"},
            {"id": "D-02", "state": "implementation_handoff", "fact": "state transitions do not rebuild media pipeline"},
            {"id": "D-03", "state": "implementation_handoff", "fact": "AI tick synthesizes FrameRef and has no result publication"},
            {"id": "D-04", "state": "implementation_handoff", "fact": "semantic adapter/tracking/order needed before rule ingestion"},
            {"id": "D-05", "state": "implementation_handoff", "fact": "repository update/transaction API and file fsync absent"},
            {"id": "D-06", "state": "implementation_handoff", "fact": "clip metadata and replay cursor do not supply actual media"},
            {"id": "D-07", "state": "implementation_handoff", "fact": "main seeds health/recovery examples unconditionally"},
            {"id": "D-08", "state": "unknown", "fact": "NVR/PTZ actual protocol, distance calibration and dedicated model semantics need evidence"}
        ],
        "not_executed": ["remote inspection", "camera stream access", "product compilation or product unit tests",
                         "target integration", "real inference", "recording/playback", "performance", "power loss", "human visual acceptance"],
        "limitations": ["Source snippets check correspondence only, not runtime correctness or absence of other code.",
                        "No schema acceptance creates a usable pixel buffer, model, device, or media file.",
                        "Product 0.1.0 release scope remains unchanged; downstream implementation is required."]
    }
    if args.output:
        destination = args.output.resolve()
        if not destination.is_relative_to(HERE):
            raise ValueError("Evidence output must remain inside docs/interfaces")
        destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["summary"], ensure_ascii=False))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
