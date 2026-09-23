#!/usr/bin/env python3
"""Compare selected-private shadow JSONL records with server GDB AUTH lines."""

import argparse
import json
import math
import re
import sys
import tempfile
from pathlib import Path

AUTH = re.compile(r"\bAUTH\s+seq=(\S+)(.*)$")
FIELD = re.compile(r"([A-Za-z_][A-Za-z_0-9]*)=([^\s]+)")


def finite(value, where):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError("{} must be a finite number".format(where))
    value = float(value)
    if not math.isfinite(value):
        raise ValueError("{} is nonfinite".format(where))
    return value


def vector(value, where):
    if not isinstance(value, list) or len(value) != 3:
        raise ValueError("{} must have three components".format(where))
    return tuple(finite(v, where) for v in value)


def reject_constant(value):
    raise ValueError("invalid JSON number {}".format(value))


def read_client(path):
    rows = {}
    with path.open(encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            if not line.strip():
                continue
            try:
                r = json.loads(line, parse_constant=reject_constant)
                if not isinstance(r, dict):
                    raise ValueError("record must be an object")
                target, ack = r["target"], r["ack"]
                if type(target) is not int or type(ack) is not int:
                    raise ValueError("ack and target must be integers")
                if target in rows:
                    raise ValueError("duplicate client target {}".format(target))
                if r.get("ok") is not True:
                    raise ValueError("shadow rejected or omitted target {}".format(target))
                if ack >= target:
                    raise ValueError("target {} is not ahead of ACK {}".format(target, ack))
                rec = {k: vector(r[k], k) for k in
                       ("base_o", "base_v", "result_o", "result_v")}
                rec["jump"] = finite(r["jump_secs"], "jump_secs")
                rec["ground"] = r["onground"]
                if type(rec["ground"]) is not int or rec["ground"] not in (0, 1):
                    raise ValueError("onground must be 0 or 1")
                if r.get("cmd_seconds") is not None and "cmd_seconds" in r:
                    finite(r["cmd_seconds"], "cmd_seconds")
                if r.get("cmd_msec") is not None and "cmd_msec" in r:
                    if type(r["cmd_msec"]) is not int:
                        raise ValueError("cmd_msec must be an integer or null")
                rows[target] = rec
            except (KeyError, TypeError, ValueError, json.JSONDecodeError) as e:
                raise ValueError("{}:{}: {}".format(path, n, e)) from e
    if not rows:
        raise ValueError("{} has no shadow records".format(path))
    return rows


def read_server(path):
    rows = {}
    with path.open(encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            m = AUTH.search(line)
            if not m:
                continue
            try:
                seq = int(m.group(1), 10)
                if seq in rows:
                    raise ValueError("duplicate AUTH target {}".format(seq))
                fields = dict(FIELD.findall(m.group(2)))
                number = lambda k: finite(float(fields[k]), k)
                origin = tuple(number(k) for k in ("x", "y", "z"))
                velocity = tuple(number(k) for k in ("vx", "vy", "vz"))
                flags = int(fields["flags"], 10)
                jump = number("jump")
                rows[seq] = (origin, velocity, flags, jump)
            except (KeyError, ValueError) as e:
                raise ValueError("{}:{}: malformed AUTH: {}".format(path, n, e)) from e
    if not rows:
        raise ValueError("{} has no AUTH seq records".format(path))
    return rows


def distance(a, b):
    d = math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))
    if not math.isfinite(d):
        raise ValueError("computed delta is nonfinite")
    return d


def compare(client, server, ptol, vtol, jtol, ground_bit, require_jump):
    stats = {"pairs": 0, "moving": 0, "ground_bad": [], "missing": [],
             "jump_pairs": 0, "pos_bad": 0, "vel_bad": 0, "jump_bad": 0}
    stats["pos"] = [0.0, None]
    stats["vel"] = [0.0, None]
    stats["jump"] = [0.0, None]
    for seq, c in client.items():
        if seq not in server:
            stats["missing"].append(seq)
            continue
        origin, velocity, flags, jump = server[seq]
        stats["pairs"] += 1
        pd = distance(c["result_o"], origin)
        vd = distance(c["result_v"], velocity)
        jd = abs(c["jump"] - jump)
        for key, delta in (("pos", pd), ("vel", vd), ("jump", jd)):
            if stats[key][1] is None or delta > stats[key][0]:
                stats[key] = [delta, seq]
        stats["pos_bad"] += pd > ptol
        stats["vel_bad"] += vd > vtol
        stats["jump_bad"] += jd > jtol
        stats["ground_bad"] += [seq] if c["ground"] != bool(flags & ground_bit) else []
        stats["jump_pairs"] += c["jump"] != 0.0 or jump != 0.0
        # Count actual replay movement, not text formatting or wire rounding.
        if (distance(c["base_o"], c["result_o"]) > .05 or
                distance(c["base_v"], c["result_v"]) > .25):
            stats["moving"] += 1

    errors = []
    if stats["missing"]:
        errors.append("{} client targets lack AUTH matches: {}".format(
            len(stats["missing"]), stats["missing"][:8]))
    if stats["pairs"] < 100:
        errors.append("{} matched pairs; need at least 100".format(stats["pairs"]))
    if stats["moving"] < 30:
        errors.append("{} movement pairs; need at least 30".format(stats["moving"]))
    for key, label in (("pos_bad", "position"), ("vel_bad", "velocity"),
                       ("jump_bad", "jump")):
        if stats[key]:
            errors.append("{} {} deltas exceed tolerance".format(stats[key], label))
    if stats["ground_bad"]:
        errors.append("FL_ONGROUND mismatch at targets {}".format(stats["ground_bad"][:8]))
    if require_jump and not stats["jump_pairs"]:
        errors.append("no matched pair has a nonzero jump timer")
    return stats, errors


def show(stats, errors, ptol, vtol, jtol):
    print("Matched pairs: {} (movement: {})".format(stats["pairs"], stats["moving"]))
    for key, label, unit, tol in (("pos", "position", "units", ptol),
                                  ("vel", "velocity", "units/s", vtol),
                                  ("jump", "jump", "s", jtol)):
        value, seq = stats[key]
        print("Max {} delta: {:.9g} {} (seq {}, limit {:.9g})".format(
            label, value, unit, seq, tol))
    print("Nonzero jump pairs: {}".format(stats["jump_pairs"]))
    for error in errors:
        print("FAIL: {}".format(error))
    if not errors:
        print("PASS")


def self_test():
    with tempfile.TemporaryDirectory() as d:
        cpath, spath = Path(d) / "client.jsonl", Path(d) / "server.log"
        clients, servers = [], []
        for seq in range(1, 101):
            x, jump = (1 if seq <= 30 else 0), (0.02 if seq == 42 else 0)
            clients.append(json.dumps({"ack": seq - 1, "target": seq,
                "base_o": [0, 0, 0], "base_v": [0, 0, 0],
                "result_o": [x, 0, 0], "result_v": [0, 0, 0],
                "jump_secs": jump, "onground": 1, "ok": True}))
            servers.append("AUTH seq={} x={} y=0 z=0 vx=0 vy=0 vz=0 flags=512 jump={}".format(
                seq, x, jump))
        cpath.write_text("\n".join(clients) + "\n")
        spath.write_text("\n".join(servers) + "\n")
        stats, errors = compare(read_client(cpath), read_server(spath), .05, .25, .001, 512, True)
        assert not errors and stats["pairs"] == 100 and stats["moving"] == 30
        cpath.write_text(clients[0] + "\n" + clients[0] + "\n")
        try:
            read_client(cpath)
        except ValueError as e:
            assert "duplicate" in str(e)
        else:
            raise AssertionError("duplicate target accepted")
    print("Self-check passed.")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("client_jsonl", nargs="?", type=Path)
    p.add_argument("server_gdb_log", nargs="?", type=Path)
    p.add_argument("--position-tolerance", type=float, default=.05, metavar="UNITS")
    p.add_argument("--velocity-tolerance", type=float, default=.25, metavar="UNITS_PER_SEC")
    p.add_argument("--jump-tolerance", type=float, default=.001, metavar="SECONDS")
    p.add_argument("--onground-bit", type=lambda s: int(s, 0), default=512, metavar="MASK")
    p.add_argument("--require-jump", action="store_true")
    p.add_argument("--self-test", action="store_true")
    a = p.parse_args()
    if a.self_test:
        self_test()
        return 0
    if not a.client_jsonl or not a.server_gdb_log:
        p.error("client_jsonl and server_gdb_log are required")
    if a.onground_bit <= 0 or any(not math.isfinite(t) or t < 0 for t in
                                  (a.position_tolerance, a.velocity_tolerance,
                                   a.jump_tolerance)):
        p.error("bit mask must be positive and tolerances finite/nonnegative")
    try:
        stats, errors = compare(read_client(a.client_jsonl), read_server(a.server_gdb_log),
            a.position_tolerance, a.velocity_tolerance, a.jump_tolerance,
            a.onground_bit, a.require_jump)
    except (OSError, ValueError) as e:
        print("ERROR: {}".format(e), file=sys.stderr)
        return 2
    show(stats, errors, a.position_tolerance, a.velocity_tolerance, a.jump_tolerance)
    return bool(errors)


if __name__ == "__main__":
    sys.exit(main())
