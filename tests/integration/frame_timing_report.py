"""Summarize native frame stages without treating camera read wait as CPU work."""
import argparse
from bisect import bisect_left
import json
from pathlib import Path
import re
import statistics


def summarize(text):
    stages = {}
    presented = {}
    camera = []
    acknowledgements = []
    for line in text.splitlines():
        fields = dict(re.findall(r"\b(\w+)=(-?\d+(?:\.\d+)?)", line))
        for key, value in fields.items():
            if key.endswith("_ms") and float(value) >= 0:
                # Keep stage names separate; read_ms appears in several logs.
                marker = next((name for name in ("CAMERA_COPY", "CAMERA_FRAME", "LIVE_FRAME",
                              "ASCII_STAGE", "FRAME_PRESENT", "TERMINAL_ACK", "TERMINAL_REPLY")
                               if name + ":" in line), None)
                if marker:
                    stages.setdefault(marker + "." + key, []).append(float(value))
        if "CAMERA_COPY:" in line:
            camera.append({key: float(value) for key, value in fields.items()})
        elif "FRAME_PRESENT:" in line:
            presented[int(fields["id"])] = int(fields["acquired_ns"])
        elif "TERMINAL_ACK:" in line:
            acknowledgements.append(fields)
    camera.sort(key=lambda frame: frame["request_ns"])
    requests = [frame["request_ns"] for frame in camera]
    for ack in acknowledgements:
        acquired = presented.get(int(ack["id"]))
        if acquired is None:
            continue
        index = bisect_left(requests, acquired)
        if index == len(camera):
            continue
        sample = camera[index]
        # Source-lock acquisition is normally submillisecond. Do not silently
        # associate an unrelated capture after a long modal/source transition.
        if sample["request_ns"] - acquired > 5_000_000:
            continue
        elapsed = float(ack["capture_to_terminal_ms"])
        after_arrival = elapsed - (sample["sample_ns"] - acquired) / 1e6
        if after_arrival < 0:
            continue
        stages.setdefault("joined.sample_arrival_to_terminal_ms", []).append(after_arrival)
        device_age = sample.get("device_age_ms", -1)
        if device_age >= 0:
            stages.setdefault("joined.device_timestamp_to_terminal_ms", []).append(after_arrival + device_age)
    result = {key: {"count": len(values), "median": round(statistics.median(values), 3),
                    "p95": round(sorted(values)[int((len(values) - 1) * .95)], 3),
                    "max": round(max(values), 3)} for key, values in sorted(stages.items())}
    if presented:
        result["coverage"] = {
            "first_presented_frame_id": min(presented),
            "first_capture_request_seconds": round(min(presented.values()) / 1e9, 3),
            "last_capture_request_seconds": round(max(presented.values()) / 1e9, 3),
            "capture_span_seconds": round((max(presented.values()) - min(presented.values())) / 1e9, 3),
        }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    print(json.dumps(summarize(args.log.read_text(encoding="utf-8", errors="replace")), indent=2))


if __name__ == "__main__":
    main()
