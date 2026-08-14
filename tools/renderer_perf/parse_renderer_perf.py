#!/usr/bin/env python3
"""Offline parser and summarizer for Quakespasm-OpenVR renderer debug logs.

Parses:
- r_perfdebug
- r_vr_eyedebug
- r_vr_mirrordebug
- r_gpudebug

The parser is intentionally narrow to known engine output and skips unrelated lines.
It produces grouped summaries for desktop, VR, and per-eye buckets.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Mapping, Optional, Sequence, TextIO

TAG_PREFIX_RE = re.compile(
    r"(?P<tag>r_perfdebug|r_vr_eyedebug|r_vr_mirrordebug|r_gpudebug):"
)
GROUP_RE = re.compile(r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\((?P<body>[^)]*)\)")
PAIR_RE = re.compile(
    r'(?P<key>[A-Za-z_][A-Za-z0-9_]*)=(?P<value>(?:"[^"]*"|[^\s()]+))'
)
FLOAT_RE = re.compile(r"^[+-]?(?:\d+\.\d*|\d*\.\d+|\d+)(?:[eE][+-]?\d+)?$")
INT_RE = re.compile(r"^[+-]?\d+$")
ABS_LINUX_RE = re.compile(r"^/[^\s]+$")
ABS_WINDOWS_RE = re.compile(r"^[A-Za-z]:\\")


def parse_numeric(value: str) -> Any:
    """Parse numbers as int/float. Fallback to string if parse fails."""
    if value == "nan":
        return float("nan")
    if INT_RE.match(value):
        try:
            return int(value)
        except ValueError:
            pass
    if FLOAT_RE.match(value):
        try:
            return float(value)
        except ValueError:
            pass
    if value.startswith('"') and value.endswith('"'):
        return value[1:-1]
    return value


def strip_quotes(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        return value[1:-1]
    return value


def is_abs_path(value: str) -> bool:
    return bool(ABS_LINUX_RE.match(value) or ABS_WINDOWS_RE.match(value))


def redact_abs_path(value: str) -> str:
    if not value:
        return value
    if is_abs_path(value):
        return value.split("/")[-1]
    if "\\" in value:
        candidate = value.split("\\")[-1]
        if is_abs_path(value.replace("\\", "/")):
            return candidate
    return value


def parse_kv_pairs(text: str) -> Dict[str, Any]:
    fields: Dict[str, Any] = {}
    for match in PAIR_RE.finditer(text):
        key = match.group("key")
        raw = strip_quotes(match.group("value"))
        value = parse_numeric(raw)
        fields[key] = value
    return fields


def parse_line_body(body: str) -> Dict[str, Any]:
    fields: Dict[str, Any] = {}
    grouped_body = GROUP_RE.sub(" ", body)
    for group_match in GROUP_RE.finditer(body):
        group_name = group_match.group("name")
        inner = group_match.group("body")
        for key, value in parse_kv_pairs(inner).items():
            fields[f"{group_name}.{key}"] = value
    for key, value in parse_kv_pairs(grouped_body).items():
        fields[key] = value
    if "map" in fields and isinstance(fields["map"], str):
        fields["map"] = redact_abs_path(fields["map"])
    return fields


def parse_eye(raw_eye: Any) -> Optional[str]:
    if raw_eye is None:
        return None
    if isinstance(raw_eye, int):
        return str(raw_eye)
    if not isinstance(raw_eye, str):
        return None
    if "/" in raw_eye:
        raw_eye = raw_eye.split("/", 1)[0]
    raw_eye = raw_eye.strip()
    if not raw_eye:
        return None
    return raw_eye


def record_group(tag: str, fields: Mapping[str, Any]) -> str:
    if tag == "r_vr_mirrordebug":
        return "vr/mirror"
    if tag == "r_vr_eyedebug":
        return f"vr/eye_{parse_eye(fields.get('eye')) or 'unknown'}"
    if tag == "r_perfdebug":
        vr_value = fields.get("vr", 0)
        is_vr = int(vr_value or 0) != 0
        if not is_vr:
            return "desktop"
        return f"vr/eye_{parse_eye(fields.get('eye')) or 'unknown'}"
    if tag == "r_gpudebug":
        return "gpu"
    return "other"


@dataclass
class ParsedRecord:
    tag: str
    line_no: int
    fields: Dict[str, Any]
    group: str


def parse_line(line: str, line_no: int) -> Optional[ParsedRecord]:
    match = TAG_PREFIX_RE.search(line)
    if not match:
        return None

    tag = match.group("tag")
    body = line[match.end():].strip()
    fields = parse_line_body(body)
    if not fields:
        return None

    if tag == "r_vr_eyedebug":
        required = {"eye", "total_cpu"}
        if not required.issubset(fields.keys()):
            return None
    elif tag == "r_vr_mirrordebug":
        required = {"mirror_cpu"}
        if not required.issubset(fields.keys()):
            return None
    elif tag == "r_perfdebug":
        required = {"vr", "eye"}
        if not required.issubset(fields.keys()):
            return None
    elif tag == "r_gpudebug":
        required = {"submit_host", "poll_host", "sample", "gpu_ms"}
        if not required.issubset(fields.keys()):
            return None

    group = record_group(tag, fields)
    return ParsedRecord(tag=tag, line_no=line_no, fields=dict(fields), group=group)


def percentile(values: Sequence[float], percent: float) -> float:
    if not values:
        return float("nan")
    if len(values) == 1:
        return float(values[0])
    if percent <= 0:
        return float(min(values))
    if percent >= 100:
        return float(max(values))

    sorted_values = sorted(values)
    pos = (len(sorted_values) - 1) * (percent / 100.0)
    low = math.floor(pos)
    high = math.ceil(pos)
    if low == high:
        return float(sorted_values[low])
    weight = pos - low
    return float(sorted_values[low] + (sorted_values[high] - sorted_values[low]) * weight)


def summarize_records(records: Sequence[ParsedRecord]) -> Dict[str, Any]:
    groups: Dict[str, Dict[str, Any]] = {}
    for record in records:
        group_summary = groups.setdefault(
            record.group,
            {"count": 0, "by_tag": {}},
        )
        group_summary["count"] += 1
        tag_summary = group_summary["by_tag"].setdefault(
            record.tag,
            {"count": 0, "metrics": {}, "samples": {}},
        )
        tag_summary["count"] += 1
        if record.tag == "r_gpudebug":
            sample_name = record.fields.get("sample")
            if isinstance(sample_name, str):
                sample_summary = tag_summary["samples"].setdefault(
                    sample_name,
                    {
                        "count": 0,
                        "gpu_ms": [],
                        "submit_host_counts": {},
                        "poll_host_counts": {},
                    },
                )
                sample_summary["count"] += 1
                gpu_ms = record.fields.get("gpu_ms")
                if isinstance(gpu_ms, (int, float)) and not math.isnan(float(gpu_ms)):
                    sample_summary["gpu_ms"].append(float(gpu_ms))
                submit_host = record.fields.get("submit_host")
                if isinstance(submit_host, int):
                    submit_summary = sample_summary["submit_host_counts"].setdefault(
                        submit_host, {"count": 0}
                    )
                    submit_summary["count"] += 1
                poll_host = record.fields.get("poll_host")
                if isinstance(poll_host, int):
                    poll_summary = sample_summary["poll_host_counts"].setdefault(
                        poll_host, {"count": 0}
                    )
                    poll_summary["count"] += 1
        for key, value in record.fields.items():
            if isinstance(value, (int, float)):
                metric_values = tag_summary["metrics"].setdefault(key, [])
                if not math.isnan(float(value)):
                    metric_values.append(float(value))

    for group_summary in groups.values():
        for tag_summary in group_summary["by_tag"].values():
            summarized: Dict[str, Dict[str, Any]] = {}
            for key, values in tag_summary["metrics"].items():
                summarized[key] = {
                    "count": len(values),
                    "median": percentile(values, 50),
                    "p95": percentile(values, 95),
                    "p99": percentile(values, 99),
                    "max": max(values) if values else float("nan"),
                }
            tag_summary["metrics"] = summarized
            if tag_summary.get("samples"):
                sample_summary_output = {}
                for sample_name, sample_data in tag_summary["samples"].items():
                    gpu_ms_values = sample_data.get("gpu_ms", [])
                    sample_summary_output[sample_name] = {
                        "count": sample_data.get("count", 0),
                        "gpu_ms": {
                            "count": len(gpu_ms_values),
                            "median": percentile(gpu_ms_values, 50),
                            "p95": percentile(gpu_ms_values, 95),
                            "p99": percentile(gpu_ms_values, 99),
                            "max": max(gpu_ms_values) if gpu_ms_values else float("nan"),
                        },
                        "submit_host": {
                            host: host_data["count"] for host, host_data in sorted(sample_data.get("submit_host_counts", {}).items())
                        },
                        "poll_host": {
                            host: host_data["count"] for host, host_data in sorted(sample_data.get("poll_host_counts", {}).items())
                        },
                    }
                tag_summary["samples"] = sample_summary_output

    return groups


def redact_record_map(fields: Mapping[str, Any]) -> Dict[str, Any]:
    if "map" in fields and isinstance(fields["map"], str):
        return dict(fields, map=redact_abs_path(fields["map"]))
    return dict(fields)


def analyze_lines(lines: Sequence[str]) -> Dict[str, Any]:
    records: List[ParsedRecord] = []
    unsupported: List[Dict[str, Any]] = []
    total_lines = 0
    cpu_hosts = set()
    gpu_submit_hosts = set()
    gpu_poll_hosts = set()

    for line_no, line in enumerate(lines, start=1):
        total_lines += 1
        stripped = line.strip()
        if not stripped:
            continue
        has_perf_tag = (
            "r_perfdebug" in stripped
            or "r_vr_eyedebug" in stripped
            or "r_vr_mirrordebug" in stripped
            or "r_gpudebug" in stripped
        )
        record = parse_line(stripped, line_no)
        if record:
            if record.tag == "r_perfdebug":
                host = record.fields.get("host")
                if isinstance(host, int):
                    cpu_hosts.add(host)
            elif record.tag == "r_gpudebug":
                submit_host = record.fields.get("submit_host")
                poll_host = record.fields.get("poll_host")
                if isinstance(submit_host, int):
                    gpu_submit_hosts.add(submit_host)
                if isinstance(poll_host, int):
                    gpu_poll_hosts.add(poll_host)
            record.fields = redact_record_map(record.fields)
            records.append(record)
            continue
        if has_perf_tag:
            unsupported.append(
                {"line_no": line_no, "line": stripped[:120], "reason": "unmatched_format"}
            )

    groups = summarize_records(records)
    return {
        "records_parsed": len(records),
        "total_lines": total_lines,
        "unsupported_lines": len(unsupported),
        "unsupported": unsupported[:25],
        "groups": groups,
        "host_correlation": {
            "cpu_hosts": sorted(cpu_hosts),
            "gpu_submit_hosts": sorted(gpu_submit_hosts),
            "gpu_poll_hosts": sorted(gpu_poll_hosts),
            "submit_host_matches_cpu": sorted(gpu_submit_hosts.intersection(cpu_hosts)),
            "submit_host_without_cpu": sorted(gpu_submit_hosts.difference(cpu_hosts)),
            "cpu_without_submit_host": sorted(cpu_hosts.difference(gpu_submit_hosts)),
        },
        "records": [
            {
                "tag": record.tag,
                "group": record.group,
                "line_no": record.line_no,
                "fields": record.fields,
            }
            for record in records
        ],
    }


def _fmt_value(value: Any) -> str:
    if isinstance(value, float) and not math.isfinite(value):
        return "n/a"
    if isinstance(value, float):
        return f"{value:.3f}"
    return str(value)


def _print_text_summary(summary: Mapping[str, Any], metadata: Mapping[str, Any]) -> None:
    print("Renderer perf summary")
    print(
        f"lines_total={summary['total_lines']} "
        f"parsed={summary['records_parsed']} "
        f"unsupported={summary['unsupported_lines']}"
    )
    if any(metadata.values()):
        print(
            "metadata: "
            + ", ".join(f"{k}={v}" for k, v in metadata.items() if v)
            if any(metadata.values())
            else "metadata: none"
        )
    host_corr = summary.get("host_correlation")
    if host_corr:
        print(
            "host_correlation: "
            f"cpu={host_corr.get('cpu_hosts', [])} "
            f"submit={host_corr.get('gpu_submit_hosts', [])} "
            f"poll={host_corr.get('gpu_poll_hosts', [])}"
        )
    for group_name in sorted(summary["groups"]):
        group = summary["groups"][group_name]
        print(f"\n[{group_name}] records={group['count']}")
        for tag_name, tag_summary in sorted(group["by_tag"].items()):
            print(f"  {tag_name}: count={tag_summary['count']}")
            for metric_name, metric in sorted(tag_summary["metrics"].items()):
                print(
                    f"    {metric_name}: "
                    f"count={metric['count']} "
                    f"p50={_fmt_value(metric['median'])} "
                    f"p95={_fmt_value(metric['p95'])} "
                    f"p99={_fmt_value(metric['p99'])} "
                    f"max={_fmt_value(metric['max'])}"
                )
            if not tag_summary["metrics"]:
                print("    no numeric metrics")
            if tag_summary.get("samples"):
                for sample_name in sorted(tag_summary["samples"]):
                    sample_data = tag_summary["samples"][sample_name]
                    sample_gpu = sample_data.get("gpu_ms", {})
                    print(
                        f"    sample={sample_name}: "
                        f"count={sample_data.get('count', 0)} "
                        f"gpu_ms_p50={_fmt_value(sample_gpu.get('median', float('nan')))} "
                        f"gpu_ms_p95={_fmt_value(sample_gpu.get('p95', float('nan')))} "
                        f"gpu_ms_p99={_fmt_value(sample_gpu.get('p99', float('nan')))} "
                        f"gpu_ms_max={_fmt_value(sample_gpu.get('max', float('nan')))}"
                    )


def _sanitize_metadata(value: Optional[str]) -> Optional[str]:
    if value is None:
        return None
    return redact_abs_path(value.strip()) or value.strip()

def build_metadata(args: argparse.Namespace) -> Dict[str, Optional[str]]:
    return {
        "commit": _sanitize_metadata(args.meta_commit),
        "build": _sanitize_metadata(args.meta_build),
        "gpu": _sanitize_metadata(args.meta_gpu),
        "runtime": _sanitize_metadata(args.meta_runtime),
        "headset": _sanitize_metadata(args.meta_headset),
        "map": _sanitize_metadata(args.meta_map),
        "settings": _sanitize_metadata(args.meta_settings),
    }


def emit_json(summary: Mapping[str, Any], metadata: Mapping[str, Any], out) -> None:
    safe_records = []
    for record in summary["records"]:
        safe_records.append(
            {
                "tag": record["tag"],
                "group": record["group"],
                "line_no": record["line_no"],
                "fields": record["fields"],
            }
        )
    payload = {
        "metadata": metadata,
        "counts": {
            "total_lines": summary["total_lines"],
            "parsed": summary["records_parsed"],
            "unsupported": summary["unsupported_lines"],
        },
        "host_correlation": summary["host_correlation"],
        "groups": summary["groups"],
        "unsupported": summary["unsupported"],
        "records": safe_records,
    }
    json.dump(payload, out, indent=2)
    out.write("\n")


def parse_stream(stream: TextIO) -> List[str]:
    return stream.readlines()


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", nargs="?", type=Path, default=Path("-"), help="path to log file, or '-' for stdin")
    parser.add_argument("--meta-commit", dest="meta_commit", default=None)
    parser.add_argument("--meta-build", dest="meta_build", default=None)
    parser.add_argument("--meta-gpu", dest="meta_gpu", default=None)
    parser.add_argument("--meta-runtime", dest="meta_runtime", default=None)
    parser.add_argument("--meta-headset", dest="meta_headset", default=None)
    parser.add_argument("--meta-map", dest="meta_map", default=None)
    parser.add_argument("--meta-settings", dest="meta_settings", default=None)
    parser.add_argument(
        "--format",
        dest="format",
        choices=["text", "json", "both"],
        default="both",
    )
    parser.add_argument("--output", "-o", type=Path, default=None, help="optional output file for json mode")
    return parser.parse_args(argv)


def _run(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    if str(args.input) == "-":
        lines = parse_stream(sys.stdin)
    else:
        lines = Path(args.input).read_text(encoding="utf-8", errors="replace").splitlines()

    summary = analyze_lines(lines)
    metadata = build_metadata(args)
    if args.format in {"text", "both"}:
        _print_text_summary(summary, metadata)
    if args.format in {"json", "both"}:
        if args.output is not None:
            with args.output.open("w", encoding="utf-8") as out:
                emit_json(summary, metadata, out)
        else:
            emit_json(summary, metadata, sys.stdout)
            if args.format == "both":
                sys.stdout.write("\n")
    return 0


def main() -> int:
    return _run(sys.argv[1:])


if __name__ == "__main__":
    raise SystemExit(main())
