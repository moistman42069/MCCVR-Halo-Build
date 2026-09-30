#!/usr/bin/env python3
"""MCCVR Telemetry Analyser — standalone edition.

Single-file, standard-library-only deterministic analyser for
HaloMCCVR-Telemetry JSONL recordings.

This file contains the hardened core analysis, concern-mining diagnostics, the
provenance/query/authority layer, and the neutral sidecar layer: a mechanically
derived per-capture manifest, a capture-independent AI usage guide, a diagnostics
document with explicit provenance/parameters/signal definitions, and investigator-
directed raw retrieval helpers. It has no sibling-module dependencies.
"""
from __future__ import annotations
import argparse
import collections
import dataclasses
import hashlib
import itertools
import json
import math
import os
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Iterable, Iterator
EPS = 1e-06
AIM_PATH_NAMES = {0: 'none', 1: 'one_hand', 2: 'legacy_two_hand', 3: 'fixed_stock', 4: 'hybrid'}
DYNAMIC_EFFECTIVE_FIELDS = {'two_hand_latched', 'support_endpoint_used_grip'}
SCHEMA1_ROLE_BY_NAME = {'A_VsOffControl': 'vs_off_control', 'B_FixedHeadControl': 'fixed_head_control', 'C_FixedShoulderControl': 'fixed_shoulder_control'}
DEFAULT_HORIZONTAL_CANDIDATES = {'O_270_425': (0.27, 0.425), 'P_300_450': (0.3, 0.45), 'Q_320_460': (0.32, 0.46)}

def finite(v: Any) -> bool:
    return isinstance(v, (int, float)) and (not isinstance(v, bool)) and math.isfinite(float(v))

def fnum(v: Any) -> float | None:
    return float(v) if finite(v) else None

def pct(n: int, d: int) -> float | None:
    return 100.0 * n / d if d else None

def percentile(values: Iterable[float], q: float) -> float | None:
    xs = sorted((float(x) for x in values if finite(x)))
    if not xs:
        return None
    if len(xs) == 1:
        return xs[0]
    pos = (len(xs) - 1) * q
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    if lo == hi:
        return xs[lo]
    f = pos - lo
    return xs[lo] * (1.0 - f) + xs[hi] * f

def stats(values: Iterable[float]) -> dict[str, Any] | None:
    xs = [float(x) for x in values if finite(x)]
    if not xs:
        return None
    return {'n': len(xs), 'min': min(xs), 'p50': statistics.median(xs), 'mean': statistics.fmean(xs), 'p95': percentile(xs, 0.95), 'p99': percentile(xs, 0.99), 'max': max(xs)}

def fmt(v: Any, d: int=3) -> str:
    if v is None:
        return 'N/A'
    if isinstance(v, bool):
        return 'true' if v else 'false'
    if isinstance(v, float):
        return f'{v:.{d}f}'
    return str(v)

def fmt_sig(v: Any, sig: int=8) -> str:
    if v is None:
        return 'N/A'
    if isinstance(v, float):
        return f'{v:.{sig}g}'
    return str(v)

def fmt_pct(v: float | None, d: int=1) -> str:
    return 'N/A' if v is None else f'{v:.{d}f}%'

def fmt_stats(s: dict[str, Any] | None, d: int=3) -> str:
    if not s:
        return 'N/A'
    return f"p50 {fmt(s['p50'], d)}, p95 {fmt(s['p95'], d)}, max {fmt(s['max'], d)}"

def vec3(v: Any) -> tuple[float, float, float] | None:
    if not isinstance(v, (list, tuple)) or len(v) != 3:
        return None
    try:
        x, y, z = map(float, v)
    except (TypeError, ValueError):
        return None
    return (x, y, z) if all((math.isfinite(t) for t in (x, y, z))) else None

def quat(v: Any) -> tuple[float, float, float, float] | None:
    if not isinstance(v, (list, tuple)) or len(v) != 4:
        return None
    try:
        x, y, z, w = map(float, v)
    except (TypeError, ValueError):
        return None
    return (x, y, z, w) if all((math.isfinite(t) for t in (x, y, z, w))) else None

def vdist(a: Any, b: Any) -> float | None:
    av = vec3(a)
    bv = vec3(b)
    if av is None or bv is None:
        return None
    return math.sqrt(sum(((x - y) ** 2 for x, y in zip(av, bv))))

def vangle(a: Any, b: Any) -> float | None:
    av = vec3(a)
    bv = vec3(b)
    if av is None or bv is None:
        return None
    an = math.sqrt(sum((x * x for x in av)))
    bn = math.sqrt(sum((x * x for x in bv)))
    if an <= 1e-12 or bn <= 1e-12:
        return None
    d = sum((x * y for x, y in zip(av, bv))) / (an * bn)
    return math.degrees(math.acos(max(-1.0, min(1.0, d))))

def qangle(a: Any, b: Any) -> float | None:
    av = quat(a)
    bv = quat(b)
    if av is None or bv is None:
        return None
    an = math.sqrt(sum((x * x for x in av)))
    bn = math.sqrt(sum((x * x for x in bv)))
    if an <= 1e-12 or bn <= 1e-12:
        return None
    d = abs(sum((x * y for x, y in zip(av, bv))) / (an * bn))
    return math.degrees(2.0 * math.acos(max(-1.0, min(1.0, d))))

def qrotate_forward(q: Any) -> tuple[float, float, float] | None:
    qq = quat(q)
    if qq is None:
        return None
    x, y, z, w = qq
    vx, vy, vz = (0.0, 0.0, -1.0)
    tx = 2.0 * (y * vz - z * vy)
    ty = 2.0 * (z * vx - x * vz)
    tz = 2.0 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty), vy + w * ty + (z * tx - x * tz), vz + w * tz + (x * ty - y * tx))

def yaw_deg(v: Any) -> float | None:
    vv = vec3(v)
    if vv is None:
        return None
    x, _, z = vv
    return math.degrees(math.atan2(x, -z))

def wrap_deg(d: float) -> float:
    return (d + 180.0) % 360.0 - 180.0

def yaw_delta(old: Any, new: Any) -> float | None:
    a = yaw_deg(old)
    b = yaw_deg(new)
    return None if a is None or b is None else wrap_deg(b - a)

def normalized_blend(a: Any, b: Any, t: float) -> tuple[float, float, float] | None:
    av = vec3(a)
    bv = vec3(b)
    if av is None or bv is None or (not finite(t)):
        return None
    tt = max(0.0, min(1.0, float(t)))
    if tt <= 0.0:
        raw = av
    elif tt >= 1.0:
        raw = bv
    else:
        raw = tuple(((1.0 - tt) * x + tt * y for x, y in zip(av, bv)))
    n = math.sqrt(sum((x * x for x in raw)))
    if n <= 1e-12 or not math.isfinite(n):
        return None
    return tuple((x / n for x in raw))

def proximity_influence(distance: float, full: float, release: float) -> float | None:
    if not all((finite(x) for x in (distance, full, release))):
        return None
    distance = float(distance)
    full = float(full)
    release = float(release)
    if distance < 0 or full < 0 or release <= full:
        return None
    if distance <= full:
        return 1.0
    if distance >= release:
        return 0.0
    t = max(0.0, min(1.0, (distance - full) / (release - full)))
    smooth = t * t * (3.0 - 2.0 * t)
    return 1.0 - smooth

def horizontal_distance(a: Any, b: Any) -> float | None:
    av = vec3(a)
    bv = vec3(b)
    if av is None or bv is None:
        return None
    dx = av[0] - bv[0]
    dz = av[2] - bv[2]
    return math.sqrt(dx * dx + dz * dz)

def nested_diff(a: Any, b: Any, prefix: str='') -> list[tuple[str, Any, Any]]:
    out: list[tuple[str, Any, Any]] = []
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b)):
            p = f'{prefix}.{k}' if prefix else k
            if k not in a:
                out.append((p, None, b[k]))
            elif k not in b:
                out.append((p, a[k], None))
            else:
                out.extend(nested_diff(a[k], b[k], p))
    elif a != b:
        out.append((prefix, a, b))
    return out

def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

def analyser_sha256() -> str:
    try:
        return sha256_file(Path(__file__).resolve())
    except Exception:
        return 'unavailable'

@dataclasses.dataclass(frozen=True)
class ProfileDef:
    id: int
    name: str
    role: str | None
    label: str | None = None

class ProfileRegistry:

    def __init__(self, mapping: Any, schema_version: int) -> None:
        self.by_id: dict[int, ProfileDef] = {}
        self.by_name: dict[str, ProfileDef] = {}
        self.by_role: dict[str, list[ProfileDef]] = collections.defaultdict(list)
        if not isinstance(mapping, list):
            mapping = []
        for raw in mapping:
            if not isinstance(raw, dict):
                continue
            try:
                pid = int(raw['id'])
                name = str(raw['name'])
            except (KeyError, TypeError, ValueError):
                continue
            role = raw.get('role') or raw.get('semantic_role')
            if role is not None:
                role = str(role).strip().lower()
            elif schema_version <= 1:
                role = SCHEMA1_ROLE_BY_NAME.get(name)
            label = raw.get('label')
            d = ProfileDef(pid, name, role, str(label) if label is not None else None)
            self.by_id[pid] = d
            self.by_name[name] = d
            if role:
                self.by_role[role].append(d)

    def get(self, pid: Any, name: Any=None) -> ProfileDef:
        try:
            i = int(pid)
        except (TypeError, ValueError):
            i = -2 ** 31
        if i in self.by_id:
            return self.by_id[i]
        if name is not None and str(name) in self.by_name:
            return self.by_name[str(name)]
        return ProfileDef(i, str(name) if name is not None else f'id:{i}', None, None)

    def role_for(self, pid: Any, name: Any=None) -> str | None:
        return self.get(pid, name).role

    def unique_role(self, role: str) -> ProfileDef | None:
        xs = self.by_role.get(role, [])
        return xs[0] if len(xs) == 1 else None

    def as_json(self) -> list[dict[str, Any]]:
        return [dataclasses.asdict(x) for x in sorted(self.by_id.values(), key=lambda x: x.id)]

@dataclasses.dataclass
class ParsedRecording:
    path: Path
    source_sha256: str
    session_start: dict[str, Any]
    frames: list[dict[str, Any]]
    other_records: list[dict[str, Any]]
    session_end: dict[str, Any] | None
    trailing_partial: bool
    trailing_partial_line: int | None
    registry: ProfileRegistry
    line_count: int = 0
    malformed_middle_lines: list[dict[str, Any]] = dataclasses.field(
        default_factory=list)
    first_record_type: str | None = None
    last_record_type: str | None = None

    @property
    def schema_version(self) -> int:
        try:
            return int(self.session_start.get('schema_version', 0))
        except (TypeError, ValueError):
            return 0

    @property
    def build_commit(self) -> str:
        return str(self.session_start.get('build_commit', 'unknown'))

    @property
    def finalised(self) -> bool:
        """The source's terminal valid protocol state is session_end.

        A session_end followed by another valid record, by a malformed middle
        line, or by a truncated nonblank tail is not canonical-final.
        """
        return (self.session_end is not None
                and not self.trailing_partial
                and self.last_record_type == 'session_end')

    @property
    def parse_health_status(self) -> str:
        if self.malformed_middle_lines:
            return 'corrupt_middle'
        if self.trailing_partial:
            return 'recoverable_trailing_partial'
        return 'clean'

    @property
    def recorder_declared_file(self) -> str | None:
        declared = self.session_start.get('file')
        return str(declared) if declared is not None else None

def parse_jsonl(path: Path, tolerate_corruption: bool = False) -> ParsedRecording:
    source_hash = sha256_file(path)
    ss: dict[str, Any] | None = None
    se: dict[str, Any] | None = None
    frames: list[dict[str, Any]] = []
    other: list[dict[str, Any]] = []
    trailing_partial = False
    partial_line: int | None = None
    malformed: list[dict[str, Any]] = []
    first_type: str | None = None
    last_type: str | None = None
    line_count = 0
    with path.open('r', encoding='utf-8') as f:
        line_no = 0
        while True:
            raw = f.readline()
            if raw == '':
                break
            line_no += 1
            line_count = line_no
            if not raw.strip():
                continue
            try:
                obj = json.loads(raw)
            except json.JSONDecodeError as exc:
                resume_position = f.tell()
                rest = f.read()
                if not rest.strip():
                    trailing_partial = True
                    partial_line = line_no
                    break
                malformed.append({'line': line_no, 'error': str(exc)})
                last_type = '<malformed>'
                if not tolerate_corruption:
                    raise ValueError(f'Corrupt JSONL: malformed middle line {line_no} in {path.name}: {exc}') from exc
                # Tolerant mode: rewind to just after the malformed line so
                # later physical lines still contribute parsed records.
                f.seek(resume_position)
                continue
            if not isinstance(obj, dict):
                raise ValueError(f'Corrupt JSONL: line {line_no} is not an object')
            obj['_line'] = line_no
            typ = obj.get('type')
            if first_type is None:
                first_type = str(typ)
            last_type = str(typ)
            if typ == 'session_start':
                if ss is not None:
                    raise ValueError(f'Multiple session_start records in {path.name}')
                ss = obj
            elif typ == 'frame':
                frames.append(obj)
            elif typ == 'session_end':
                se = obj
            else:
                other.append(obj)
    if ss is None:
        raise ValueError(f'Missing session_start in {path.name}')
    schema = int(ss.get('schema_version', 0) or 0)
    registry = ProfileRegistry(ss.get('test_profile_enum_mapping'), schema)
    return ParsedRecording(path, source_hash, ss, frames, other, se,
        trailing_partial, partial_line, registry, line_count, malformed,
        first_type, last_type)

def frame_time_s(frame: dict[str, Any], first_qpc: int, qpc_freq: int) -> float | None:
    q = frame.get('capture_begin_qpc')
    if not finite(q) or qpc_freq <= 0:
        return None
    return (int(q) - first_qpc) / qpc_freq

def primary_pos(f: dict[str, Any]) -> Any:
    x = f.get('semantic_primary_aim')
    return x.get('position') if isinstance(x, dict) else None

def primary_quat(f: dict[str, Any]) -> Any:
    x = f.get('semantic_primary_aim')
    return x.get('orientation') if isinstance(x, dict) else None

def support_pos(f: dict[str, Any]) -> Any:
    return f.get('semantic_support_endpoint')

def support_quat(f: dict[str, Any]) -> Any:
    x = f.get('semantic_support_aim')
    return x.get('orientation') if isinstance(x, dict) else None

def head_pos(f: dict[str, Any]) -> Any:
    x = f.get('semantic_hmd')
    return x.get('position') if isinstance(x, dict) else None

def head_quat(f: dict[str, Any]) -> Any:
    x = f.get('semantic_hmd')
    return x.get('orientation') if isinstance(x, dict) else None

def semantic_support_grip(f: dict[str, Any], schema_version: int) -> float | None:
    """Semantic support grip after MCC handedness routing.

    Source-verified for the schema-1 recorder implementation: physical grip L/R
    are read first, then pad.gripL/pad.gripR are swapped when left_handed, and
    downstream code immediately assigns rawSupportGrip = pad.gripL. Therefore
    pad.gripL is semantic support grip for both handedness modes.

    Future schemas may add explicit semantic pad fields; prefer them if present.
    """
    pad = f.get('pad', {})
    if not isinstance(pad, dict):
        return None
    for key in ('support_grip', 'grip_support', 'semantic_support_grip'):
        if finite(pad.get(key)):
            return float(pad[key])
    if finite(pad.get('gripL')):
        return float(pad['gripL'])
    return None

def valid_support_grip_position(f: dict[str, Any]) -> list[float] | None:
    """Return support grip position only when the recorder marks it valid.

    Recorder contract:
      - support_grip_valid is authoritative.
      - support_grip_position may still contain a numeric/default/stale value
        when invalid and must not be interpreted geometrically.

    In the historical schema-1 recorder, support_grip_valid is only true while
    the Support Grip Pose path is enabled/fresh. Future schemas must preserve
    the same validity-first interpretation even if serialization details change.
    """
    if not bool(f.get('support_grip_valid')):
        return None
    p = f.get('support_grip_position')
    if not isinstance(p, list) or len(p) != 3 or not all(finite(x) for x in p):
        return None
    return [float(x) for x in p]


def semantic_primary_grip(f: dict[str, Any], schema_version: int) -> float | None:
    pad = f.get('pad', {})
    if not isinstance(pad, dict):
        return None
    for key in ('primary_grip', 'grip_primary', 'semantic_primary_grip'):
        if finite(pad.get(key)):
            return float(pad[key])
    if finite(pad.get('gripR')):
        return float(pad['gripR'])
    return None

def valid_fixed_proximity(cf: dict[str, Any]) -> float | None:
    if not isinstance(cf, dict):
        return None
    if not bool(cf.get('fixed_target_valid')):
        return None
    if not bool(cf.get('fixed_proximity_enabled')):
        return None
    if not bool(cf.get('fixed_proximity_calculated')):
        return None
    return fnum(cf.get('fixed_proximity_influence'))

def valid_fixed_effective_strength(trace: dict[str, Any]) -> float | None:
    if not isinstance(trace, dict):
        return None
    if not bool(trace.get('fixed_target_valid')):
        return None
    if not bool(trace.get('fixed_proximity_enabled')):
        return None
    if not bool(trace.get('fixed_proximity_calculated')):
        return None
    if not finite(trace.get('fixed_configured_strength')):
        return None
    return fnum(trace.get('fixed_effective_strength'))

def actual_horizontal_fields(f: dict[str, Any]) -> dict[str, Any]:
    """Best-effort schema-2+ horizontal-release consumption with aliases."""
    eff = f.get('effective_settings', {}) if isinstance(f.get('effective_settings'), dict) else {}
    tr = f.get('aim_trace', {}) if isinstance(f.get('aim_trace'), dict) else {}

    def first_num(obj: dict[str, Any], keys: tuple[str, ...]) -> float | None:
        for k in keys:
            if k in obj and finite(obj[k]):
                return float(obj[k])
        return None

    def first_bool(obj: dict[str, Any], keys: tuple[str, ...]) -> bool | None:
        for k in keys:
            if k in obj and isinstance(obj[k], bool):
                return obj[k]
        return None
    return {'enabled': first_bool(eff, ('horizontal_release_enabled', 'hybrid_horizontal_release_enabled')), 'full_m': first_num(eff, ('horizontal_release_full_m', 'hybrid_horizontal_release_full_m')), 'release_m': first_num(eff, ('horizontal_release_release_m', 'hybrid_horizontal_release_release_m')), 'reach_m': first_num(tr, ('horizontal_reach_m', 'rear_horizontal_reach_m', 'horizontal_release_reach_m')), 'h': first_num(tr, ('horizontal_release_weight', 'horizontal_release_h', 'h_release', 'h')), 'w_after_diagnostic': first_num(tr, ('w_after_diagnostic', 'w_diagnostic')), 'w_effective': fnum(tr.get('w_effective')) if tr.get('w_effective_valid') else None}

def b_state(trace: dict[str, Any]) -> str:
    if not bool(trace.get('b_attempted')):
        return 'not_attempted'
    if bool(trace.get('b_accepted')):
        return 'accepted'
    if bool(trace.get('b_extreme_rejected')):
        return 'extreme_rejected'
    return 'other_invalid_or_degenerate'

def frame_profile(f: dict[str, Any], reg: ProfileRegistry) -> ProfileDef:
    return reg.get(f.get('test_profile_id'), f.get('test_profile_name'))

def path_name(trace: dict[str, Any]) -> str:
    try:
        return AIM_PATH_NAMES.get(int(trace.get('path', 0)), f"path_{int(trace.get('path'))}")
    except (TypeError, ValueError):
        return 'unknown'

class ProfileStats:

    def __init__(self, p: ProfileDef) -> None:
        self.profile = p
        self.frames = 0
        self.path_counts = collections.Counter()
        self.two_hand_active = 0
        self.b = collections.Counter()
        self.hybrid_applicable = 0
        self.stock_contributed = 0
        self.w_natural: list[float] = []
        self.w_effective: list[float] = []
        self.seat_error: list[float] = []
        self.rear_to_target: list[float] = []
        self.horizontal_reach: list[float] = []
        self.fixed_head_prox: list[float] = []
        self.offhand_span: list[float] = []
        self.tracking_invalid = collections.Counter()

    def add(self, f: dict[str, Any]) -> None:
        self.frames += 1
        tr = f.get('aim_trace', {}) if isinstance(f.get('aim_trace'), dict) else {}
        can = f.get('canonical_aim', {}) if isinstance(f.get('canonical_aim'), dict) else {}
        pn = path_name(tr)
        self.path_counts[pn] += 1
        if can.get('two_hand_active') is True:
            self.two_hand_active += 1
        self.b[b_state(tr)] += 1
        if pn == 'hybrid':
            self.hybrid_applicable += 1
            if tr.get('stock_contributed') is True:
                self.stock_contributed += 1
        if tr.get('w_natural_valid') and finite(tr.get('w_natural')):
            self.w_natural.append(float(tr['w_natural']))
        if tr.get('w_effective_valid') and finite(tr.get('w_effective')):
            self.w_effective.append(float(tr['w_effective']))
        if tr.get('seat_valid') and finite(tr.get('seat_error_m')):
            self.seat_error.append(float(tr['seat_error_m']))
        if tr.get('release_geometry_valid') and finite(tr.get('rear_to_stock_target_distance_m')):
            self.rear_to_target.append(float(tr['rear_to_stock_target_distance_m']))
        hr = horizontal_distance(primary_pos(f), head_pos(f)) if f.get('semantic_primary_aim_valid') and f.get('head_sample_valid') else None
        if hr is not None:
            self.horizontal_reach.append(hr)
        fh = valid_fixed_proximity(f.get('cf_fixed_head', {}))
        if fh is not None:
            self.fixed_head_prox.append(fh)
        for label, key in (('primary', 'semantic_primary_aim_valid'), ('support', 'semantic_support_aim_valid'), ('support_endpoint', 'semantic_support_endpoint_valid'), ('head', 'head_sample_valid'), ('stock_head', 'stock_head_valid'), ('views', 'upcoming_views_valid')):
            if not bool(f.get(key, False)):
                self.tracking_invalid[label] += 1
        span = offhand_span_deg(f)
        if span is not None:
            self.offhand_span.append(span)

    def result(self) -> dict[str, Any]:
        app = self.hybrid_applicable
        return {'profile_id': self.profile.id, 'profile': self.profile.name, 'profile_role': self.profile.role, 'frames': self.frames, 'path_counts': dict(self.path_counts), 'two_hand_active_pct': pct(self.two_hand_active, self.frames), 'b_counts': dict(self.b), 'b_attempted_pct': pct(self.b['accepted'] + self.b['extreme_rejected'] + self.b['other_invalid_or_degenerate'], self.frames), 'b_accepted_pct': pct(self.b['accepted'], self.frames), 'b_extreme_rejected_pct': pct(self.b['extreme_rejected'], self.frames), 'b_other_invalid_pct': pct(self.b['other_invalid_or_degenerate'], self.frames), 'stock_contributed_applicable_frames': app, 'stock_contributed_pct': pct(self.stock_contributed, app), 'w_natural': stats(self.w_natural), 'w_effective': stats(self.w_effective), 'seat_error_m': stats(self.seat_error), 'rear_to_stock_target_m': stats(self.rear_to_target), 'horizontal_reach_m': stats(self.horizontal_reach), 'fixed_head_proximity_influence': stats(self.fixed_head_prox), 'offhand_i0_i1_span_deg': stats(self.offhand_span), 'offhand_span_gt_0_1deg': sum((x > 0.1 for x in self.offhand_span)), 'offhand_span_gt_1deg': sum((x > 1 for x in self.offhand_span)), 'offhand_span_gt_5deg': sum((x > 5 for x in self.offhand_span)), 'tracking_invalid_frames': dict(self.tracking_invalid)}

def offhand_span_deg(f: dict[str, Any]) -> float | None:
    tr = f.get('aim_trace', {}) if isinstance(f.get('aim_trace'), dict) else {}
    if path_name(tr) != 'hybrid' or not (tr.get('a_valid') and tr.get('b_accepted')):
        return None
    a = tr.get('a_direction')
    b = tr.get('b_direction')
    hip0 = normalized_blend(a, b, 0.0)
    hip1 = normalized_blend(a, b, 1.0)
    if hip0 is None or hip1 is None:
        return None
    w = fnum(tr.get('w_effective')) if tr.get('w_effective_valid') else None
    c = tr.get('c_direction') if tr.get('c_valid') else None
    if w is not None and c is not None:
        out0 = normalized_blend(hip0, c, w)
        out1 = normalized_blend(hip1, c, w)
        if out0 is None or out1 is None:
            return None
    else:
        out0, out1 = (hip0, hip1)
    return vangle(out0, out1)

@dataclasses.dataclass
class Transition:
    line: int
    serial: int | None
    time_s: float | None
    profile: str
    kind: str
    detail: dict[str, Any]

def transition_flags(prev: dict[str, Any], cur: dict[str, Any]) -> set[str]:
    flags: set[str] = set()
    if prev.get('prepared_serial') is not None and cur.get('prepared_serial') is not None:
        try:
            if int(cur['prepared_serial']) != int(prev['prepared_serial']) + 1:
                flags.add('serial_gap')
        except (TypeError, ValueError):
            flags.add('serial_gap')
    if prev.get('test_profile_id') != cur.get('test_profile_id') or prev.get('test_profile_name') != cur.get('test_profile_name'):
        flags.add('profile_change')
    pe = prev.get('effective_settings', {}) if isinstance(prev.get('effective_settings'), dict) else {}
    ce = cur.get('effective_settings', {}) if isinstance(cur.get('effective_settings'), dict) else {}
    if bool(pe.get('two_hand_latched')) != bool(ce.get('two_hand_latched')):
        flags.add('latch_change')
    diffs = [x for x in nested_diff(pe, ce) if x[0] not in DYNAMIC_EFFECTIVE_FIELDS]
    if diffs and 'profile_change' not in flags:
        flags.add('setting_change')
    if bool(prev.get('menu_open')) != bool(cur.get('menu_open')):
        flags.add('menu_transition')
    pp = prev.get('aim_trace', {}).get('path') if isinstance(prev.get('aim_trace'), dict) else None
    cp = cur.get('aim_trace', {}).get('path') if isinstance(cur.get('aim_trace'), dict) else None
    if pp != cp:
        flags.add('path_change')
    if prev.get('contact_space_epoch') != cur.get('contact_space_epoch') or prev.get('contact_space_change_at_ns') != cur.get('contact_space_change_at_ns'):
        flags.add('reference_space_change')
    valid_keys = ('semantic_primary_aim_valid', 'semantic_support_aim_valid', 'semantic_support_endpoint_valid', 'head_sample_valid', 'upcoming_views_valid')
    if any((bool(prev.get(k)) != bool(cur.get(k)) for k in valid_keys)):
        flags.add('tracking_validity_change')
    if not all((bool(cur.get(k)) for k in ('semantic_primary_aim_valid', 'semantic_support_aim_valid', 'semantic_support_endpoint_valid', 'head_sample_valid'))):
        flags.add('tracking_invalid')
    return flags

def aim_jump(prev: dict[str, Any], cur: dict[str, Any]) -> float | None:
    a = prev.get('canonical_aim', {}).get('forward') if isinstance(prev.get('canonical_aim'), dict) else None
    b = cur.get('canonical_aim', {}).get('forward') if isinstance(cur.get('canonical_aim'), dict) else None
    return vangle(a, b)
SIGNALS = {'canonical': lambda f: f.get('canonical_aim', {}).get('forward') if isinstance(f.get('canonical_aim'), dict) else None, 'c': lambda f: f.get('aim_trace', {}).get('c_direction') if isinstance(f.get('aim_trace'), dict) and f.get('aim_trace', {}).get('c_valid') else None, 'fixed_head': lambda f: f.get('cf_fixed_head', {}).get('aim', {}).get('forward') if isinstance(f.get('cf_fixed_head'), dict) else None, 'fixed_shoulder': lambda f: f.get('cf_fixed_shoulder', {}).get('aim', {}).get('forward') if isinstance(f.get('cf_fixed_shoulder'), dict) else None, 'vs_off': lambda f: f.get('cf_vs_off', {}).get('aim', {}).get('forward') if isinstance(f.get('cf_vs_off'), dict) else None}

def regression(x: list[float], y: list[float]) -> dict[str, Any]:
    pairs = [(a, b) for a, b in zip(x, y) if finite(a) and finite(b) and (abs(a) > 1e-12)]
    if not pairs:
        return {'n': 0, 'slope': None, 'opposite_fraction': None, 'median_abs_ratio': None}
    xx = sum((a * a for a, _ in pairs))
    xy = sum((a * b for a, b in pairs))
    ratios = [abs(b / a) for a, b in pairs]
    opp = sum((1 for a, b in pairs if a * b < 0))
    return {'n': len(pairs), 'slope': xy / xx if xx else None, 'opposite_fraction': opp / len(pairs), 'median_abs_ratio': statistics.median(ratios)}

def lag_table(head_steps: list[float], aim_steps: list[float]) -> dict[str, Any]:
    out = {}
    n = min(len(head_steps), len(aim_steps))
    for lag in (-2, -1, 0, 1, 2):
        xs = []
        ys = []
        for i in range(n):
            j = i + lag
            if 0 <= j < n:
                xs.append(head_steps[i])
                ys.append(aim_steps[j])
        out[str(lag)] = regression(xs, ys)
    return out

def unwrap_yaws(vectors: list[Any]) -> list[float] | None:
    ys = [yaw_deg(v) for v in vectors]
    if any((y is None for y in ys)):
        return None
    out = [float(ys[0])]
    for y in ys[1:]:
        out.append(out[-1] + wrap_deg(float(y) - out[-1]))
    return out

def signal_episode_metrics(frames: list[dict[str, Any]], fn) -> dict[str, Any] | None:
    vecs = [fn(f) for f in frames]
    ys = unwrap_yaws(vecs)
    if ys is None or len(ys) < 2:
        return None
    steps = [ys[i] - ys[i - 1] for i in range(1, len(ys))]
    return {'net_excursion_deg': ys[-1] - ys[0], 'peak_to_peak_deg': max(ys) - min(ys), 'path_abs_sum_deg': sum((abs(x) for x in steps)), 'steps': steps}

def head_turn_episodes(rec: ParsedRecording, args: argparse.Namespace, times: list[float | None]) -> list[dict[str, Any]]:
    fs = rec.frames
    episodes = []
    current: list[int] = []
    current_sign = 0

    def step_ok(i: int) -> tuple[bool, float | None]:
        if i <= 0:
            return (False, None)
        p = fs[i - 1]
        c = fs[i]
        if transition_flags(p, c):
            return (False, None)
        pd = vdist(primary_pos(p), primary_pos(c))
        sd = vdist(support_pos(p), support_pos(c))
        pr = qangle(primary_quat(p), primary_quat(c))
        sr = qangle(support_quat(p), support_quat(c))
        hf0 = qrotate_forward(head_quat(p))
        hf1 = qrotate_forward(head_quat(c))
        dh = yaw_delta(hf0, hf1)
        if None in (pd, sd, pr, sr, dh):
            return (False, dh)
        if pd * 1000 > args.stationary_hand_mm or sd * 1000 > args.stationary_hand_mm:
            return (False, dh)
        if pr > args.stationary_hand_rotation_deg or sr > args.stationary_hand_rotation_deg:
            return (False, dh)
        if abs(dh) < args.head_episode_step_min_deg or abs(dh) > args.head_yaw_max_deg:
            return (False, dh)
        return (True, dh)

    def finish(indices: list[int]):
        if len(indices) < args.head_episode_min_frames:
            return
        frames = [fs[i] for i in indices]
        head_vecs = [qrotate_forward(head_quat(f)) for f in frames]
        hys = unwrap_yaws(head_vecs)
        if hys is None:
            return
        excursion = hys[-1] - hys[0]
        if abs(excursion) < args.head_episode_min_excursion_deg:
            return
        head_steps = [hys[i] - hys[i - 1] for i in range(1, len(hys))]
        pdef = frame_profile(frames[0], rec.registry)
        e = {'profile': pdef.name, 'profile_role': pdef.role, 'start_line': frames[0]['_line'], 'end_line': frames[-1]['_line'], 'start_serial': frames[0].get('prepared_serial'), 'end_serial': frames[-1].get('prepared_serial'), 'start_time_s': times[indices[0]], 'end_time_s': times[indices[-1]], 'frames': len(frames), 'head_yaw_excursion_deg': excursion, 'head_yaw_peak_to_peak_deg': max(hys) - min(hys), 'signals': {}}
        for name, fn in SIGNALS.items():
            sm = signal_episode_metrics(frames, fn)
            if sm is None:
                continue
            steps = sm.pop('steps')
            sm.update(regression(head_steps, steps))
            sm['lag_table'] = lag_table(head_steps, steps)
            e['signals'][name] = sm
        episodes.append(e)
    for i in range(1, len(fs)):
        ok, dh = step_ok(i)
        sign = 1 if dh is not None and dh > 0 else -1 if dh is not None and dh < 0 else 0
        if ok and (not current or current_sign == sign):
            if not current:
                current = [i - 1, i]
                current_sign = sign
            else:
                current.append(i)
        else:
            if current:
                finish(current)
            current = []
            current_sign = 0
            if ok:
                current = [i - 1, i]
                current_sign = sign
    if current:
        finish(current)
    return episodes

def setting_changes(rec: ParsedRecording, times: list[float | None]) -> list[dict[str, Any]]:
    out = []
    fs = rec.frames
    for i in range(1, len(fs)):
        p, c = (fs[i - 1], fs[i])
        if p.get('test_profile_id') != c.get('test_profile_id') or p.get('test_profile_name') != c.get('test_profile_name'):
            continue
        pe = p.get('effective_settings', {}) if isinstance(p.get('effective_settings'), dict) else {}
        ce = c.get('effective_settings', {}) if isinstance(c.get('effective_settings'), dict) else {}
        for field, old, new in nested_diff(pe, ce):
            if field in DYNAMIC_EFFECTIVE_FIELDS:
                continue
            out.append({'source': 'effective_frame_diff', 'line': c['_line'], 'serial': c.get('prepared_serial'), 'time_s': times[i], 'profile': frame_profile(c, rec.registry).name, 'field': field, 'from': old, 'to': new, 'menu_context': bool(p.get('menu_open') or c.get('menu_open'))})
    for obj in rec.other_records:
        typ = str(obj.get('type', ''))
        ev = str(obj.get('event', obj.get('name', '')))
        provider = str(obj.get('provider', obj.get('channel', '')))
        if provider in ('settings', 'config', 'menu') and 'setting' in ev.lower() or ev.lower() in ('settings.changed', 'setting_changed'):
            payload = obj.get('payload', {}) if isinstance(obj.get('payload'), dict) else obj
            out.append({'source': 'raw_setting_event', 'line': obj.get('_line'), 'serial': obj.get('prepared_serial'), 'time_s': payload.get('time_s'), 'profile': payload.get('profile_name'), 'field': payload.get('setting_id', payload.get('field')), 'from': payload.get('raw_old_value', payload.get('old')), 'to': payload.get('raw_new_value', payload.get('new')), 'resulting_effective': payload.get('resulting_effective_value'), 'menu_context': payload.get('menu_open')})
    out.sort(key=lambda x: (x.get('line') or 10 ** 18, str(x.get('field'))))
    return out

def profile_segments(rec: ParsedRecording, times: list[float | None]) -> list[dict[str, Any]]:
    if not rec.frames:
        return []
    fs = rec.frames
    out = []
    start = 0
    for i in range(1, len(fs) + 1):
        if i == len(fs) or fs[i].get('test_profile_id') != fs[start].get('test_profile_id') or fs[i].get('test_profile_name') != fs[start].get('test_profile_name'):
            p = frame_profile(fs[start], rec.registry)
            out.append({'profile_id': p.id, 'profile': p.name, 'profile_role': p.role, 'start_line': fs[start]['_line'], 'end_line': fs[i - 1]['_line'], 'start_serial': fs[start].get('prepared_serial'), 'end_serial': fs[i - 1].get('prepared_serial'), 'start_time_s': times[start], 'end_time_s': times[i - 1], 'frames': i - start})
            start = i
    return out

def horizontal_analysis(rec: ParsedRecording, args: argparse.Namespace) -> dict[str, Any]:
    per_profile: dict[str, dict[str, Any]] = {}
    reach_breaks = sorted(set([0.27, 0.3, 0.32, 0.425, 0.45, 0.46]))
    candidates = parse_horizontal_candidates(args.horizontal_candidates)
    actual_schema2 = []
    for f in rec.frames:
        p = frame_profile(f, rec.registry).name
        d = per_profile.setdefault(p, {'reach': [], 'rows': [], 'candidate_weights': {k: [] for k in candidates}})
        reach = horizontal_distance(primary_pos(f), head_pos(f)) if f.get('semantic_primary_aim_valid') and f.get('head_sample_valid') else None
        tr = f.get('aim_trace', {}) if isinstance(f.get('aim_trace'), dict) else {}
        wn = fnum(tr.get('w_natural')) if tr.get('w_natural_valid') else None
        fh = valid_fixed_proximity(f.get('cf_fixed_head', {}))
        if reach is not None:
            d['reach'].append(reach)
            d['rows'].append((reach, wn, fh))
            for k, (full, rel) in candidates.items():
                h = proximity_influence(reach, full, rel)
                if h is not None:
                    d['candidate_weights'][k].append(h)
        ah = actual_horizontal_fields(f)
        if any((v is not None for v in ah.values())):
            actual_schema2.append({'line': f['_line'], 'profile': p, **ah, 'computed_reach_m': reach})
    out = {}
    edges = [-math.inf] + reach_breaks + [math.inf]
    for p, d in per_profile.items():
        bins = []
        for lo, hi in zip(edges[:-1], edges[1:]):
            rows = [r for r in d['rows'] if r[0] >= lo and r[0] < hi]
            if not rows:
                continue
            ws = [r[1] for r in rows if r[1] is not None]
            fhs = [r[2] for r in rows if r[2] is not None]
            bins.append({'lo_m': None if math.isinf(lo) else lo, 'hi_m': None if math.isinf(hi) else hi, 'frames': len(rows), 'w_natural': stats(ws), 'w_ge_0_9_pct': pct(sum((x >= 0.9 for x in ws)), len(ws)), 'fixed_head_proximity': stats(fhs), 'fixed_head_le_0_1_pct': pct(sum((x <= 0.1 for x in fhs)), len(fhs))})
        out[p] = {'horizontal_reach_m': stats(d['reach']), 'reach_bins': bins, 'offline_candidates': {k: {'full_m': candidates[k][0], 'release_m': candidates[k][1], 'h': stats(v), 'frames_h_ge_0_9': sum((x >= 0.9 for x in v)), 'frames_h_le_0_1': sum((x <= 0.1 for x in v))} for k, v in d['candidate_weights'].items()}}
    return {'per_profile': out, 'actual_horizontal_fields': _debug_per_frame_rows(args, actual_schema2), 'offline_candidate_semantics': 'H is an offline cubic horizontal rear-release factor only; it is not production behaviour in schema-1 recordings.', 'candidates': {k: {'full_m': v[0], 'release_m': v[1]} for k, v in candidates.items()}}

def parse_horizontal_candidates(text: str) -> dict[str, tuple[float, float]]:
    if not text.strip():
        return dict(DEFAULT_HORIZONTAL_CANDIDATES)
    out = {}
    for item in text.split(','):
        name, a, b = item.split(':', 2)
        out[name.strip()] = (float(a), float(b))
    return out

def jump_analysis(rec: ParsedRecording, times: list[float | None], args: argparse.Namespace) -> dict[str, Any]:
    allj = []
    steady = []
    for i in range(1, len(rec.frames)):
        p, c = (rec.frames[i - 1], rec.frames[i])
        j = aim_jump(p, c)
        if j is None:
            continue
        flags = sorted(transition_flags(p, c))
        row = {'line': c['_line'], 'serial': c.get('prepared_serial'), 'time_s': times[i], 'profile': frame_profile(c, rec.registry).name, 'jump_deg': j, 'transition_flags': flags, 'b': f"{b_state(p.get('aim_trace', {}))}->{b_state(c.get('aim_trace', {}))}", 'w_prev': fnum(p.get('aim_trace', {}).get('w_effective')) if p.get('aim_trace', {}).get('w_effective_valid') else None, 'w_now': fnum(c.get('aim_trace', {}).get('w_effective')) if c.get('aim_trace', {}).get('w_effective_valid') else None}
        allj.append(row)
        excluded = {'profile_change', 'setting_change', 'menu_transition', 'latch_change', 'path_change', 'reference_space_change', 'tracking_validity_change', 'tracking_invalid', 'serial_gap'}
        if not excluded.intersection(flags):
            steady.append(row)
    allj.sort(key=lambda x: -x['jump_deg'])
    steady.sort(key=lambda x: -x['jump_deg'])
    return {'all_top': allj[:args.top_jumps], 'steady_state_top': steady[:args.top_jumps], 'all_stats': stats((x['jump_deg'] for x in allj)), 'steady_state_stats': stats((x['jump_deg'] for x in steady))}

def health(rec: ParsedRecording, times: list[float | None]) -> dict[str, Any]:
    se = rec.session_end or {}
    ss = rec.session_start
    gaps = []
    hot = []
    fq = int(ss.get('qpc_frequency', 0) or 0)
    for i, f in enumerate(rec.frames):
        if fq > 0 and finite(f.get('capture_begin_qpc')) and finite(f.get('capture_end_qpc')):
            hot.append((float(f['capture_end_qpc']) - float(f['capture_begin_qpc'])) * 1000000.0 / fq)
        if i and finite(rec.frames[i - 1].get('prepared_serial')) and finite(f.get('prepared_serial')):
            a = int(rec.frames[i - 1]['prepared_serial'])
            b = int(f['prepared_serial'])
            if b != a + 1:
                gaps.append({'line': f['_line'], 'previous': a, 'current': b, 'delta': b - a})
    clean = bool(se.get('clean_stop')) if rec.session_end is not None else False
    status = 'clean' if clean and (not rec.trailing_partial) else 'unclean/trailing_partial' if rec.trailing_partial else 'unclean/no_session_end_or_clean_stop'
    return {'status': status, 'trailing_partial': rec.trailing_partial, 'trailing_partial_line': rec.trailing_partial_line, 'frames_parsed': len(rec.frames), 'session_end': se, 'serial_gaps': gaps, 'hotpath_us': stats(hot), 'accounting_ok': se.get('producer_calls') == se.get('duplicate_serial_suppressed', 0) + se.get('enqueued', 0) + se.get('dropped_queue_full', 0) if rec.session_end else None, 'written_matches_frames': se.get('written') == len(rec.frames) if rec.session_end and finite(se.get('written')) else None}

def load_annotations(path: Path | None) -> dict[str, Any] | None:
    if path is None:
        return None
    obj = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(obj, dict):
        raise ValueError('Annotation sidecar must be a JSON object')
    return obj

def annotation_analysis(rec: ParsedRecording, times: list[float | None], ann: dict[str, Any] | None) -> dict[str, Any] | None:
    if not ann:
        return None
    out = {'run_notes': ann.get('run_notes'), 'profile_labels': ann.get('profile_labels', {}), 'intervals': []}
    for raw in ann.get('intervals', []):
        if not isinstance(raw, dict) or not finite(raw.get('start_s')) or (not finite(raw.get('end_s'))):
            continue
        a = float(raw['start_s'])
        b = float(raw['end_s'])
        idx = [i for i, t in enumerate(times) if t is not None and a <= t <= b]
        fs = [rec.frames[i] for i in idx]
        ws = []
        hrs = []
        jumps = []
        prof = collections.Counter()
        for j, f in enumerate(fs):
            prof[frame_profile(f, rec.registry).name] += 1
            tr = f.get('aim_trace', {})
            if tr.get('w_effective_valid') and finite(tr.get('w_effective')):
                ws.append(float(tr['w_effective']))
            hr = horizontal_distance(primary_pos(f), head_pos(f))
            if hr is not None:
                hrs.append(hr)
            if j:
                x = aim_jump(fs[j - 1], f)
                if x is not None:
                    jumps.append(x)
        out['intervals'].append({'label': raw.get('label'), 'start_s': a, 'end_s': b, 'frames': len(fs), 'profiles': dict(prof), 'w_effective': stats(ws), 'horizontal_reach_m': stats(hrs), 'aim_jump_deg': stats(jumps), 'notes': raw.get('notes')})
    return out

def _runs(indices: list[int]) -> list[tuple[int, int]]:
    if not indices:
        return []
    out = []
    a = b = indices[0]
    for x in indices[1:]:
        if x == b + 1:
            b = x
        else:
            out.append((a, b))
            a = b = x
    out.append((a, b))
    return out

def grip_latch_analysis(rec: ParsedRecording, args: argparse.Namespace, times: list[float | None]) -> dict[str, Any]:
    fs = rec.frames
    schema = rec.schema_version
    held_unlatched = []
    held_latched_aim_inactive = []
    release_while_held = []
    transitions = []
    idx_unlatched = []
    idx_aimdrop = []
    for i, f in enumerate(fs):
        grip = semantic_support_grip(f, schema)
        held = grip is not None and grip >= args.grip_held_threshold
        eff = f.get('effective_settings', {}) if isinstance(f.get('effective_settings'), dict) else {}
        latched = bool(eff.get('two_hand_latched', False))
        aim = bool(f.get('canonical_aim', {}).get('two_hand_active', False))
        if held and (not latched):
            idx_unlatched.append(i)
        if held and latched and (not aim):
            idx_aimdrop.append(i)
        if i:
            pe = fs[i - 1].get('effective_settings', {}) if isinstance(fs[i - 1].get('effective_settings'), dict) else {}
            prev_latched = bool(pe.get('two_hand_latched', False))
            prev_aim = bool(fs[i - 1].get('canonical_aim', {}).get('two_hand_active', False))
            if prev_latched != latched:
                row = {'line': f['_line'], 'serial': f.get('prepared_serial'), 'time_s': times[i], 'profile': frame_profile(f, rec.registry).name, 'from': prev_latched, 'to': latched, 'support_grip': grip, 'support_grip_held': held, 'two_hand_active': aim, 'b_state': b_state(f.get('aim_trace', {}))}
                transitions.append(row)
                if prev_latched and (not latched) and held:
                    release_while_held.append(row)
            if prev_aim and (not aim) and held and latched:
                pass

    def conv(runs):
        out = []
        for a, b in runs:
            fa, fb = (fs[a], fs[b])
            out.append({'start_line': fa['_line'], 'end_line': fb['_line'], 'start_serial': fa.get('prepared_serial'), 'end_serial': fb.get('prepared_serial'), 'start_time_s': times[a], 'end_time_s': times[b], 'frames': b - a + 1, 'profile': frame_profile(fa, rec.registry).name, 'grip_start': semantic_support_grip(fa, schema), 'grip_end': semantic_support_grip(fb, schema)})
        return out
    return {'support_grip_semantics': 'semantic support grip = routed pad.gripL for schema-1/current implementation; no handedness-dependent re-selection', 'latch_transitions': transitions, 'latch_released_while_grip_held': release_while_held, 'grip_held_unlatched_runs': conv(_runs(idx_unlatched)), 'grip_and_latch_held_but_two_hand_aim_inactive_runs': conv(_runs(idx_aimdrop))}

def analyse_recording(rec: ParsedRecording, args: argparse.Namespace, annotations: dict[str, Any] | None=None) -> dict[str, Any]:
    fq = int(rec.session_start.get('qpc_frequency', 0) or 0)
    first_qpc = int(rec.frames[0].get('capture_begin_qpc', 0)) if rec.frames else 0
    times = [frame_time_s(f, first_qpc, fq) for f in rec.frames]
    profiles: dict[tuple[int, str], ProfileStats] = {}
    for f in rec.frames:
        p = frame_profile(f, rec.registry)
        key = (p.id, p.name)
        profiles.setdefault(key, ProfileStats(p)).add(f)
    settings = setting_changes(rec, times)
    episodes = head_turn_episodes(rec, args, times)
    result = {'analysis_metadata': {'analyser_version': ANALYSER_VERSION, 'analyser_sha256': analyser_sha256(), 'source_file': rec.path.name, 'source_jsonl_sha256': rec.source_sha256, 'schema_version': rec.schema_version, 'build_commit': rec.build_commit, 'cli_thresholds': threshold_args(args)}, 'session_start': {k: v for k, v in rec.session_start.items() if k != '_line'}, 'profile_registry': rec.registry.as_json(), 'health': health(rec, times), 'profile_segments': profile_segments(rec, times), 'profile_summaries': [x.result() for _, x in sorted(profiles.items(), key=lambda kv: (kv[0][0], kv[0][1]))], 'setting_changes': settings, 'grip_latch': grip_latch_analysis(rec, args, times), 'head_yaw_episodes': episodes, 'horizontal_release': horizontal_analysis(rec, args), 'aim_jumps': jump_analysis(rec, times, args), 'annotations': annotation_analysis(rec, times, annotations), 'semantic_notes': {'support_grip': 'pad.gripL is treated as semantic support grip. Source-verified for schema-1 MCCVR: physical grips are read, pad L/R are swapped for left-handed weapon mode, then downstream rawSupportGrip=pad.gripL.', 'support_grip_position': 'support_grip_valid is authoritative. support_grip_position is geometrically meaningful only when support_grip_valid is true; otherwise it is treated as N/A regardless of serialized numeric contents.', 'profile_roles': 'Schema-2+ roles come from session_start mapping. Schema-1 control roles are inferred by stable profile name, never numeric ID.', 'missing_values': 'Missing/inapplicable values remain null/N/A and are never coerced to numeric zero for causal logic.', 'horizontal_offline': 'O/P/Q calculations on schema-1 files are offline candidate simulations only, not recorded production behaviour.'}}
    return result

def threshold_args(args: argparse.Namespace) -> dict[str, Any]:
    keys = ('grip_held_threshold', 'top_jumps', 'stationary_hand_mm', 'stationary_hand_rotation_deg', 'head_yaw_max_deg', 'head_episode_step_min_deg', 'head_episode_min_excursion_deg', 'head_episode_min_frames', 'horizontal_candidates')
    return {k: getattr(args, k) for k in keys}

def markdown_report(r: dict[str, Any]) -> str:
    md = r['analysis_metadata']
    h = r['health']
    out = [f"# MCCVR Telemetry Analysis v3 — {md['source_file']}", '', '## Reproducibility / recorder health', '', f"- Analyser: `{md['analyser_version']}` / SHA-256 `{md['analyser_sha256']}`", f"- Source JSONL SHA-256: `{md['source_jsonl_sha256']}`", f"- Schema: `{md['schema_version']}`; build: `{md['build_commit']}`", f"- Parse/stop status: **{h['status']}**", f"- Frames parsed: **{h['frames_parsed']}**; serial gaps: **{len(h['serial_gaps'])}**", f"- Recorder accounting identity: `{h['accounting_ok']}`; written==parsed frames: `{h['written_matches_frames']}`", f"- Hot-path µs: {fmt_stats(h['hotpath_us'], 2)}", f"- Exact thresholds: `{json.dumps(md['cli_thresholds'], sort_keys=True, separators=(',', ':'))}`", '', '## Profile registry', '', '| ID | Stable name | Semantic role | Label |', '|---:|---|---|---|']
    for p in r['profile_registry']:
        out.append(f"| {p['id']} | `{p['name']}` | `{p.get('role') or '-'}` | {p.get('label') or '-'} |")
    out += ['', '## Profile timeline', '', '| Lines | Serial | Time (s) | Profile | Role |', '|---|---|---|---|---|']
    for s in r['profile_segments']:
        out.append(f"| {s['start_line']}–{s['end_line']} | {s['start_serial']}–{s['end_serial']} | {fmt(s['start_time_s'])}–{fmt(s['end_time_s'])} | `{s['profile']}` | `{s.get('profile_role') or '-'}` |")
    out += ['', '## Profile summary', '', '`Stock contributed` is reported only for Hybrid-path frames; fixed/legacy paths show N/A rather than a misleading zero.', '', '| Profile | Frames | Two-hand active | B accepted | B extreme reject | B other invalid | Stock contributed | W natural | Horizontal reach | Offhand I=0↔1 span |', '|---|---:|---:|---:|---:|---:|---:|---|---|---|']
    for p in r['profile_summaries']:
        out.append(f"| `{p['profile']}` | {p['frames']} | {fmt_pct(p['two_hand_active_pct'])} | {fmt_pct(p['b_accepted_pct'])} | {fmt_pct(p['b_extreme_rejected_pct'])} | {fmt_pct(p['b_other_invalid_pct'])} | {fmt_pct(p['stock_contributed_pct'])} | {fmt_stats(p['w_natural'])} | {fmt_stats(p['horizontal_reach_m'])} | {fmt_stats(p['offhand_i0_i1_span_deg'])} |")
    out += ['', '## Effective/manual setting changes', '', 'Schema-1 can observe effective same-profile changes; schema-2 raw `settings.changed` events are also consumed when present. A raw Config edit hidden by an active profile remains unknowable unless the recorder emits that event.', '']
    if not r['setting_changes']:
        out.append('None.')
    else:
        out += ['| Line | Time | Profile | Source | Field | From → To | Menu context |', '|---:|---:|---|---|---|---|---|']
        for x in r['setting_changes']:
            out.append(f"| {x.get('line', '-')} | {fmt(x.get('time_s'))} | `{x.get('profile') or '-'}` | `{x['source']}` | `{x.get('field')}` | `{x.get('from')}` → `{x.get('to')}` | {x.get('menu_context')} |")
    gl = r['grip_latch']
    out += ['', '## Grip / latch / two-hand-state episodes', '', gl['support_grip_semantics'], '', f"Latch releases while semantic support grip remained >= threshold: **{len(gl['latch_released_while_grip_held'])}**.", f"Grip-held but latch-false episodes: **{len(gl['grip_held_unlatched_runs'])}**.", f"Grip+latch held but canonical two-hand aim inactive episodes: **{len(gl['grip_and_latch_held_but_two_hand_aim_inactive_runs'])}**.", '', 'The last category is a solver/presentation-state mismatch index; it does **not** prove a rendered IK hand visibly detached.', '']
    if gl['grip_and_latch_held_but_two_hand_aim_inactive_runs']:
        out += ['| Lines | Time | Frames | Profile | Grip start→end |', '|---|---:|---:|---|---|']
        for x in gl['grip_and_latch_held_but_two_hand_aim_inactive_runs'][:30]:
            out.append(f"| {x['start_line']}–{x['end_line']} | {fmt(x['start_time_s'])}–{fmt(x['end_time_s'])} | {x['frames']} | `{x['profile']}` | {fmt(x['grip_start'], 3)}→{fmt(x['grip_end'], 3)} |")
    out += ['', '## Head-yaw stationary-hand episodes', '', 'Episodes exclude profile/settings/menu/latch/path/reference-space/tracking transitions. Lag table semantics: lag `+1` pairs a head-yaw step with the aim step **one frame later**; no best lag is selected.', '']
    if not r['head_yaw_episodes']:
        out.append('None matching thresholds.')
    else:
        for i, e in enumerate(r['head_yaw_episodes'], 1):
            out += [f"### Episode {i}: `{e['profile']}` lines {e['start_line']}–{e['end_line']}", '', f"Head yaw excursion: **{fmt(e['head_yaw_excursion_deg'], 2)}°**; peak-to-peak **{fmt(e['head_yaw_peak_to_peak_deg'], 2)}°**; frames {e['frames']}.", '', '| Signal | Net aim yaw | Peak-to-peak | Opposite fraction | Median |aim/head| | Slope |', '|---|---:|---:|---:|---:|---:|']
            for name, s in e['signals'].items():
                out.append(f"| `{name}` | {fmt(s['net_excursion_deg'], 2)}° | {fmt(s['peak_to_peak_deg'], 2)}° | {fmt_pct(s['opposite_fraction'] * 100 if s['opposite_fraction'] is not None else None)} | {fmt(s['median_abs_ratio'], 3)} | {fmt(s['slope'], 3)} |")
            can = e['signals'].get('canonical')
            if can:
                out += ['', 'Canonical fixed lag slopes:', '', '| Lag frames | n | slope | opposite fraction |', '|---:|---:|---:|---:|']
                for lag in ('-2', '-1', '0', '1', '2'):
                    x = can['lag_table'][lag]
                    out.append(f"| {lag} | {x['n']} | {fmt(x['slope'], 3)} | {fmt_pct(x['opposite_fraction'] * 100 if x['opposite_fraction'] is not None else None)} |")
            out.append('')
    out += ['## Horizontal rear-release analysis', '', 'Horizontal reach is computed directly from semantic primary P and semantic HMD position in LOCAL XZ; Y is intentionally ignored. O/P/Q rows below are **offline candidate simulations** unless actual schema-2 horizontal fields are present.', '']
    for p, d in r['horizontal_release']['per_profile'].items():
        out += [f'### `{p}`', '', f"Reach distribution: {fmt_stats(d['horizontal_reach_m'])}", '', '| Reach bin m | Frames | W natural | W≥0.9 | Fixed Head proximity | Fixed Head≤0.1 |', '|---|---:|---|---:|---|---:|']
        for b in d['reach_bins']:
            lo = '-∞' if b['lo_m'] is None else f"{b['lo_m']:.3f}"
            hi = '∞' if b['hi_m'] is None else f"{b['hi_m']:.3f}"
            out.append(f"| {lo}–{hi} | {b['frames']} | {fmt_stats(b['w_natural'])} | {fmt_pct(b['w_ge_0_9_pct'])} | {fmt_stats(b['fixed_head_proximity'])} | {fmt_pct(b['fixed_head_le_0_1_pct'])} |")
        out += ['', 'Offline horizontal candidates:', '', '| Candidate | Full/release | H distribution | H≥0.9 frames | H≤0.1 frames |', '|---|---|---|---:|---:|']
        for name, c in d['offline_candidates'].items():
            out.append(f"| `{name}` | {c['full_m']:.3f}/{c['release_m']:.3f} | {fmt_stats(c['h'])} | {c['frames_h_ge_0_9']} | {c['frames_h_le_0_1']} |")
        out.append('')
    out += ['## Offhand Influence deterministic sensitivity', '', 'Recomposes recorded A/B/C/effective-W at I=0 and I=1. This quantifies the maximum authority the slider could have had on that frame under the recorded composition.', '', '| Profile | I=0↔1 angular span | >0.1° | >1° | >5° |', '|---|---|---:|---:|---:|']
    for p in r['profile_summaries']:
        out.append(f"| `{p['profile']}` | {fmt_stats(p['offhand_i0_i1_span_deg'])} | {p['offhand_span_gt_0_1deg']} | {p['offhand_span_gt_1deg']} | {p['offhand_span_gt_5deg']} |")
    out += ['', '## Largest adjacent-frame aim jumps — all', '', '| Line | Time | Profile | Jump | Transition flags | B state | W |', '|---:|---:|---|---:|---|---|---|']
    for x in r['aim_jumps']['all_top']:
        out.append(f"| {x['line']} | {fmt(x['time_s'])} | `{x['profile']}` | {x['jump_deg']:.2f}° | `{','.join(x['transition_flags']) or '-'}` | `{x['b']}` | {fmt_sig(x['w_prev'])}→{fmt_sig(x['w_now'])} |")
    out += ['', '## Largest adjacent-frame aim jumps — steady-state', '', 'Excludes profile/settings/menu/latch/path/reference-space/tracking/serial transitions, so internal gameplay discontinuities remain visible.', '', '| Line | Time | Profile | Jump | B state | W |', '|---:|---:|---|---:|---|---|']
    for x in r['aim_jumps']['steady_state_top']:
        out.append(f"| {x['line']} | {fmt(x['time_s'])} | `{x['profile']}` | {x['jump_deg']:.2f}° | `{x['b']}` | {fmt_sig(x['w_prev'])}→{fmt_sig(x['w_now'])} |")
    if r.get('annotations'):
        a = r['annotations']
        out += ['', '## Subjective annotation sidecar', '', f"Run notes: {a.get('run_notes') or '-'}", '', f"Profile labels: `{json.dumps(a.get('profile_labels', {}), sort_keys=True)}`", '', '| Label | Time | Frames | Profiles | W effective | Horizontal reach | Aim jumps |', '|---|---|---:|---|---|---|---|']
        for x in a['intervals']:
            out.append(f"| `{x.get('label')}` | {x['start_s']:.3f}–{x['end_s']:.3f} | {x['frames']} | `{json.dumps(x['profiles'], sort_keys=True)}` | {fmt_stats(x['w_effective'])} | {fmt_stats(x['horizontal_reach_m'])} | {fmt_stats(x['aim_jump_deg'])} |")
    out += ['', '## Interpretation guardrails', '', '- Control-role labels come from the file registry where available; schema-1 roles are inferred by stable names, never numeric position.', '- VS OFF / Fixed Head / Fixed Shoulder are controls. The analyser does not call resemblance to any control intrinsically desirable.', '- Missing/inapplicable trace values are N/A, not numeric zero.', '- A grip-held/latch mismatch does not prove a rendered IK hand detached; semantic interaction/IK events are needed for that.', '- Horizontal O/P/Q results on old recordings are offline simulations, not production outcomes.', '- One truncated final JSON line is recoverable and marked unclean; malformed middle JSON is treated as corruption.', '']
    return '\n'.join(out)

def combined_analysis(items: list[tuple[ParsedRecording, dict[str, Any]]]) -> dict[str, Any]:
    groups = collections.defaultdict(list)
    for rec, res in items:
        groups[rec.schema_version, rec.build_commit].append((rec, res))
    runs = [{'file': rec.path.name, 'schema': rec.schema_version, 'build': rec.build_commit, 'sha256': rec.source_sha256} for rec, _ in items]
    profiles = collections.defaultdict(list)
    for rec, res in items:
        for p in res['profile_summaries']:
            profiles[p['profile']].append({'file': rec.path.name, 'schema': rec.schema_version, 'build': rec.build_commit, **p})
    return {'analysis_metadata': {'analyser_version': ANALYSER_VERSION, 'analyser_sha256': analyser_sha256()}, 'runs': runs, 'compatibility_groups': [{'schema': k[0], 'build': k[1], 'files': [x[0].path.name for x in v]} for k, v in sorted(groups.items())], 'compatible_for_global_pooling': len(groups) == 1, 'profiles': dict(sorted(profiles.items()))}

def combined_markdown(c: dict[str, Any]) -> str:
    out = ['# MCCVR Telemetry Combined Comparison v3', '', f"Analyser `{c['analysis_metadata']['analyser_version']}` / `{c['analysis_metadata']['analyser_sha256']}`", '', f"Global pooling compatible: **{c['compatible_for_global_pooling']}**", '', '## Compatibility groups', '', '| Schema | Build | Files |', '|---:|---|---|']
    for g in c['compatibility_groups']:
        out.append(f"| {g['schema']} | `{g['build']}` | {', '.join(g['files'])} |")
    if not c['compatible_for_global_pooling']:
        out += ['', '> **WARNING:** schema/build differs. Values are shown per run; the analyser does not silently pool them.', '']
    out += ['', '## Same-profile comparison', '', '| Profile | Run | Frames | Two-hand | B accepted | W natural | Horizontal reach | Offhand span |', '|---|---|---:|---:|---:|---|---|---|']
    for profile, rows in c['profiles'].items():
        for r in rows:
            out.append(f"| `{profile}` | `{r['file']}` | {r['frames']} | {fmt_pct(r['two_hand_active_pct'])} | {fmt_pct(r['b_accepted_pct'])} | {fmt_stats(r['w_natural'])} | {fmt_stats(r['horizontal_reach_m'])} | {fmt_stats(r['offhand_i0_i1_span_deg'])} |")
    return '\n'.join(out) + '\n'

def _fixture_session(schema: int, mapping: list[dict[str, Any]]) -> dict[str, Any]:
    return {'type': 'session_start', 'schema_version': schema, 'build_commit': 'fixture', 'qpc_frequency': 1000000, 'test_profile_enum_mapping': mapping}

def _fixture_frame(serial: int, pid: int, pname: str, left: bool=False) -> dict[str, Any]:
    return {'type': 'frame', 'schema_version': 1, 'prepared_serial': serial, 'capture_begin_qpc': serial * 1000, 'capture_end_qpc': serial * 1000 + 8, 'test_profile_id': pid, 'test_profile_name': pname, 'menu_open': False, 'contact_space_epoch': 1, 'contact_space_change_at_ns': 0, 'semantic_primary_aim_valid': True, 'semantic_support_aim_valid': True, 'semantic_support_endpoint_valid': True, 'head_sample_valid': True, 'upcoming_views_valid': True, 'semantic_primary_aim': {'position': [0.1, 0, 0.2], 'orientation': [0, 0, 0, 1]}, 'semantic_support_aim': {'position': [0.2, 0, -0.2], 'orientation': [0, 0, 0, 1]}, 'semantic_support_endpoint': [0.2, 0, -0.2], 'semantic_hmd': {'position': [0, 1.6, 0], 'orientation': [0, 0, 0, 1]}, 'pad': {'gripL': 0.8, 'gripR': 0.2}, 'effective_settings': {'left_handed': left, 'two_hand_latched': True, 'two_hand_enabled': True, 'virtual_stock_enabled': True, 'rear_reference': 3}, 'aim_trace': {'path': 4, 'a_valid': True, 'a_direction': [0, 0, -1], 'b_attempted': True, 'b_accepted': True, 'b_extreme_rejected': False, 'b_direction': [0.1, 0, -0.995], 'c_valid': True, 'c_direction': [0, 0, -1], 'w_effective_valid': True, 'w_effective': 0.5, 'w_natural_valid': True, 'w_natural': 0.5, 'seat_valid': True, 'seat_error_m': 0.1}, 'canonical_aim': {'valid': True, 'forward': [0, 0, -1], 'two_hand_active': True}, 'cf_fixed_head': {'fixed_target_valid': True, 'fixed_proximity_enabled': True, 'fixed_proximity_calculated': True, 'fixed_proximity_influence': 0.5, 'aim': {'forward': [0, 0, -1]}}, 'cf_fixed_shoulder': {'aim': {'forward': [0, 0, -1]}}, 'cf_vs_off': {'aim': {'forward': [0, 0, -1]}}}

def run_self_test() -> int:
    passed = []
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        p1 = root / 's1.jsonl'
        rows = [_fixture_session(1, [{'id': 0, 'name': 'Custom'}, {'id': 1, 'name': 'A_VsOffControl'}, {'id': 2, 'name': 'B_FixedHeadControl'}, {'id': 3, 'name': 'C_FixedShoulderControl'}]), _fixture_frame(1, 1, 'A_VsOffControl'), {'type': 'session_end', 'clean_stop': True, 'producer_calls': 1, 'duplicate_serial_suppressed': 0, 'enqueued': 1, 'dropped_queue_full': 0, 'written': 1}]
        p1.write_text('\n'.join((json.dumps(x) for x in rows)) + '\n')
        r1 = parse_jsonl(p1)
        assert r1.registry.role_for(1) == 'vs_off_control'
        passed.append('schema1 stable-name roles')
        assert semantic_support_grip(r1.frames[0], 1) == 0.8
        passed.append('support grip routing uses pad.gripL')
        grip_valid = _fixture_frame(2, 1, 'A_VsOffControl')
        grip_valid['support_grip_valid'] = True
        grip_valid['support_grip_position'] = [0.11, -0.22, 0.33]
        assert valid_support_grip_position(grip_valid) == [0.11, -0.22, 0.33]
        grip_invalid = _fixture_frame(3, 1, 'A_VsOffControl')
        grip_invalid['support_grip_valid'] = False
        grip_invalid['support_grip_position'] = [9.0, 8.0, 7.0]
        assert valid_support_grip_position(grip_invalid) is None
        passed.append('support grip position requires support_grip_valid')
        p2 = root / 's2.jsonl'
        mapping = [{'id': 77, 'name': 'ShoulderWhatever', 'role': 'fixed_shoulder_control'}, {'id': 12, 'name': 'OffWhatever', 'role': 'vs_off_control'}, {'id': 44, 'name': 'HeadWhatever', 'role': 'fixed_head_control'}]
        fr = _fixture_frame(1, 44, 'HeadWhatever', left=True)
        fr['schema_version'] = 2
        p2.write_text(json.dumps(_fixture_session(2, mapping)) + '\n' + json.dumps(fr) + '\n')
        r2 = parse_jsonl(p2)
        assert r2.registry.role_for(44) == 'fixed_head_control'
        assert semantic_support_grip(r2.frames[0], 2) == 0.8
        passed.append('schema2 role mapping independent of IDs/names')
        p3 = root / 'partial.jsonl'
        p3.write_text(json.dumps(_fixture_session(1, [])) + '\n' + json.dumps(_fixture_frame(1, 0, 'Custom')) + '\n' + '{"type":"frame"')
        r3 = parse_jsonl(p3)
        assert r3.trailing_partial and len(r3.frames) == 1
        passed.append('trailing partial recovered')
        p4 = root / 'corrupt.jsonl'
        p4.write_text(json.dumps(_fixture_session(1, [])) + '\n' + '{bad}\n' + json.dumps(_fixture_frame(1, 0, 'Custom')) + '\n')
        try:
            parse_jsonl(p4)
            raise AssertionError('middle corruption accepted')
        except ValueError:
            passed.append('middle corruption rejected')
        f = _fixture_frame(1, 0, 'Custom')
        f['aim_trace'].pop('w_effective')
        f['aim_trace']['w_effective_valid'] = False
        assert offhand_span_deg(f) is not None
        assert valid_fixed_effective_strength({'path': 3, 'fixed_target_valid': False, 'fixed_effective_strength': 0}) is None
        passed.append('missing/applicability semantics')
        f = _fixture_frame(1, 0, 'Custom')
        f['effective_settings']['horizontal_release_enabled'] = True
        f['effective_settings']['horizontal_release_full_m'] = 0.3
        f['effective_settings']['horizontal_release_release_m'] = 0.45
        f['aim_trace']['horizontal_reach_m'] = 0.4
        f['aim_trace']['horizontal_release_weight'] = 0.26
        hh = actual_horizontal_fields(f)
        assert hh['enabled'] is True and abs(hh['reach_m'] - 0.4) < 1e-09
        passed.append('schema2 horizontal fields')
    print(f'MCCVR analyser v3 self-test: {len(passed)} checks passed')
    for x in passed:
        print('  PASS', x)
    return 0

def resolve_annotation_arg(arg: Path | None, recording: Path, multiple: bool) -> Path | None:
    if arg is None:
        return None
    if multiple and arg.is_dir():
        for name in (recording.stem + '.annotations.json', recording.stem + '.json'):
            p = arg / name
            if p.exists():
                return p
        return None
    return arg

def write_json(path: Path, obj: Any) -> None:
    path.write_text(json_text(obj), encoding='utf-8')

def cross_mag(a: Any, b: Any) -> float | None:
    av = vec3(a)
    bv = vec3(b)
    if av is None or bv is None:
        return None
    an = math.sqrt(sum((x * x for x in av)))
    bn = math.sqrt(sum((x * x for x in bv)))
    if an <= 1e-12 or bn <= 1e-12:
        return None
    ax, ay, az = (x / an for x in av)
    bx, by, bz = (x / bn for x in bv)
    cx = ay * bz - az * by
    cy = az * bx - ax * bz
    cz = ax * by - ay * bx
    return math.sqrt(cx * cx + cy * cy + cz * cz)

def qrotate_up(q: Any) -> tuple[float, float, float] | None:
    qq = quat(q)
    if qq is None:
        return None
    x, y, z, w = qq
    vx, vy, vz = (0.0, 1.0, 0.0)
    tx = 2 * (y * vz - z * vy)
    ty = 2 * (z * vx - x * vz)
    tz = 2 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty), vy + w * ty + (z * tx - x * tz), vz + w * tz + (x * ty - y * tx))

def frame_times(rec: ParsedRecording) -> list[float | None]:
    fq = int(rec.session_start.get('qpc_frequency', 0) or 0)
    first = int(rec.frames[0].get('capture_begin_qpc', 0)) if rec.frames else 0
    return [frame_time_s(f, first, fq) for f in rec.frames]

def profile_name(f, rec):
    return frame_profile(f, rec.registry).name

def agreement_value(tr: dict[str, Any]) -> float | None:
    if tr.get('b_accepted') and finite(tr.get('a_dot_b')):
        return float(tr['a_dot_b'])
    if tr.get('b_extreme_rejected') and finite(tr.get('b_rejected_agreement')):
        return float(tr['b_rejected_agreement'])
    return None

def w_region(tr: dict[str, Any]) -> str:
    if not tr.get('w_effective_valid') or not finite(tr.get('w_effective')):
        return 'N/A'
    w = float(tr['w_effective'])
    if w <= 0.01:
        return 'released'
    if w >= 0.99:
        return 'full'
    return 'transition'

def b_boundary_analysis(rec: ParsedRecording, times: list[float | None], args) -> dict[str, Any]:
    events = []
    for i in range(1, len(rec.frames)):
        p, c = (rec.frames[i - 1], rec.frames[i])
        pt = p.get('aim_trace', {})
        ct = c.get('aim_trace', {})
        if bool(pt.get('b_accepted')) == bool(ct.get('b_accepted')):
            continue
        if not (pt.get('b_attempted') or ct.get('b_attempted')):
            continue
        jump = aim_jump(p, c)
        wp = fnum(pt.get('w_effective')) if pt.get('w_effective_valid') else None
        wc = fnum(ct.get('w_effective')) if ct.get('w_effective_valid') else None
        stock_masked = wp is not None and wc is not None and (min(wp, wc) >= 0.9)
        events.append({'line': c['_line'], 'serial': c.get('prepared_serial'), 'time_s': times[i], 'profile': profile_name(c, rec), 'from': b_state(pt), 'to': b_state(ct), 'prev_agreement': agreement_value(pt), 'agreement': agreement_value(ct), 'jump_deg': jump, 'w_prev': wp, 'w_now': wc, 'stock_masked_high_w': stock_masked, 'visible_gt_5deg': jump is not None and jump > 5, 'visible_gt_20deg': jump is not None and jump > 20})
    clusters = []
    cur = []
    for e in events:
        if not cur or (e['profile'] == cur[-1]['profile'] and e['time_s'] is not None and (cur[-1]['time_s'] is not None) and (e['time_s'] - cur[-1]['time_s'] <= args.chatter_gap_s)):
            cur.append(e)
        else:
            if len(cur) >= 2:
                clusters.append(cur)
            cur = [e]
    if len(cur) >= 2:
        clusters.append(cur)
    out = []
    for xs in clusters:
        js = [x['jump_deg'] for x in xs if x['jump_deg'] is not None]
        out.append({'profile': xs[0]['profile'], 'start_line': xs[0]['line'], 'end_line': xs[-1]['line'], 'start_time_s': xs[0]['time_s'], 'end_time_s': xs[-1]['time_s'], 'toggles': len(xs), 'jump_deg': stats(js), 'masked_high_w_toggles': sum((x['stock_masked_high_w'] for x in xs)), 'visible_gt_20deg': sum((x['visible_gt_20deg'] for x in xs))})
    return {'events': events, 'chatter_episodes': out, 'semantics': 'B boundary events are mechanism events. high-W/C dominance may mask their visible aim consequence; low-W/Force-Hip/VS-OFF can expose large snaps.'}

def w_transition_analysis(rec, times) -> list[dict[str, Any]]:
    out = []
    for i in range(1, len(rec.frames)):
        p, c = (rec.frames[i - 1], rec.frames[i])
        pt = p.get('aim_trace', {})
        ct = c.get('aim_trace', {})
        a = w_region(pt)
        b = w_region(ct)
        if a == 'N/A' or b == 'N/A' or a == b:
            continue
        out.append({'line': c['_line'], 'serial': c.get('prepared_serial'), 'time_s': times[i], 'profile': profile_name(c, rec), 'from': a, 'to': b, 'w_prev': float(pt['w_effective']), 'w_now': float(ct['w_effective']), 'jump_deg': aim_jump(p, c)})
    return out

def fixed_zero_crossings(rec, times) -> list[dict[str, Any]]:
    out = []
    for i in range(1, len(rec.frames)):
        p, c = (rec.frames[i - 1], rec.frames[i])
        a = valid_fixed_effective_strength(p.get('aim_trace', {}))
        b = valid_fixed_effective_strength(c.get('aim_trace', {}))
        if a is None or b is None:
            continue
        if a <= EPS < b or b <= EPS < a:
            out.append({'line': c['_line'], 'time_s': times[i], 'profile': profile_name(c, rec), 'from': a, 'to': b, 'jump_deg': aim_jump(p, c), 'from_exact_a': p.get('aim_trace', {}).get('exact_a_endpoint_selected'), 'to_exact_a': c.get('aim_trace', {}).get('exact_a_endpoint_selected')})
    return out

class MotionStats:

    def __init__(self):
        self.values = []
        self.examples = []

    def add(self, gain, row):
        if gain is not None and finite(gain):
            self.values.append(float(gain))
            if len(self.examples) < 20:
                self.examples.append(row)

    def result(self):
        return {'gain': stats(self.values), 'samples': len(self.values), 'examples': self.examples[:10]}

def movement_response(rec, times, args) -> dict[str, Any]:
    by = collections.defaultdict(lambda: {'rear_translation': MotionStats(), 'support_translation': MotionStats(), 'support_rotation_aim_endpoint': MotionStats(), 'support_rotation_grip_endpoint': MotionStats(), 'primary_rotation': MotionStats()})
    for i in range(1, len(rec.frames)):
        p, c = (rec.frames[i - 1], rec.frames[i])
        if transition_flags(p, c):
            continue
        pd = vdist(primary_pos(p), primary_pos(c))
        sd = vdist(support_pos(p), support_pos(c))
        pr = qangle(primary_quat(p), primary_quat(c))
        sr = qangle(support_quat(p), support_quat(c))
        hr = qangle(head_quat(p), head_quat(c))
        j = aim_jump(p, c)
        if None in (pd, sd, pr, sr, hr, j):
            continue
        pmm = pd * 1000
        smm = sd * 1000
        name = profile_name(c, rec)
        row = {'line': c['_line'], 'time_s': times[i], 'jump_deg': j, 'primary_mm': pmm, 'support_mm': smm, 'primary_rot_deg': pr, 'support_rot_deg': sr, 'head_rot_deg': hr}
        if pmm >= args.motion_min_mm and smm <= args.motion_stationary_mm and (pr <= args.motion_stationary_rot_deg) and (sr <= args.motion_stationary_rot_deg) and (hr <= args.motion_stationary_rot_deg):
            by[name]['rear_translation'].add(j / pmm, row)
        if smm >= args.motion_min_mm and pmm <= args.motion_stationary_mm and (pr <= args.motion_stationary_rot_deg) and (sr <= args.motion_stationary_rot_deg) and (hr <= args.motion_stationary_rot_deg):
            by[name]['support_translation'].add(j / smm, row)
        if sr >= args.rotation_min_deg and pmm <= args.motion_stationary_mm and (smm <= args.motion_stationary_mm) and (pr <= args.motion_stationary_rot_deg) and (hr <= args.motion_stationary_rot_deg):
            key = 'support_rotation_grip_endpoint' if bool(c.get('support_endpoint_used_grip')) else 'support_rotation_aim_endpoint'
            by[name][key].add(j / sr, row)
        if pr >= args.rotation_min_deg and pmm <= args.motion_stationary_mm and (smm <= args.motion_stationary_mm) and (sr <= args.motion_stationary_rot_deg) and (hr <= args.motion_stationary_rot_deg):
            by[name]['primary_rotation'].add(j / pr, row)
    return {p: {k: v.result() for k, v in vals.items()} for p, vals in sorted(by.items())}

def geometry_indexes(rec, times, args) -> dict[str, Any]:
    per = collections.defaultdict(lambda: {'u': collections.Counter(), 'cross': [], 'singular_examples': []})
    for i, f in enumerate(rec.frames):
        tr = f.get('aim_trace', {})
        p = profile_name(f, rec)
        if tr.get('seat_valid') and finite(tr.get('seat_raw_projection')):
            u = float(tr['seat_raw_projection'])
            if u < 0:
                per[p]['u']['lt_0'] += 1
            elif u > 1:
                per[p]['u']['gt_1'] += 1
            else:
                per[p]['u']['in_segment'] += 1
            if abs(u) < 0.02 or abs(u - 1) < 0.02:
                per[p]['u']['near_endpoint_0_02'] += 1
        if tr.get('final_direction_valid'):
            up = qrotate_up(tr.get('primary_quaternion'))
            cm = cross_mag(tr.get('final_direction'), up)
            if cm is not None:
                per[p]['cross'].append(cm)
                if cm < args.singularity_cross_warn and len(per[p]['singular_examples']) < 20:
                    per[p]['singular_examples'].append({'line': f['_line'], 'time_s': times[i], 'cross_magnitude': cm, 'path': path_name(tr)})
    return {p: {'seat_projection_regions': dict(x['u']), 'orientation_cross_magnitude': stats(x['cross']), 'near_singularity_count': sum((v < args.singularity_cross_warn for v in x['cross'])), 'examples': x['singular_examples']} for p, x in sorted(per.items())}

def control_oracles(rec) -> dict[str, Any]:
    by = collections.defaultdict(list)
    examples = []
    role_to_cf = {'vs_off_control': 'cf_vs_off', 'fixed_head_control': 'cf_fixed_head', 'fixed_shoulder_control': 'cf_fixed_shoulder'}
    for f in rec.frames:
        p = frame_profile(f, rec.registry)
        cfkey = role_to_cf.get(p.role)
        if not cfkey:
            continue
        a = f.get('canonical_aim', {}).get('forward')
        b = f.get(cfkey, {}).get('aim', {}).get('forward')
        err = vangle(a, b)
        if err is not None:
            by[p.name].append(err)
            if err > 0.001 and len(examples) < 30:
                examples.append({'line': f['_line'], 'profile': p.name, 'error_deg': err})
    return {'per_profile': {p: stats(v) for p, v in sorted(by.items())}, 'mismatch_examples': examples}

def state_pose_mismatches(rec, times, args) -> dict[str, Any]:
    gl = grip_latch_analysis(rec, args, times)
    episodes = []
    line_map = {f['_line']: f for f in rec.frames}
    for x in gl['grip_and_latch_held_but_two_hand_aim_inactive_runs']:
        f = line_map.get(x['start_line'])
        tr = f.get('aim_trace', {}) if f else {}
        episodes.append({**x, 'start_b_state': b_state(tr), 'start_exact_a': tr.get('exact_a_endpoint_selected') if f else None, 'start_w_effective': fnum(tr.get('w_effective')) if f and tr.get('w_effective_valid') else None})
    return {'grip_latch': gl, 'aim_inactive_while_grip_latch_held': episodes}

def horizontal_extension_episodes(rec, times, args) -> list[dict[str, Any]]:
    fs = rec.frames
    vals = []
    for f in fs:
        vals.append(horizontal_distance(primary_pos(f), head_pos(f)) if f.get('semantic_primary_aim_valid') and f.get('head_sample_valid') else None)
    episodes = []
    cur = []
    sign = 0

    def finish(idx):
        if len(idx) < args.reach_episode_min_frames:
            return
        a, b = (idx[0], idx[-1])
        if vals[a] is None or vals[b] is None:
            return
        delta = vals[b] - vals[a]
        if abs(delta) < args.reach_episode_min_delta_m:
            return
        frames = [fs[i] for i in idx]
        traces = [f.get('aim_trace', {}) for f in frames]
        ys = []
        for f in frames:
            pp = vec3(primary_pos(f))
            hp = vec3(head_pos(f))
            if pp and hp:
                ys.append(pp[1] - hp[1])
        wn = [float(t['w_natural']) for t in traces if t.get('w_natural_valid') and finite(t.get('w_natural'))]
        we = [float(t['w_effective']) for t in traces if t.get('w_effective_valid') and finite(t.get('w_effective'))]
        fh = [valid_fixed_proximity(f.get('cf_fixed_head', {})) for f in frames]
        fh = [x for x in fh if x is not None]
        candidates = parse_horizontal_candidates(args.horizontal_candidates)
        hs = {k: {'start': proximity_influence(vals[a], *v), 'end': proximity_influence(vals[b], *v), 'min': min((proximity_influence(vals[i], *v) for i in idx if vals[i] is not None)), 'max': max((proximity_influence(vals[i], *v) for i in idx if vals[i] is not None))} for k, v in candidates.items()}
        episodes.append({'direction': 'outward' if delta > 0 else 'inward', 'profile': profile_name(frames[0], rec), 'start_line': frames[0]['_line'], 'end_line': frames[-1]['_line'], 'start_time_s': times[a], 'end_time_s': times[b], 'frames': len(frames), 'reach_start_m': vals[a], 'reach_end_m': vals[b], 'reach_delta_m': delta, 'vertical_primary_minus_head_start_m': ys[0] if ys else None, 'vertical_primary_minus_head_end_m': ys[-1] if ys else None, 'w_natural': stats(wn), 'w_effective': stats(we), 'fixed_head_proximity': stats(fh), 'offline_h': hs, 'aim_net_jump_deg': vangle(frames[0].get('canonical_aim', {}).get('forward'), frames[-1].get('canonical_aim', {}).get('forward'))})
    for i in range(1, len(fs)):
        if vals[i - 1] is None or vals[i] is None or transition_flags(fs[i - 1], fs[i]):
            if cur:
                finish(cur)
            cur = []
            sign = 0
            continue
        d = vals[i] - vals[i - 1]
        ns = 1 if d >= args.reach_episode_step_min_m else -1 if d <= -args.reach_episode_step_min_m else 0
        if ns == 0:
            if cur:
                cur.append(i)
            continue
        if not cur:
            cur = [i - 1, i]
            sign = ns
        elif ns == sign:
            cur.append(i)
        else:
            finish(cur)
            cur = [i - 1, i]
            sign = ns
    if cur:
        finish(cur)
    episodes.sort(key=lambda x: abs(x['reach_delta_m']), reverse=True)
    return episodes

def low_held_retention(rec, args) -> dict[str, Any]:
    candidates = parse_horizontal_candidates(args.horizontal_candidates)
    by = collections.defaultdict(lambda: {k: [] for k in candidates})
    count = collections.Counter()
    for f in rec.frames:
        pp = vec3(primary_pos(f))
        hp = vec3(head_pos(f))
        reach = horizontal_distance(primary_pos(f), head_pos(f))
        if pp is None or hp is None or reach is None:
            continue
        if pp[1] - hp[1] <= -args.low_held_below_head_m:
            p = profile_name(f, rec)
            count[p] += 1
            for k, v in candidates.items():
                h = proximity_influence(reach, *v)
                if h is not None:
                    by[p][k].append(h)
    return {p: {'low_held_frames': count[p], 'candidates': {k: {'h': stats(v), 'retain_ge_0_9_pct': pct(sum((x >= 0.9 for x in v)), len(v)), 'release_le_0_1_pct': pct(sum((x <= 0.1 for x in v)), len(v))} for k, v in d.items()}} for p, d in sorted(by.items())}

def collapse_setting_sweeps(changes: list[dict[str, Any]], gap_s: float) -> list[dict[str, Any]]:
    groups = []
    cur = []
    for x in changes:
        if x['source'] != 'effective_frame_diff':
            if cur:
                groups.append(cur)
                cur = []
            groups.append([x])
            continue
        if cur:
            p = cur[-1]
            close = x.get('time_s') is not None and p.get('time_s') is not None and (x['time_s'] - p['time_s'] <= gap_s)
            if x.get('field') == p.get('field') and x.get('profile') == p.get('profile') and close:
                cur.append(x)
                continue
            groups.append(cur)
        cur = [x]
    if cur:
        groups.append(cur)
    out = []
    for g in groups:
        a, b = (g[0], g[-1])
        out.append({'kind': 'setting_sweep' if len(g) > 1 else 'setting_change', 'field': a.get('field'), 'profile': a.get('profile'), 'source': a.get('source'), 'start_line': a.get('line'), 'end_line': b.get('line'), 'start_time_s': a.get('time_s'), 'end_time_s': b.get('time_s'), 'from': a.get('from'), 'to': b.get('to'), 'steps': len(g), 'menu_context': any((bool(x.get('menu_context')) for x in g))})
    return out

def other_record_inventory(rec) -> list[dict[str, Any]]:
    c = collections.Counter()
    for x in rec.other_records:
        key = (str(x.get('type', 'unknown')), str(x.get('provider', x.get('channel', '-'))), str(x.get('event', x.get('name', '-'))))
        c[key] += 1
    return [{'type': k[0], 'provider': k[1], 'event': k[2], 'count': v} for k, v in sorted(c.items())]

def analyse_v4(rec, args, annotations=None) -> dict[str, Any]:
    base = analyse_recording(rec, args, annotations)
    times = frame_times(rec)
    base['analysis_metadata']['analyser_version'] = ANALYSER_VERSION
    base['analysis_metadata']['analyser_sha256'] = sha256_file(Path(__file__))
    base['v4'] = {'b_boundary': b_boundary_analysis(rec, times, args), 'w_transitions': w_transition_analysis(rec, times), 'fixed_zero_crossings': fixed_zero_crossings(rec, times), 'movement_response': movement_response(rec, times, args), 'geometry_indexes': geometry_indexes(rec, times, args), 'control_oracles': control_oracles(rec), 'state_pose_mismatches': state_pose_mismatches(rec, times, args), 'horizontal_reach_episodes': horizontal_extension_episodes(rec, times, args), 'low_held_retention': low_held_retention(rec, args), 'setting_sweeps': collapse_setting_sweeps(base['setting_changes'], args.setting_sweep_gap_s), 'other_record_inventory': other_record_inventory(rec), 'semantics': {'low_held': 'Low-held is defined only by the configured vertical-offset threshold relative to the HMD; the analyser reports the geometry and leaves posture classification to the investigator.', 'horizontal_release': 'Horizontal XZ reach is a descriptive geometric quantity computed from recorded positions; the recorded horizontal-release fields and the offline candidates are reported without selecting a release criterion.', 'b_boundary': 'A B acceptance flip is recorded even if C/high W masks visible aim movement. Mechanism and visible consequence are separate.', 'movement_response': 'Adjacent-frame isolation heuristics index sensitivity; they are not controlled causal experiments.', 'future_events': 'Unknown semantic event/provider records are inventoried generically so future general-recorder channels do not require core parser edits just to be visible.'}}
    return base

def md_v4(r, args) -> str:
    out = [markdown_report(r), '', '---', '', '# V4 concern-mining additions', '']
    v = r['v4']
    out += ['## B acceptance/rejection boundary events', '', v['b_boundary']['semantics'], '', f"Transitions: **{len(v['b_boundary']['events'])}**; chatter episodes: **{len(v['b_boundary']['chatter_episodes'])}**.", '', '| Profile | Lines | Toggles | Jump distribution | High-W masked toggles | >20° visible |', '|---|---:|---:|---|---:|---:|']
    for x in v['b_boundary']['chatter_episodes'][:40]:
        out.append(f"| `{x['profile']}` | {x['start_line']}–{x['end_line']} | {x['toggles']} | {fmt_stats(x['jump_deg'], 2)} | {x['masked_high_w_toggles']} | {x['visible_gt_20deg']} |")
    out += ['', '## W-region transitions', '', f"Released↔transition↔full crossings: **{len(v['w_transitions'])}**.", '']
    if v['w_transitions']:
        out += ['| Line | Time | Profile | Region | W | Aim jump |', '|---:|---:|---|---|---|---:|']
        for x in sorted(v['w_transitions'], key=lambda z: -(z['jump_deg'] or 0))[:30]:
            out.append(f"| {x['line']} | {fmt(x['time_s'])} | `{x['profile']}` | {x['from']}→{x['to']} | {fmt_sig(x['w_prev'])}→{fmt_sig(x['w_now'])} | {fmt(x['jump_deg'], 2)}° |")
    out += ['', '## Fixed-stock valid effective-strength zero crossings', '', 'Only frames where fixed-stock trace applicability is proven are considered; missing/inapplicable strength is never coerced to zero.', '']
    if not v['fixed_zero_crossings']:
        out.append('None.')
    else:
        out += ['| Line | Time | Profile | Strength | Exact-A | Aim jump |', '|---:|---:|---|---|---|---:|']
        for x in v['fixed_zero_crossings'][:30]:
            out.append(f"| {x['line']} | {fmt(x['time_s'])} | `{x['profile']}` | {fmt_sig(x['from'])}→{fmt_sig(x['to'])} | {x['from_exact_a']}→{x['to_exact_a']} | {fmt(x['jump_deg'], 2)}° |")
    out += ['', '## Movement-response indexes', '', 'Deterministic adjacent-frame filters look for approximately isolated rear translation, support translation, support rotation, or primary rotation. These index high-gain regions; they are not substitutes for a controlled gesture.', '', '| Profile | Rear translation deg/mm | Support translation deg/mm | Support rot (aim endpoint) deg/deg | Support rot (grip endpoint) deg/deg | Primary rot deg/deg |', '|---|---|---|---|---|---|']
    for p, d in v['movement_response'].items():
        out.append(f"| `{p}` | {fmt_stats(d['rear_translation']['gain'], 3)} n={d['rear_translation']['samples']} | {fmt_stats(d['support_translation']['gain'], 3)} n={d['support_translation']['samples']} | {fmt_stats(d['support_rotation_aim_endpoint']['gain'], 3)} n={d['support_rotation_aim_endpoint']['samples']} | {fmt_stats(d['support_rotation_grip_endpoint']['gain'], 3)} n={d['support_rotation_grip_endpoint']['samples']} | {fmt_stats(d['primary_rotation']['gain'], 3)} n={d['primary_rotation']['samples']} |")
    out += ['', '## Geometry / orientation-builder indexes', '', '`uRaw` outside [0,1] is descriptive endpoint-clamped finite-segment usage, not a bug. Small `|cross(final,primaryUp)|` approaches the existing orientation-builder singular domain.', '', '| Profile | uRaw<0 | in segment | uRaw>1 | near endpoint | cross magnitude | near-singular |', '|---|---:|---:|---:|---:|---|---:|']
    for p, d in v['geometry_indexes'].items():
        q = d['seat_projection_regions']
        out.append(f"| `{p}` | {q.get('lt_0', 0)} | {q.get('in_segment', 0)} | {q.get('gt_1', 0)} | {q.get('near_endpoint_0_02', 0)} | {fmt_stats(d['orientation_cross_magnitude'], 4)} | {d['near_singularity_count']} |")
    out += ['', '## Same-frame control identity oracle', '', 'Uses semantic profile roles from the session registry, not profile numeric IDs.', '', '| Active control profile | Canonical↔counterfactual error |', '|---|---|']
    for p, s in v['control_oracles']['per_profile'].items():
        out.append(f'| `{p}` | {fmt_stats(s, 8)} |')
    out += ['', '## State vs pose-authority mismatch episodes', '', 'Grip+latch held while canonical `two_hand_active` is false is intentionally distinguished from physical latch release. It does not by itself prove a visible IK hand detached.', '', f"Episodes: **{len(v['state_pose_mismatches']['aim_inactive_while_grip_latch_held'])}**.", '']
    if v['state_pose_mismatches']['aim_inactive_while_grip_latch_held']:
        out += ['| Lines | Time | Frames | Profile | B start | W start |', '|---|---:|---:|---|---|---:|']
        for x in v['state_pose_mismatches']['aim_inactive_while_grip_latch_held'][:40]:
            out.append(f"| {x['start_line']}–{x['end_line']} | {fmt(x['start_time_s'])}–{fmt(x['end_time_s'])} | {x['frames']} | `{x['profile']}` | `{x['start_b_state']}` | {fmt(x['start_w_effective'])} |")
    out += ['', '## Horizontal reach episodes — outward/inward', '', 'These detect substantial monotonic XZ reach changes with transition-contaminated steps excluded. Low/hip vertical position is reported rather than used as a release criterion.', '', '| Dir | Profile | Lines | Reach | Δreach | Rear Y−Head start→end | W eff | Fixed Head prox | Aim net |', '|---|---|---:|---|---:|---|---|---|---:|']
    for x in v['horizontal_reach_episodes'][:40]:
        out.append(f"| {x['direction']} | `{x['profile']}` | {x['start_line']}–{x['end_line']} | {x['reach_start_m']:.3f}→{x['reach_end_m']:.3f} | {x['reach_delta_m']:+.3f} | {fmt(x['vertical_primary_minus_head_start_m'])}→{fmt(x['vertical_primary_minus_head_end_m'])} | {fmt_stats(x['w_effective'])} | {fmt_stats(x['fixed_head_proximity'])} | {fmt(x['aim_net_jump_deg'], 2)}° |")
    out += ['', '## Low-held geometry diagnostic', '', f'Low-held is defined here only for analysis as primary Y at least **{args.low_held_below_head_m:.3f} m below HMD**. This reports the geometric condition; the analyser does not classify it as a defect or a false engagement.', '', '| Profile | Low-held frames | Candidate | H | H≥0.9 retain | H≤0.1 release |', '|---|---:|---|---|---:|---:|']
    for p, d in v['low_held_retention'].items():
        for name, c in d['candidates'].items():
            out.append(f"| `{p}` | {d['low_held_frames']} | `{name}` | {fmt_stats(c['h'])} | {fmt_pct(c['retain_ge_0_9_pct'])} | {fmt_pct(c['release_le_0_1_pct'])} |")
    out += ['', '## Collapsed setting changes / sweeps', '', '| Kind | Field | Profile | Lines | Time | From→To | Steps | Menu |', '|---|---|---|---:|---:|---|---:|---|']
    for x in v['setting_sweeps']:
        out.append(f"| `{x['kind']}` | `{x['field']}` | `{x.get('profile') or '-'}` | {x['start_line']}–{x['end_line']} | {fmt(x['start_time_s'])}–{fmt(x['end_time_s'])} | `{x.get('from')}`→`{x.get('to')}` | {x['steps']} | {x['menu_context']} |")
    out += ['', '## General recorder event/provider inventory', '']
    if not v['other_record_inventory']:
        out.append('No non-frame/session semantic event records in this schema-1 recording.')
    else:
        out += ['| Type | Provider | Event | Count |', '|---|---|---|---:|']
        for x in v['other_record_inventory']:
            out.append(f"| `{x['type']}` | `{x['provider']}` | `{x['event']}` | {x['count']} |")
    out += ['', '## V4 guardrails', '', '- Low-held is reported against the configured vertical-offset threshold only; the analyser does not classify the posture as desirable or undesirable.', '- Horizontal reach is reported in XZ. O/P/Q are offline candidate curves, not recorded production behaviour; the vertical component is not used by those curves.', '- B boundary transitions are reported separately from visible aim jumps so the two observation types remain distinguishable.', '- VS OFF resemblance is not scored as desirable; it remains just another factual control comparison.', '- Support rotation response is split by aim-endpoint vs grip-endpoint source because wrist rotation can move an aim-pose endpoint even without direct quaternion authority.', '- Current schema still cannot explain semantic grab admission/rejection, rendered IK attachment, reload/holster/melee state, or other feature callbacks. V4 inventories those records automatically once the general recorder emits them.', '']
    return '\n'.join(out)

def combined_v4(items):
    c = combined_analysis([(rec, res) for rec, res in items])
    c['analysis_metadata']['analyser_version'] = ANALYSER_VERSION
    c['analysis_metadata']['analyser_sha256'] = sha256_file(Path(__file__))
    rows = collections.defaultdict(list)
    for rec, res in items:
        v = res['v4']
        ps = {p['profile']: p for p in res['profile_summaries']}
        for p, d in v['movement_response'].items():
            rows[p].append({'file': rec.path.name, 'build': rec.build_commit, 'schema': rec.schema_version, 'rear_gain': d['rear_translation']['gain'], 'support_gain': d['support_translation']['gain'], 'support_rot_aim': d['support_rotation_aim_endpoint']['gain'], 'support_rot_grip': d['support_rotation_grip_endpoint']['gain'], 'offhand_span': ps.get(p, {}).get('offhand_i0_i1_span_deg')})
    c['v4_profiles'] = dict(sorted(rows.items()))
    return c

def combined_md_v4(c):
    out = [combined_markdown(c), '', '## V4 same-profile concern metrics', '', '| Profile | Run | Rear gain deg/mm | Support gain deg/mm | Support rot aim | Support rot grip | Offhand I0↔I1 |', '|---|---|---|---|---|---|---|']
    for p, rows in c.get('v4_profiles', {}).items():
        for r in rows:
            out.append(f"| `{p}` | `{r['file']}` | {fmt_stats(r['rear_gain'], 3)} | {fmt_stats(r['support_gain'], 3)} | {fmt_stats(r['support_rot_aim'], 3)} | {fmt_stats(r['support_rot_grip'], 3)} | {fmt_stats(r['offhand_span'], 3)} |")
    return '\n'.join(out) + '\n'

def self_test_v4() -> int:
    rc = run_self_test()
    if rc:
        return rc
    assert abs(proximity_influence(0.27, 0.27, 0.425) - 1) < 1e-12
    assert abs(proximity_influence(0.425, 0.27, 0.425) - 0) < 1e-12
    assert qrotate_up([0, 0, 0, 1]) == (0.0, 1.0, 0.0)
    print('MCCVR analyser v4 self-test: concern-mining helper checks passed')
    return 0

def add_core_args(ap):
    ap.add_argument('recordings', nargs='*', type=Path)
    ap.add_argument('--report', type=Path)
    ap.add_argument('--json-out', type=Path)
    ap.add_argument('--combined-report', type=Path)
    ap.add_argument('--combined-json', type=Path)
    ap.add_argument('--annotations', type=Path)
    ap.add_argument('--grip-held-threshold', type=float, default=0.5)
    ap.add_argument('--top-jumps', type=int, default=25)
    ap.add_argument('--stationary-hand-mm', type=float, default=5.0)
    ap.add_argument('--stationary-hand-rotation-deg', type=float, default=1.0)
    ap.add_argument('--head-yaw-max-deg', type=float, default=10.0)
    ap.add_argument('--head-episode-step-min-deg', type=float, default=0.05)
    ap.add_argument('--head-episode-min-excursion-deg', type=float, default=2.0)
    ap.add_argument('--head-episode-min-frames', type=int, default=4)
    ap.add_argument('--horizontal-candidates', default='O_270_425:0.270:0.425,P_300_450:0.300:0.450,Q_320_460:0.320:0.460')
ANALYSER_VERSION = '6.3.0-standalone'

def authority_decomposition(rec) -> dict[str, Any]:
    """
    Neutral angular-distance decomposition with coordinate-domain hygiene.

    A/B/Hip/C are pre-calibration trace directions and are compared against
    aim_trace.final_direction, which is also pre-calibration.

    Same-frame control AimPoseResults are post-finishAimPose outputs and are
    compared against canonical_aim.forward, which is also post-calibration.

    This prevents configured gun yaw/pitch/roll from masquerading as solver
    authority distance.
    """
    solver_by = collections.defaultdict(lambda: collections.defaultdict(list))
    control_by = collections.defaultdict(lambda: collections.defaultdict(list))
    counts = collections.defaultdict(collections.Counter)
    for f in rec.frames:
        p = profile_name(f, rec)
        tr = f.get('aim_trace', {})
        solver_final = tr.get('final_direction') if tr.get('final_direction_valid') else None
        canonical = f.get('canonical_aim', {}).get('forward')
        solver_candidates = {'A': tr.get('a_direction') if tr.get('a_valid') else None, 'B': tr.get('b_direction') if tr.get('b_accepted') else None, 'Hip': tr.get('hip_aim_direction') if tr.get('hip_aim_valid') else None, 'C': tr.get('c_direction') if tr.get('c_valid') else None}
        controls = {'CF_VS_OFF': f.get('cf_vs_off', {}).get('aim', {}).get('forward'), 'CF_FIXED_HEAD': f.get('cf_fixed_head', {}).get('aim', {}).get('forward'), 'CF_FIXED_SHOULDER': f.get('cf_fixed_shoulder', {}).get('aim', {}).get('forward')}
        for name, vec in solver_candidates.items():
            a = vangle(solver_final, vec)
            if a is not None:
                solver_by[p][name].append(a)
                for t in (0.1, 1.0, 5.0):
                    if a <= t:
                        counts[p][f'solver_{name}_le_{t:g}deg'] += 1
        for name, vec in controls.items():
            a = vangle(canonical, vec)
            if a is not None:
                control_by[p][name].append(a)
                for t in (0.1, 1.0, 5.0):
                    if a <= t:
                        counts[p][f'control_{name}_le_{t:g}deg'] += 1
        counts[p]['frames'] += 1
    profiles = sorted(set(solver_by) | set(control_by) | set(counts))
    out = {}
    for p in profiles:
        out[p] = {'solver_precal_angles_deg': {k: stats(v) for k, v in sorted(solver_by[p].items())}, 'control_postcal_angles_deg': {k: stats(v) for k, v in sorted(control_by[p].items())}, 'close_counts': dict(counts[p]), 'semantics': {'solver_precal': 'aim_trace.final_direction versus A/B/Hip/C; all pre-calibration', 'control_postcal': 'canonical_aim.forward versus same-frame control forwards; all post-calibration', 'desirability': 'none inferred; angular distances are descriptive only'}}
    return out

def transition_timeline(rec, times) -> list[dict[str, Any]]:
    out = []
    fs = rec.frames
    for i in range(1, len(fs)):
        p, c = (fs[i - 1], fs[i])
        events = []
        if p.get('test_profile_id') != c.get('test_profile_id') or p.get('test_profile_name') != c.get('test_profile_name'):
            events.append('profile_change')
        pe = p.get('effective_settings', {})
        ce = c.get('effective_settings', {})
        if [x for x in nested_diff(pe, ce) if x[0] not in DYNAMIC_EFFECTIVE_FIELDS] and 'profile_change' not in events:
            events.append('setting_change')
        if bool(p.get('menu_open')) != bool(c.get('menu_open')):
            events.append('menu_open' if c.get('menu_open') else 'menu_close')
        if bool(p.get('focused')) != bool(c.get('focused')):
            events.append('focus_change')
        if bool(p.get('should_render')) != bool(c.get('should_render')):
            events.append('should_render_change')
        if p.get('openxr_session_state') != c.get('openxr_session_state'):
            events.append('openxr_session_state_change')
        for label, key in [('primary', 'semantic_primary_aim_valid'), ('support', 'semantic_support_aim_valid'), ('support_endpoint', 'semantic_support_endpoint_valid'), ('head', 'head_sample_valid'), ('views', 'upcoming_views_valid')]:
            if bool(p.get(key)) != bool(c.get(key)):
                events.append(f"{label}_{('valid' if c.get(key) else 'invalid')}")
        if bool(p.get('support_endpoint_used_grip')) != bool(c.get('support_endpoint_used_grip')):
            events.append('support_endpoint_source_change')
        pt = p.get('aim_trace', {})
        ct = c.get('aim_trace', {})
        if bool(pt.get('c_valid')) != bool(ct.get('c_valid')):
            events.append('c_valid_change')
        if bool(pt.get('shoulder_to_head_fallback')) != bool(ct.get('shoulder_to_head_fallback')):
            events.append('shoulder_head_fallback_change')
        if bool(pt.get('exact_a_endpoint_selected')) != bool(ct.get('exact_a_endpoint_selected')):
            events.append('exact_a_change')
        if w_region(pt) != w_region(ct) and 'N/A' not in (w_region(pt), w_region(ct)):
            events.append('w_region_change')
        if b_state(pt) != b_state(ct):
            events.append('b_state_change')
        pc = p.get('canonical_aim', {})
        cc = c.get('canonical_aim', {})
        if bool(pc.get('two_hand_active')) != bool(cc.get('two_hand_active')):
            events.append('two_hand_active_change')
        if ct.get('orientation_rebuild_attempted') and (not ct.get('orientation_rebuild_succeeded')):
            events.append('orientation_rebuild_failed')
        if p.get('contact_space_epoch') != c.get('contact_space_epoch'):
            events.append('contact_space_epoch_change')
        if events:
            out.append({'line': c['_line'], 'serial': c.get('prepared_serial'), 'time_s': times[i], 'profile': profile_name(c, rec), 'events': events, 'aim_jump_deg': aim_jump(p, c)})
    return out

def performance_index(rec, times, args) -> dict[str, Any]:
    fq = int(rec.session_start.get('qpc_frequency', 0) or 0)
    hot = []
    cad = []
    outliers = []
    fs = rec.frames
    for i, f in enumerate(fs):
        us = None
        if fq and finite(f.get('capture_begin_qpc')) and finite(f.get('capture_end_qpc')):
            us = (float(f['capture_end_qpc']) - float(f['capture_begin_qpc'])) * 1000000.0 / fq
            hot.append(us)
        if i and times[i] is not None and (times[i - 1] is not None) and finite(f.get('predicted_display_period')):
            expected = float(f['predicted_display_period']) / 1000000000.0
            actual = times[i] - times[i - 1]
            if expected > 0:
                ratio = actual / expected
                cad.append(ratio)
                if ratio >= args.frame_interval_warn_ratio:
                    outliers.append({'line': f['_line'], 'time_s': times[i], 'profile': profile_name(f, rec), 'actual_interval_ms': actual * 1000, 'expected_ms': expected * 1000, 'ratio': ratio, 'hotpath_us': us})
    top = []
    for i, f in enumerate(fs):
        if not fq:
            break
        if finite(f.get('capture_begin_qpc')) and finite(f.get('capture_end_qpc')):
            us = (float(f['capture_end_qpc']) - float(f['capture_begin_qpc'])) * 1000000.0 / fq
            top.append({'line': f['_line'], 'serial': f.get('prepared_serial'), 'time_s': times[i], 'profile': profile_name(f, rec), 'hotpath_us': us})
    top.sort(key=lambda x: -x['hotpath_us'])
    return {'hotpath_us': stats(hot), 'frame_interval_ratio_to_predicted_period': stats(cad), 'top_hotpath_frames': top[:args.top_performance], 'frame_interval_outliers': outliers[:args.top_performance]}


# ---------------------------------------------------------------------------
# Persistent grip (PG) x Two-Hand Lab: indexed solve-provenance episodes
# ---------------------------------------------------------------------------
PG_FAMILY_KEY = 'persistent_support_grip_configured'
LAB_FAMILY_KEY = 'two_hand_lab_enabled'
SMOOTHING_FAMILY_KEY = 'two_hand_transition_smoothing_configured'
SUPPORT_EPOCH_UNKNOWN = 2 ** 64 - 1
DEFAULT_A_DOT_B_AGREEMENT_FLOOR = 0.35
LAB_INFLUENCE_EPSILON = 1e-09
PG_EPISODE_ROW_CAP = 30
PG_LAB_NON_CLAIMS = (
    'Episodes are indexed recorded states, never verdicts: an edge does not establish that it caused visible aim motion.',
    'effective_settings.two_hand_latched false or canonical_aim.two_hand_active false is not a disengaged durable persistent-grip relationship.',
    'support_force_one_hand true is explicit persistent-grip forcing of that invocation to the one-hand path; false does not mean the solve stayed two-hand.',
    'support_relationship_readable false is not persistent grip being off; consult persistent_support_grip_configured and persistent_support_grip_applicable for the config knob and title gate.',
    'support_solve_trusted is the qualification permission of that invocation, not a receipt that the solve consumed support geometry.',
    'support_solve_serial is a stamped solve serial that normally lags prepared_serial by one; the recorded lag is reported and the two fields are never asserted equal.',
    'reticle_presented_support_trusted and reticle_presented_support_epoch belong to a different, normally lagged assembly and are never the canonical solve trust.',
    'support_epoch values are process-local and are compared only between adjacent frames of one capture, never across sessions or captures.',
    'Same-frame counterfactual control profiles re-solve independently and do not carry the canonical assembly support facts.',
    'The transition/input smoothing values are recorded amounts and paths, not quality verdicts: two_hand_smoothing_strength (the user amount 0..25 frozen for that serial), two_hand_smoothing_mix (strength/25, the applied wet/dry amount) and two_hand_smoothing_alpha (the filter internal clamp(25*dt, 0, 1) coefficient) are distinct recorded quantities and none of them is a filter speed; two_hand_offhand_influence is the frozen free two-hand product authority, also not a quality verdict.',
    'The two_hand_smoothing_*_error_* values are raw-to-full-filtered input differences, never solver or presented-aim errors; their magnitude is reported descriptively only.',
    'prepared_serial versus reticle_presented_serial differences are reported frame counts; the pair is never asserted equal and a recorded difference is not by itself a defect.',
)


def _bool_or_none(value: Any) -> bool | None:
    return value if isinstance(value, bool) else None


def _int_or_none(value: Any) -> int | None:
    return value if isinstance(value, int) and not isinstance(value, bool) else None


def pg_frame_provenance(f: dict[str, Any]) -> dict[str, Any] | None:
    """Frozen persistent-grip solve provenance of one frame, or None when the
    additive schema-2 family is absent from that frame. Absence is not a
    false/zero state and is reported as not recorded."""
    if PG_FAMILY_KEY not in f:
        return None
    return {'configured': _bool_or_none(f.get(PG_FAMILY_KEY)), 'applicable': _bool_or_none(f.get('persistent_support_grip_applicable')), 'readable': _bool_or_none(f.get('support_relationship_readable')), 'engaged': _bool_or_none(f.get('support_relationship_engaged')), 'epoch': _int_or_none(f.get('support_epoch')), 'trusted': _bool_or_none(f.get('support_solve_trusted')), 'force_one_hand': _bool_or_none(f.get('support_force_one_hand')), 'solve_serial': _int_or_none(f.get('support_solve_serial'))}


def lab_frame_context(f: dict[str, Any]) -> dict[str, Any] | None:
    """Two-Hand Lab family of one frame, or None when the family is absent.

    The Lab values are meaningful only while ``two_hand_lab_enabled`` is true:
    the raw family reads inactive defaults while the Lab does not steer that
    frame's canonical solve, so non-enabled frames report only the gate."""
    if LAB_FAMILY_KEY not in f:
        return None
    enabled = _bool_or_none(f.get(LAB_FAMILY_KEY))
    out: dict[str, Any] = {'lab_enabled': enabled}
    if enabled:
        out.update({'offhand_influence_requested': fnum(f.get('two_hand_lab_offhand_influence')), 'effective_influence': fnum(f.get('two_hand_lab_effective_influence')), 'anchor_requested': _int_or_none(f.get('two_hand_lab_anchor_requested')), 'anchor_resolved': _int_or_none(f.get('two_hand_lab_anchor_resolved')), 'anchor_fallback': _int_or_none(f.get('two_hand_lab_anchor_fallback')), 'agreement_mode': _int_or_none(f.get('two_hand_lab_agreement_mode')), 'agreement': fnum(f.get('two_hand_lab_agreement')), 'agreement_confidence': fnum(f.get('two_hand_lab_agreement_confidence')), 'temporal_mode': _int_or_none(f.get('two_hand_lab_temporal_mode')), 'temporal_active': _bool_or_none(f.get('two_hand_lab_temporal_active')), 'temporal_error_deg': fnum(f.get('two_hand_lab_temporal_error_deg'))})
    return out


def smoothing_frame_context(f: dict[str, Any]) -> dict[str, Any] | None:
    """Two-Hand transition/input smoothing family of one frame, or None when the
    family is absent from that frame. Absence is not a false/zero state.

    ``two_hand_transition_smoothing_configured`` is the family presence marker.
    ``two_hand_smoothing_strength`` and ``two_hand_smoothing_mix`` are the
    strength-slider extension of that family: recordings written by the earlier
    boolean-era candidate carry the booleans, alpha and the error inputs without
    them, so those two read None (not recorded) instead of a fabricated zero.
    ``two_hand_smoothing_alpha`` is the filter's internal clamp(25*dt, 0, 1)
    temporal coefficient and is never the user amount, and the
    ``two_hand_smoothing_*_error_*`` values are raw-to-full-filtered input
    differences, never solver or presented-aim errors.

    ``two_hand_offhand_influence`` is the frozen free two-hand (VS-OFF) product
    offhand directional authority carried beside this family, not a member of
    it: a recording written before the field existed omits the key and that
    absence is not a zero authority."""
    if SMOOTHING_FAMILY_KEY not in f:
        return None
    return {'transition_configured': _bool_or_none(f.get(SMOOTHING_FAMILY_KEY)), 'transition_active': _bool_or_none(f.get('two_hand_transition_active')), 'configured': _bool_or_none(f.get('two_hand_smoothing_configured')), 'applied': _bool_or_none(f.get('two_hand_smoothing_applied')), 'offhand_influence': fnum(f.get('two_hand_offhand_influence')), 'strength': fnum(f.get('two_hand_smoothing_strength')), 'mix': fnum(f.get('two_hand_smoothing_mix')), 'alpha': fnum(f.get('two_hand_smoothing_alpha')), 'primary_orientation_error_deg': fnum(f.get('two_hand_smoothing_primary_orientation_error_deg')), 'primary_position_error_m': fnum(f.get('two_hand_smoothing_primary_position_error_m')), 'support_position_error_m': fnum(f.get('two_hand_smoothing_support_position_error_m'))}


def persistent_grip_lab_analysis(rec: ParsedRecording, times: list[float | None], args: argparse.Namespace) -> dict[str, Any]:
    """Indexed persistent-grip relationship/epoch/trust/forcing episodes with
    the Two-Hand Lab influence context of each event frame, plus the frozen
    Two-Hand transition/input smoothing values of that capture.

    Every list is an exact line/serial/time index. The PG, Lab and smoothing
    families are additive: a capture that lacks them reports them as not
    recorded rather than as false/zero, and epochs are compared only between
    adjacent frames of this capture."""
    fs = rec.frames
    floor = float(getattr(args, 'a_dot_b_agreement_floor', DEFAULT_A_DOT_B_AGREEMENT_FLOOR))
    pg = [pg_frame_provenance(f) for f in fs]
    lab = [lab_frame_context(f) for f in fs]
    smooth = [smoothing_frame_context(f) for f in fs]
    recorded = any(row is not None for row in pg)
    lab_recorded = any(row is not None for row in lab)
    smoothing_recorded = any(row is not None for row in smooth)

    def trace_of(index: int) -> dict[str, Any]:
        trace = fs[index].get('aim_trace', {})
        return trace if isinstance(trace, dict) else {}

    def effective_of(index: int) -> dict[str, Any]:
        eff = fs[index].get('effective_settings', {})
        return eff if isinstance(eff, dict) else {}

    def canonical_of(index: int) -> dict[str, Any]:
        canonical = fs[index].get('canonical_aim', {})
        return canonical if isinstance(canonical, dict) else {}

    def base(index: int, kind: str) -> dict[str, Any]:
        frame = fs[index]
        return {'kind': kind, 'line': frame['_line'], 'serial': frame.get('prepared_serial'), 'time_s': times[index], 'profile': profile_name(frame, rec)}

    def pg_detail(row: dict[str, Any] | None) -> dict[str, Any] | None:
        if row is None:
            return None
        return {'configured': row['configured'], 'applicable': row['applicable'], 'readable': row['readable'], 'engaged': row['engaged'], 'epoch': row['epoch'], 'trusted': row['trusted'], 'force_one_hand': row['force_one_hand']}

    def lab_detail(index: int) -> dict[str, Any] | None:
        row = lab[index]
        if row is None or not row.get('lab_enabled'):
            return None
        return {key: value for key, value in row.items() if key != 'lab_enabled'}

    def run_row(a: int, b: int) -> dict[str, Any]:
        values = [float(trace_of(i)['a_dot_b']) for i in range(a, b + 1) if finite(trace_of(i).get('a_dot_b'))]
        rejected = [float(trace_of(i)['b_rejected_agreement']) for i in range(a, b + 1) if finite(trace_of(i).get('b_rejected_agreement'))]
        return {'start_line': fs[a]['_line'], 'end_line': fs[b]['_line'], 'start_serial': fs[a].get('prepared_serial'), 'end_serial': fs[b].get('prepared_serial'), 'start_time_s': times[a], 'end_time_s': times[b], 'frames': b - a + 1, 'profile': profile_name(fs[a], rec), 'a_dot_b': stats(values), 'b_rejected_agreement': stats(rejected), 'b_accepted_frames': sum(1 for i in range(a, b + 1) if trace_of(i).get('b_accepted') is True), 'b_extreme_rejected_frames': sum(1 for i in range(a, b + 1) if trace_of(i).get('b_extreme_rejected') is True), 'b_steering_retained_frames': sum(1 for i in range(a, b + 1) if trace_of(i).get('b_steering_retained') is True), 'engaged_frames': sum(1 for i in range(a, b + 1) if pg[i] is not None and pg[i]['engaged'] is True) if recorded else None}

    counts = None
    if recorded:
        def count_true(key: str) -> int:
            return sum(1 for row in pg if row is not None and row[key] is True)
        epochs = collections.Counter(row['epoch'] for row in pg if row is not None and row['epoch'] is not None)
        counts = {'frames_total': len(fs), 'frames_with_family': sum(1 for row in pg if row is not None), 'configured_true': count_true('configured'), 'applicable_true': count_true('applicable'), 'readable_true': count_true('readable'), 'engaged_true': count_true('engaged'), 'trusted_true': count_true('trusted'), 'force_one_hand_true': count_true('force_one_hand'), 'epoch_zero_frames': epochs.get(0, 0), 'epoch_unreadable_sentinel_frames': epochs.get(SUPPORT_EPOCH_UNKNOWN, 0), 'epoch_values_observed': [{'epoch': epoch, 'sentinel': epoch == SUPPORT_EPOCH_UNKNOWN, 'frames': count} for epoch, count in sorted(epochs.items())]}

    relationship_transitions: list[dict[str, Any]] = []
    epoch_changes: list[dict[str, Any]] = []
    trust_loss_transitions: list[dict[str, Any]] = []
    if recorded:
        for i in range(1, len(fs)):
            prev, cur = pg[i - 1], pg[i]
            if prev is None or cur is None:
                continue
            changed = [key for key in ('readable', 'engaged') if prev[key] != cur[key]]
            if changed:
                if prev['engaged'] is False and cur['engaged'] is True:
                    kind = 'relationship_acquire'
                elif prev['engaged'] is True and cur['engaged'] is False:
                    kind = 'relationship_release'
                else:
                    kind = 'relationship_readability_change'
                row = base(i, kind)
                row.update({'changed': changed, 'from': pg_detail(prev), 'to': pg_detail(cur), 'lab': lab_detail(i)})
                relationship_transitions.append(row)
            if prev['epoch'] is not None and cur['epoch'] is not None and prev['epoch'] != cur['epoch']:
                row = base(i, 'epoch_change')
                row.update({'from_epoch': prev['epoch'], 'to_epoch': cur['epoch'], 'from_unreadable_sentinel': prev['epoch'] == SUPPORT_EPOCH_UNKNOWN, 'to_unreadable_sentinel': cur['epoch'] == SUPPORT_EPOCH_UNKNOWN, 'engaged_before': prev['engaged'], 'engaged_after': cur['engaged']})
                epoch_changes.append(row)
            if prev['trusted'] is True and cur['trusted'] is False:
                row = base(i, 'trust_loss')
                row.update({'engaged_before': prev['engaged'], 'engaged_after': cur['engaged'], 'epoch_before': prev['epoch'], 'epoch_after': cur['epoch'], 'readable_after': cur['readable'], 'force_one_hand_after': cur['force_one_hand'], 'lab': lab_detail(i)})
                trust_loss_transitions.append(row)

    force_one_hand_runs: list[dict[str, Any]] = []
    force_index_runs: list[tuple[int, int]] = []
    if recorded:
        force_index_runs = _runs([i for i, row in enumerate(pg) if row is not None and row['force_one_hand'] is True])
        for a, b in force_index_runs:
            force_one_hand_runs.append({'start_line': fs[a]['_line'], 'end_line': fs[b]['_line'], 'start_serial': fs[a].get('prepared_serial'), 'end_serial': fs[b].get('prepared_serial'), 'start_time_s': times[a], 'end_time_s': times[b], 'frames': b - a + 1, 'profile': profile_name(fs[a], rec), 'trusted_frames': sum(1 for row in pg[a:b + 1] if row is not None and row['trusted'] is True), 'engaged_frames': sum(1 for row in pg[a:b + 1] if row is not None and row['engaged'] is True), 'epochs': sorted({row['epoch'] for row in pg[a:b + 1] if row is not None and row['epoch'] is not None})})

    two_hand_to_one_hand: list[dict[str, Any]] = []
    if recorded:
        for i in range(1, len(fs)):
            cur = pg[i]
            if cur is None or cur['engaged'] is not True:
                continue
            if _bool_or_none(canonical_of(i - 1).get('two_hand_active')) is True and _bool_or_none(canonical_of(i).get('two_hand_active')) is False:
                row = base(i, 'two_hand_to_one_hand_while_engaged')
                row.update({'engaged_after': True, 'trusted_after': cur['trusted'], 'force_one_hand_after': cur['force_one_hand'], 'epoch_after': cur['epoch'], 'two_hand_latched_before': _bool_or_none(effective_of(i - 1).get('two_hand_latched')), 'two_hand_latched_after': _bool_or_none(effective_of(i).get('two_hand_latched')), 'lab': lab_detail(i)})
                two_hand_to_one_hand.append(row)

    agreement_index_runs = _runs([i for i in range(len(fs)) if finite(trace_of(i).get('a_dot_b')) and float(trace_of(i)['a_dot_b']) < floor])
    agreement_below_floor_runs = [run_row(a, b) for a, b in agreement_index_runs]
    steering_index_runs = _runs([i for i in range(len(fs)) if trace_of(i).get('b_steering_retained') is True])
    b_steering_retained_runs = [run_row(a, b) for a, b in steering_index_runs]

    lab_summary = None
    if lab_recorded:
        steering = [i for i, row in enumerate(lab) if row is not None and row.get('lab_enabled')]

        def influence_delta(row: dict[str, Any]) -> float | None:
            requested = row.get('offhand_influence_requested')
            effective = row.get('effective_influence')
            if requested is None or effective is None:
                return None
            return float(effective) - float(requested)

        deltas = [delta for delta in (influence_delta(lab[i]) for i in steering) if delta is not None]
        divergence_rows: list[dict[str, Any]] = []
        for i in steering:
            row = lab[i]
            delta = influence_delta(row)
            if delta is None or abs(delta) <= LAB_INFLUENCE_EPSILON:
                continue
            divergence_rows.append({'line': fs[i]['_line'], 'serial': fs[i].get('prepared_serial'), 'time_s': times[i], 'profile': profile_name(fs[i], rec), 'offhand_influence_requested': row.get('offhand_influence_requested'), 'effective_influence': row.get('effective_influence'), 'effective_minus_requested': delta, 'anchor_requested': row.get('anchor_requested'), 'anchor_resolved': row.get('anchor_resolved'), 'anchor_fallback': row.get('anchor_fallback'), 'agreement_mode': row.get('agreement_mode'), 'agreement': row.get('agreement'), 'agreement_confidence': row.get('agreement_confidence'), 'temporal_mode': row.get('temporal_mode'), 'temporal_active': row.get('temporal_active'), 'temporal_error_deg': row.get('temporal_error_deg')})
        lab_summary = {'frames_with_family': sum(1 for row in lab if row is not None), 'steering_frames': len(steering), 'requested_vs_effective_divergence_frames': len(divergence_rows), 'effective_minus_requested': stats(deltas), 'frames': divergence_rows[:PG_EPISODE_ROW_CAP], 'frames_omitted': max(0, len(divergence_rows) - PG_EPISODE_ROW_CAP)}

    smoothing_summary = None
    if smoothing_recorded:
        family_rows = [row for row in smooth if row is not None]
        applied_indexes = [i for i, row in enumerate(smooth) if row is not None and row['applied'] is True]
        strength_values = [row['strength'] for row in family_rows if row['strength'] is not None]
        offhand_values = [row['offhand_influence'] for row in family_rows if row['offhand_influence'] is not None]

        def family_flag_count(key: str, value: bool) -> int:
            return sum(1 for row in family_rows if row[key] is value)

        def applied_stats(key: str) -> dict[str, Any] | None:
            return stats([smooth[i][key] for i in applied_indexes if smooth[i][key] is not None])

        latch_pairs: collections.Counter = collections.Counter()
        latch_partial_frames = 0
        for i, row in enumerate(smooth):
            if row is None:
                continue
            latched = _bool_or_none(effective_of(i).get('two_hand_latched'))
            if row['transition_active'] is None or latched is None:
                latch_partial_frames += 1
                continue
            latch_pairs[(row['transition_active'], latched)] += 1

        pg_applied = [pg[i] for i in applied_indexes]
        lab_applied = [lab[i] for i in applied_indexes]
        lab_enabled_applied = [lab[i] for i in applied_indexes if lab[i] is not None and lab[i].get('lab_enabled')]

        def pg_flag_counts(rows: list[dict[str, Any] | None], key: str) -> dict[str, int]:
            return {'true': sum(1 for row in rows if row is not None and row[key] is True), 'false': sum(1 for row in rows if row is not None and row[key] is False), 'absent': sum(1 for row in rows if row is None or row[key] is None)}

        presented_frames = [fs[i] for i in applied_indexes]

        def presented_flag_counts(key: str) -> dict[str, int]:
            values = [_bool_or_none(f.get(key)) for f in presented_frames]
            return {'true': sum(1 for value in values if value is True), 'false': sum(1 for value in values if value is False), 'absent': sum(1 for value in values if value is None)}

        reticle_deltas: collections.Counter = collections.Counter()
        for f in presented_frames:
            prepared = _int_or_none(f.get('prepared_serial'))
            reticle = _int_or_none(f.get('reticle_presented_serial'))
            if prepared is None or reticle is None:
                continue
            reticle_deltas[prepared - reticle] += 1

        smoothing_summary = {
            'frames_total': len(fs),
            'frames_with_family': len(family_rows),
            'transition_configured_true_frames': family_flag_count('transition_configured', True),
            'transition_active_true_frames': family_flag_count('transition_active', True),
            'configured_true_frames': family_flag_count('configured', True),
            'applied_true_frames': len(applied_indexes),
            'strength_recorded_frames': len(strength_values),
            'mix_recorded_frames': sum(1 for row in family_rows if row['mix'] is not None),
            'strength_mix_recorded': bool(strength_values),
            'offhand_influence_recorded_frames': len(offhand_values),
            'offhand_influence': stats(offhand_values),
            'applied': {
                'frames': len(applied_indexes),
                'strength': applied_stats('strength'),
                'mix': applied_stats('mix'),
                'alpha': applied_stats('alpha'),
                'primary_orientation_error_deg': applied_stats('primary_orientation_error_deg'),
                'primary_position_error_m': applied_stats('primary_position_error_m'),
                'support_position_error_m': applied_stats('support_position_error_m'),
            },
            'latch_coincidence': {
                'frames_compared': sum(latch_pairs.values()),
                'pairs': {'transition_active_true_latched_true': latch_pairs[(True, True)], 'transition_active_true_latched_false': latch_pairs[(True, False)], 'transition_active_false_latched_true': latch_pairs[(False, True)], 'transition_active_false_latched_false': latch_pairs[(False, False)]},
                'frames_with_either_absent': latch_partial_frames,
            },
            'pg_context_on_applied': {'frames': len(applied_indexes), 'pg_family_frames': sum(1 for row in pg_applied if row is not None), 'engaged': pg_flag_counts(pg_applied, 'engaged'), 'trusted': pg_flag_counts(pg_applied, 'trusted'), 'configured': pg_flag_counts(pg_applied, 'configured')},
            'lab_context_on_applied': {'frames': len(applied_indexes), 'lab_family_frames': sum(1 for row in lab_applied if row is not None), 'lab_enabled_true': len(lab_enabled_applied), 'effective_influence': stats([row.get('effective_influence') for row in lab_enabled_applied if row.get('effective_influence') is not None]), 'offhand_influence_requested': stats([row.get('offhand_influence_requested') for row in lab_enabled_applied if row.get('offhand_influence_requested') is not None])},
            'presented_context_on_applied': {'frames': len(applied_indexes), 'recorded': any('presented_aim_valid' in f or 'reticle_presented_valid' in f or 'reticle_presented_serial' in f for f in presented_frames), 'presented_aim_valid': presented_flag_counts('presented_aim_valid'), 'reticle_presented_valid': presented_flag_counts('reticle_presented_valid'), 'prepared_minus_reticle_presented_serial': {'frames_compared': sum(reticle_deltas.values()), 'delta_values': {str(delta): reticle_deltas[delta] for delta in sorted(reticle_deltas)}, 'delta_zero_frames': reticle_deltas.get(0, 0), 'delta_positive_frames': sum(count for delta, count in reticle_deltas.items() if delta > 0), 'delta_negative_frames': sum(count for delta, count in reticle_deltas.items() if delta < 0)}},
        }

    solve_serial_lag = None
    if recorded:
        lag_values = []
        for i, row in enumerate(pg):
            if row is None or row['solve_serial'] is None:
                continue
            prepared = _int_or_none(fs[i].get('prepared_serial'))
            if prepared is None:
                continue
            lag_values.append(prepared - row['solve_serial'])
        lag_counter = collections.Counter(lag_values)
        solve_serial_lag = {'frames_compared': len(lag_values), 'delta_values': {str(key): lag_counter[key] for key in sorted(lag_counter)}, 'min_delta': min(lag_values) if lag_values else None, 'max_delta': max(lag_values) if lag_values else None, 'frames_with_delta_zero': lag_counter.get(0, 0), 'frames_ahead_of_prepared_serial': sum(1 for delta in lag_values if delta < 0)}

    events: list[dict[str, Any]] = []
    for a, b in agreement_index_runs:
        for index, kind in ((a, 'a_dot_b_below_floor_begin'), (b, 'a_dot_b_below_floor_end')):
            row = base(index, kind)
            row.update({'a_dot_b': fnum(trace_of(index).get('a_dot_b')), 'b_accepted': _bool_or_none(trace_of(index).get('b_accepted')), 'b_extreme_rejected': _bool_or_none(trace_of(index).get('b_extreme_rejected')), 'b_rejected_agreement': fnum(trace_of(index).get('b_rejected_agreement')), 'b_steering_retained': _bool_or_none(trace_of(index).get('b_steering_retained')), 'pg': pg_detail(pg[index]), 'lab': lab_detail(index)})
            events.append(row)
    for a, b in steering_index_runs:
        for index, kind in ((a, 'b_steering_retained_begin'), (b, 'b_steering_retained_end')):
            row = base(index, kind)
            row.update({'a_dot_b': fnum(trace_of(index).get('a_dot_b')), 'b_accepted': _bool_or_none(trace_of(index).get('b_accepted')), 'pg': pg_detail(pg[index]), 'lab': lab_detail(index)})
            events.append(row)
    if recorded:
        events.extend(relationship_transitions)
        events.extend(epoch_changes)
        events.extend(trust_loss_transitions)
        events.extend(two_hand_to_one_hand)
        for a, b in force_index_runs:
            for index, kind in ((a, 'force_one_hand_begin'), (b, 'force_one_hand_end')):
                row = base(index, kind)
                row.update({'pg': pg_detail(pg[index]), 'lab': lab_detail(index)})
                events.append(row)
    events.sort(key=lambda row: (row['line'], row['kind']))

    semantics = {
        'family_scope': 'frozen solve-time persistent-grip provenance of the frame own canonical assembly; never a live re-read of persistent-grip state',
        'epoch_scope': 'support_epoch is process-local; it is compared only between adjacent frames of this capture, never across sessions or captures',
        'epoch_sentinel': '18446744073709551615 means the relationship could not be read; epoch 0 means it was readable and coherently disengaged, and an unreadable relationship is not a disengaged one',
        'trust': 'support_solve_trusted is the qualification permission of that invocation; whether the solve consumed support geometry is a separate, unrecorded narrowing',
        'forcing': 'support_force_one_hand is explicit persistent-grip forcing of this invocation, distinct from an ordinary two_hand_enabled false which conflates config-off, dual presentation and forcing',
        'serial_lag': 'support_solve_serial is the stamped solve serial; the observed prepared_serial delta is reported and equality is never asserted',
        'agreement_floor': 'aim_trace.a_dot_b is the recorded A.B agreement; the floor is the legacy 0.35 threshold disclosed as a parameter',
        'lab_scope': 'two_hand_lab_* values are reported only while two_hand_lab_enabled is true, because the Lab does not steer the canonical solve otherwise',
        'latched_versus_relationship': 'effective_settings.two_hand_latched and canonical_aim.two_hand_active are a different layer from the durable persistent-grip relationship and are reported as context only',
        'episode_shape': 'episodes are maximal runs or adjacent-frame edges over the recorded flags; the structured query lists are never capped, the human-readable tables cap at the disclosed row_cap',
        'smoothing_scope': 'the two_hand_transition_* / two_hand_smoothing_* family is the frozen transition/input smoothing state of that prepared serial; absence of the family, or of strength/mix inside it, is not recorded and is never coerced to false/zero',
        'smoothing_strength': 'two_hand_smoothing_strength is the user amount 0..25 (0 = raw/off, 25 = the full fixed speed-25 input filter) frozen for this prepared serial, never a live re-read of the config slider',
        'smoothing_mix': 'two_hand_smoothing_mix is strength/25: the applied wet/dry amount, never a filter speed',
        'smoothing_alpha': 'two_hand_smoothing_alpha is the filter internal clamp(25*dt, 0, 1) temporal coefficient and is never the user amount',
        'smoothing_applied': 'two_hand_smoothing_applied means the eligible free two-hand path of this prepared frame consumed the frozen strength; two_hand_smoothing_configured is strength > 0',
        'smoothing_errors': 'the two_hand_smoothing_*_error_* values are raw-to-full-filtered input differences in degrees / OpenXR local metres, never solver or presented-aim errors',
        'offhand_influence': 'two_hand_offhand_influence is the free two-hand (VS-OFF) product offhand directional authority 0..1 frozen for this prepared serial from the frame own assembly (non-finite reads 0, otherwise clamped exactly as the solver consumes it), never a live re-read of the config slider; Virtual Stock solves never read it and a Lab-active solve uses its own two_hand_lab_offhand_influence, and absence of the key in older captures is not a zero authority',
        'smoothing_latch_coincidence': 'two_hand_transition_smoothing_configured / two_hand_transition_active are the product 200 ms latch-continuity pair: the continuity is fixed-on internal behaviour with no user toggle, while two_hand_transition_smoothing_configured reports the effective truth for the frame (the retired two_hand_transition_smoothing key no longer resolves); the same-frame pair counts against effective_settings.two_hand_latched are descriptive coincidence counts only',
        'smoothing_presented_lag': 'presented_aim_valid and reticle_presented_valid/reticle_presented_serial belong to the normally lagged present/reticle assembly; the serial difference is reported as frame counts on applied frames and equality is never asserted',
    }
    return {
        'method': 'persistent_grip_lab_episodes',
        'classification': 'deterministic_derivation',
        'recorded': recorded,
        'lab_recorded': lab_recorded,
        'smoothing_recorded': smoothing_recorded,
        'parameters': {
            'a_dot_b_agreement_floor': floor,
            'lab_influence_epsilon': LAB_INFLUENCE_EPSILON,
            'row_cap': PG_EPISODE_ROW_CAP,
        },
        'counts': counts,
        'relationship_transitions': relationship_transitions,
        'epoch_changes': epoch_changes,
        'trust_loss_transitions': trust_loss_transitions,
        'force_one_hand_runs': force_one_hand_runs,
        'two_hand_to_one_hand_while_engaged': two_hand_to_one_hand,
        'agreement_below_floor_runs': agreement_below_floor_runs,
        'b_steering_retained_runs': b_steering_retained_runs,
        'lab': lab_summary,
        'smoothing': smoothing_summary,
        'support_solve_serial_lag': solve_serial_lag,
        'events': events,
        'semantics': semantics,
        'non_claims': list(PG_LAB_NON_CLAIMS),
    }


def persistent_grip_lab_markdown(p: dict[str, Any]) -> list[str]:
    out = ['', '## Persistent grip (PG) \u00d7 Two-Hand Lab indexed episodes', '', 'Deterministic index over recorded frozen persistent-grip solve-time provenance and the existing agreement/steering fields around it. Episodes are line/serial/time indexes, not verdicts.', '']
    for note in p['non_claims']:
        out.append(f'- {note}')
    out.append('')
    if not p['recorded']:
        out.append('The persistent-grip family is **not recorded** in this capture: the family keys are absent, which is neither false nor zero, so the PG relationship/epoch/trust/forcing lists below are empty for that reason.')
        out.append('')
    else:
        c = p['counts']
        epoch_text = ', '.join(f"`{row['epoch']}`" + (' (unreadable sentinel)' if row['sentinel'] else '') + ' x' + str(row['frames']) for row in c['epoch_values_observed']) or 'none'
        out += [f"Frames with the family: **{c['frames_with_family']}** of {c['frames_total']}. True counts: configured **{c['configured_true']}**, applicable **{c['applicable_true']}**, readable **{c['readable_true']}**, engaged **{c['engaged_true']}**, trusted **{c['trusted_true']}**, force-one-hand **{c['force_one_hand_true']}**.", '', f"Epoch 0 (readable, coherently disengaged) frames: **{c['epoch_zero_frames']}**; unreadable all-ones sentinel frames: **{c['epoch_unreadable_sentinel_frames']}**; epochs observed in this capture: {epoch_text}.", '']
        lag = p['support_solve_serial_lag']
        if lag is not None:
            deltas = ', '.join(f"{key}:{value}" for key, value in sorted(lag['delta_values'].items(), key=lambda item: int(item[0])))
            out += [f"`prepared_serial` \u2212 `support_solve_serial`: frames compared **{lag['frames_compared']}**; delta frame counts `{deltas or 'none'}`; delta 0 **{lag['frames_with_delta_zero']}**; serial ahead of prepared_serial **{lag['frames_ahead_of_prepared_serial']}**. The two serials are never asserted equal.", '']
        out += [f"Relationship edges (`support_relationship_readable`/`engaged` changes): **{len(p['relationship_transitions'])}**.", '']
        if p['relationship_transitions']:
            out += ['| Kind | Line | Time | Profile | Readable | Engaged | Epoch | Trusted | Force |', '|---|---:|---:|---|---|---|---:|---|---|']
            for row in p['relationship_transitions'][:PG_EPISODE_ROW_CAP]:
                a, b = row['from'], row['to']
                out.append(f"| `{row['kind']}` | {row['line']} | {fmt(row['time_s'])} | `{row['profile']}` | {fmt(a['readable'])}\u2192{fmt(b['readable'])} | {fmt(a['engaged'])}\u2192{fmt(b['engaged'])} | {fmt(a['epoch'])}\u2192{fmt(b['epoch'])} | {fmt(a['trusted'])}\u2192{fmt(b['trusted'])} | {fmt(a['force_one_hand'])}\u2192{fmt(b['force_one_hand'])} |")
            if len(p['relationship_transitions']) > PG_EPISODE_ROW_CAP:
                out.append(f"_{len(p['relationship_transitions']) - PG_EPISODE_ROW_CAP} further edge rows omitted from this table._")
        out += ['', f"Epoch changes (adjacent frames, same capture): **{len(p['epoch_changes'])}**.", '']
        for row in p['epoch_changes'][:PG_EPISODE_ROW_CAP]:
            out.append(f"- line {row['line']} ({fmt(row['time_s'])} s, `{row['profile']}`): epoch `{row['from_epoch']}` \u2192 `{row['to_epoch']}`" + (' (unreadable sentinel entered)' if row['to_unreadable_sentinel'] else '') + (' (unreadable sentinel left)' if row['from_unreadable_sentinel'] else '') + f"; engaged {fmt(row['engaged_before'])}\u2192{fmt(row['engaged_after'])}.")
        out += ['', f"Trusted \u2192 untrusted solve transitions: **{len(p['trust_loss_transitions'])}**; of those still engaged on the transition frame: **{sum(1 for row in p['trust_loss_transitions'] if row['engaged_after'] is True)}**.", '']
        for row in p['trust_loss_transitions'][:PG_EPISODE_ROW_CAP]:
            out.append(f"- line {row['line']} ({fmt(row['time_s'])} s, `{row['profile']}`): engaged {fmt(row['engaged_before'])}\u2192{fmt(row['engaged_after'])}, epoch `{fmt(row['epoch_before'])}`\u2192`{fmt(row['epoch_after'])}`, force-one-hand after {fmt(row['force_one_hand_after'])}.")
        out += ['', f"Two-hand \u2192 one-hand transitions while the durable relationship remained engaged: **{len(p['two_hand_to_one_hand_while_engaged'])}** (the latch is context, not the relationship).", '']
        for row in p['two_hand_to_one_hand_while_engaged'][:PG_EPISODE_ROW_CAP]:
            out.append(f"- line {row['line']} ({fmt(row['time_s'])} s, `{row['profile']}`): `canonical_aim.two_hand_active` true\u2192false, engaged {fmt(row['engaged_after'])}, trusted {fmt(row['trusted_after'])}, force-one-hand {fmt(row['force_one_hand_after'])}, `two_hand_latched` {fmt(row['two_hand_latched_before'])}\u2192{fmt(row['two_hand_latched_after'])}.")
        out += ['', f"Explicit persistent-grip force-one-hand runs: **{len(p['force_one_hand_runs'])}**.", '']
        if p['force_one_hand_runs']:
            out += ['| Lines | Time | Frames | Profile | Trusted frames | Engaged frames | Epochs |', '|---|---:|---:|---|---:|---:|---|']
            for row in p['force_one_hand_runs'][:PG_EPISODE_ROW_CAP]:
                out.append(f"| {row['start_line']}\u2013{row['end_line']} | {fmt(row['start_time_s'])}\u2013{fmt(row['end_time_s'])} | {row['frames']} | `{row['profile']}` | {row['trusted_frames']} | {row['engaged_frames']} | {', '.join('`' + str(epoch) + '`' for epoch in row['epochs']) or '-'} |")
    out += ['', f"`aim_trace.a_dot_b` below **{p['parameters']['a_dot_b_agreement_floor']:.3f}** runs: **{len(p['agreement_below_floor_runs'])}** (frames without a finite `a_dot_b` stay missing and are never coerced to zero).", '']
    if p['agreement_below_floor_runs']:
        out += ['| Lines | Time | Frames | Profile | A.B | B accepted | B extreme rejected | B rejected agreement | Steering retained frames | Engaged frames |', '|---|---:|---:|---|---|---:|---:|---|---:|---:|']
        for row in p['agreement_below_floor_runs'][:PG_EPISODE_ROW_CAP]:
            out.append(f"| {row['start_line']}\u2013{row['end_line']} | {fmt(row['start_time_s'])}\u2013{fmt(row['end_time_s'])} | {row['frames']} | `{row['profile']}` | {fmt_stats(row['a_dot_b'])} | {row['b_accepted_frames']} | {row['b_extreme_rejected_frames']} | {fmt_stats(row['b_rejected_agreement'])} | {row['b_steering_retained_frames']} | {fmt(row['engaged_frames'])} |")
    out += ['', f"`aim_trace.b_steering_retained` true runs: **{len(p['b_steering_retained_runs'])}**.", '']
    if p['b_steering_retained_runs']:
        out += ['| Lines | Time | Frames | Profile | A.B | B accepted | Engaged frames |', '|---|---:|---:|---|---|---:|---:|']
        for row in p['b_steering_retained_runs'][:PG_EPISODE_ROW_CAP]:
            out.append(f"| {row['start_line']}\u2013{row['end_line']} | {fmt(row['start_time_s'])}\u2013{fmt(row['end_time_s'])} | {row['frames']} | `{row['profile']}` | {fmt_stats(row['a_dot_b'])} | {row['b_accepted_frames']} | {fmt(row['engaged_frames'])} |")
    lab = p['lab']
    out += ['']
    if lab is None:
        out.append('The Two-Hand Lab family is **not recorded** in this capture: the Lab keys are absent, which is neither false nor zero, so no requested-versus-effective influence comparison is available.')
    else:
        epsilon = p['parameters']['lab_influence_epsilon']
        out += [f"Two-Hand Lab: **{lab['steering_frames']}** frames with `two_hand_lab_enabled` true (only those carry meaningful Lab values; `{lab['frames_with_family']}` frames carry the family); **{lab['requested_vs_effective_divergence_frames']}** steering frames have an effective influence different from the requested influence (epsilon {epsilon:g}).", '', f"effective \u2212 requested influence on steering frames: {fmt_stats(lab['effective_minus_requested'])}", '']
        if lab['frames']:
            out += ['| Line | Time | Profile | Requested | Effective | Effective − requested | Anchor req/res (fallback) | Agreement mode/value/confidence | Temporal mode/active/error |', '|---:|---:|---|---:|---:|---:|---|---|---|']
            for row in lab['frames']:
                out.append(f"| {row['line']} | {fmt(row['time_s'])} | `{row['profile']}` | {fmt(row['offhand_influence_requested'])} | {fmt(row['effective_influence'])} | {fmt(row['effective_minus_requested'])} | {row['anchor_requested']}/{row['anchor_resolved']} ({row['anchor_fallback']}) | {row['agreement_mode']} / {fmt(row['agreement'])} / {fmt(row['agreement_confidence'])} | {row['temporal_mode']} / {fmt(row['temporal_active'])} / {fmt(row['temporal_error_deg'])} |")
            if lab['frames_omitted']:
                out.append(f"_{lab['frames_omitted']} further divergence rows omitted from this table._")
    smooth = p['smoothing']
    out += ['', '## Two-Hand transition/input smoothing values', '']
    if smooth is None:
        out.append('The Two-Hand transition/input smoothing family is **not recorded** in this capture: `two_hand_transition_smoothing_configured` and the rest of the family keys are absent, which is neither false nor zero, so no smoothing counts or distributions are available.')
    else:
        applied = smooth['applied']
        out += [f"Frames with the family: **{smooth['frames_with_family']}** of {smooth['frames_total']}. True counts: `two_hand_transition_smoothing_configured` **{smooth['transition_configured_true_frames']}**, `two_hand_transition_active` **{smooth['transition_active_true_frames']}**, `two_hand_smoothing_configured` **{smooth['configured_true_frames']}**, `two_hand_smoothing_applied` **{smooth['applied_true_frames']}**.", '', f"Smoothing-applied frames: **{applied['frames']}**.", '']
        if smooth['strength_mix_recorded']:
            out += [f"`two_hand_smoothing_strength` (the user amount 0..25, frozen for that prepared serial) is recorded on **{smooth['strength_recorded_frames']}** family frames; on the applied frames it reads {fmt_stats(applied['strength'])}.", f"`two_hand_smoothing_mix` (= strength/25; the applied wet/dry amount, never a filter speed) on the applied frames: {fmt_stats(applied['mix'])}.", '']
        else:
            out += ['`two_hand_smoothing_strength` and `two_hand_smoothing_mix` are **not recorded** in this capture: the family is present but was written by the boolean-era recorder that carries only the booleans, `two_hand_smoothing_alpha` and the error inputs, so there is no strength or mix distribution and absence is not a zero strength.', '']
        if smooth['offhand_influence_recorded_frames']:
            out += [f"`two_hand_offhand_influence` (the free two-hand product offhand directional authority 0..1, frozen for that prepared serial from the frame's own assembly; Virtual Stock solves never read it) is recorded on **{smooth['offhand_influence_recorded_frames']}** family frames: {fmt_stats(smooth['offhand_influence'])}.", '']
        else:
            out += ['`two_hand_offhand_influence` is **not recorded** in this capture (written before the field existed); absence is not a zero authority.', '']
        out += [f"`two_hand_smoothing_alpha` on the applied frames (the filter internal clamp(25*dt, 0, 1) temporal coefficient, never the user amount): {fmt_stats(applied['alpha'])}.", '', f"Raw-to-full-filtered input differences on the applied frames (degrees / OpenXR local metres; never solver or presented-aim errors): primary orientation {fmt_stats(applied['primary_orientation_error_deg'])}; primary position {fmt_stats(applied['primary_position_error_m'])}; support position {fmt_stats(applied['support_position_error_m'])}.", '']
        latch = smooth['latch_coincidence']
        pairs = latch['pairs']
        out += [f"Same-frame `two_hand_transition_active` versus `effective_settings.two_hand_latched` pairs where both are recorded: (true,true) **{pairs['transition_active_true_latched_true']}**, (true,false) **{pairs['transition_active_true_latched_false']}**, (false,true) **{pairs['transition_active_false_latched_true']}**, (false,false) **{pairs['transition_active_false_latched_false']}**; frames with either field absent **{latch['frames_with_either_absent']}**. These are coincidence counts, not a continuity verdict.", '']
        pgc = smooth['pg_context_on_applied']
        labc = smooth['lab_context_on_applied']
        out += [f"Context on the applied frames: persistent-grip family present **{pgc['pg_family_frames']}** of {pgc['frames']} (engaged true/false/absent {pgc['engaged']['true']}/{pgc['engaged']['false']}/{pgc['engaged']['absent']}, trusted {pgc['trusted']['true']}/{pgc['trusted']['false']}/{pgc['trusted']['absent']}); Two-Hand Lab family present **{labc['lab_family_frames']}** with `two_hand_lab_enabled` true **{labc['lab_enabled_true']}**, effective influence {fmt_stats(labc['effective_influence'])} on those enabled frames.", '']
        presc = smooth['presented_context_on_applied']
        delta = presc['prepared_minus_reticle_presented_serial']
        deltas = ', '.join(f"{key}:{value}" for key, value in sorted(delta['delta_values'].items(), key=lambda item: int(item[0]))) or 'none'
        if presc['recorded']:
            out += [f"Present/reticle context on the applied frames: `presented_aim_valid` true/false/absent {presc['presented_aim_valid']['true']}/{presc['presented_aim_valid']['false']}/{presc['presented_aim_valid']['absent']}; `reticle_presented_valid` {presc['reticle_presented_valid']['true']}/{presc['reticle_presented_valid']['false']}/{presc['reticle_presented_valid']['absent']}; `prepared_serial` \u2212 `reticle_presented_serial` frame counts `{deltas}` over {delta['frames_compared']} compared frames. The two serials are never asserted equal.", '']
        else:
            out += ['The present/reticle fields (`presented_aim_valid`, `reticle_presented_valid`, `reticle_presented_serial`) are **not recorded** on the applied frames of this capture, so no present/reticle lag context is available.', '']
    out += ['', f"Merged event index: **{len(p['events'])}** indexed episodes (query `pg-lab-events`). Epochs are never compared across captures, so multi-capture consolidation deliberately carries no PG/Lab episode table.", '']
    return out


# ---------------------------------------------------------------------------
# V5.2 temporary-profile provenance hardening
# ---------------------------------------------------------------------------
TEMPORARY_PROFILE_ID_MIN = 128
TEMPORARY_PROFILE_ID_MAX = 254
RESERVED_PROFILE_ID = 255
STATIC_SETTINGS_FINGERPRINT_FORMAT = 'sha256:mccvr-static-effective-settings-canonical-v1'
PROFILE_MAPPING_FINGERPRINT_FORMAT = 'sha256:mccvr-profile-mapping-canonical-v1'


def is_dynamic_effective_field(path: str) -> bool:
    """Authoritative classification seam for per-frame dynamic effective state."""
    return path in DYNAMIC_EFFECTIVE_FIELDS


def _canonical_output_value(value: Any) -> Any:
    """JSON-safe, deterministically ordered object used as inspectable evidence."""
    if value is None or isinstance(value, (bool, str, int)):
        return value
    if isinstance(value, float):
        if math.isnan(value):
            return {'__nonfinite__': 'nan'}
        if math.isinf(value):
            return {'__nonfinite__': 'inf' if value > 0 else '-inf'}
        return 0.0 if value == 0.0 else float(format(value, '.17g'))
    if isinstance(value, (list, tuple)):
        return [_canonical_output_value(x) for x in value]
    if isinstance(value, dict):
        return {str(k): _canonical_output_value(value[k]) for k in sorted(value, key=lambda x: str(x))}
    return {'__type__': type(value).__name__, '__value__': str(value)}


def _canonical_token_bytes(value: Any) -> bytes:
    """Typed length-delimited canonical serialization; no repr()/platform float formatting."""
    if value is None:
        return b'n;'
    if value is True:
        return b'b1;'
    if value is False:
        return b'b0;'
    if isinstance(value, int) and not isinstance(value, bool):
        return ('i' + str(value) + ';').encode('ascii')
    if isinstance(value, float):
        if math.isnan(value):
            return b'f:nan;'
        if math.isinf(value):
            return b'f:+inf;' if value > 0 else b'f:-inf;'
        if value == 0.0:
            txt = '0'
        else:
            txt = format(value, '.17g').replace('E', 'e')
        return ('f:' + txt + ';').encode('ascii')
    if isinstance(value, str):
        b = value.encode('utf-8')
        return b's' + str(len(b)).encode('ascii') + b':' + b
    if isinstance(value, (list, tuple)):
        parts = [b'l', str(len(value)).encode('ascii'), b':']
        parts.extend(_canonical_token_bytes(x) for x in value)
        return b''.join(parts)
    if isinstance(value, dict):
        keys = sorted((str(k) for k in value.keys()))
        parts = [b'd', str(len(keys)).encode('ascii'), b':']
        for k in keys:
            parts.append(_canonical_token_bytes(k))
            parts.append(_canonical_token_bytes(value[k]))
        return b''.join(parts)
    # JSON telemetry/settings should not reach here, but encode deterministically if it does.
    return _canonical_token_bytes({'__type__': type(value).__name__, '__value__': str(value)})


def canonical_sha256(value: Any) -> str:
    return hashlib.sha256(_canonical_token_bytes(value)).hexdigest()


def canonical_static_effective_settings(effective: Any) -> dict[str, Any]:
    """Remove only fields classified as genuinely dynamic; preserve every static solver setting."""
    if not isinstance(effective, dict):
        return {}

    def walk(value: Any, prefix: str='') -> Any:
        if isinstance(value, dict):
            out = {}
            for raw_key in sorted(value, key=lambda x: str(x)):
                key = str(raw_key)
                path = f'{prefix}.{key}' if prefix else key
                if is_dynamic_effective_field(path):
                    continue
                out[key] = walk(value[raw_key], path)
            return out
        if isinstance(value, (list, tuple)):
            return [walk(x, prefix) for x in value]
        return _canonical_output_value(value)

    return walk(effective)


def static_effective_settings_fingerprint(effective: Any) -> tuple[dict[str, Any], str]:
    obj = canonical_static_effective_settings(effective)
    return obj, canonical_sha256(obj)


def session_profile_mapping_canonical(session_start: dict[str, Any], schema_version: int) -> list[Any] | None:
    if schema_version < 2:
        return None
    mapping = session_start.get('test_profile_enum_mapping')
    if not isinstance(mapping, list):
        return []
    # Preserve mapping/list order deliberately; canonicalize keys/numbers inside each row.
    return [_canonical_output_value(row) for row in mapping]


def profile_mapping_fingerprint(session_start: dict[str, Any], schema_version: int) -> str | None:
    obj = session_profile_mapping_canonical(session_start, schema_version)
    return None if obj is None else canonical_sha256(obj)


def _mapping_row_for_profile(rec: ParsedRecording, profile: ProfileDef) -> dict[str, Any] | None:
    mapping = rec.session_start.get('test_profile_enum_mapping')
    if not isinstance(mapping, list):
        return None
    for row in mapping:
        if not isinstance(row, dict):
            continue
        try:
            rid = int(row.get('id'))
        except (TypeError, ValueError):
            continue
        if rid == profile.id:
            return row
    return None


def is_temporary_profile(rec: ParsedRecording, profile: ProfileDef) -> bool:
    if rec.schema_version < 2:
        return False
    if not (TEMPORARY_PROFILE_ID_MIN <= profile.id <= TEMPORARY_PROFILE_ID_MAX):
        return False
    # Schema-2 temporary identity is file-local and should be described by the file mapping.
    return _mapping_row_for_profile(rec, profile) is not None


def profile_condition_identity_for_frame(frame: dict[str, Any], rec: ParsedRecording) -> dict[str, Any]:
    p = frame_profile(frame, rec.registry)
    temp = is_temporary_profile(rec, p)
    if temp:
        static_obj, fp = static_effective_settings_fingerprint(frame.get('effective_settings', {}))
        key_payload = {'stable_name': p.name, 'settings_fingerprint': fp}
        key = 'temporary:' + canonical_sha256(key_payload)
        row = _mapping_row_for_profile(rec, p) or {}
        uses_custom = row.get('uses_custom_settings', row.get('use_custom_settings'))
        return {
            'temporary': True,
            'id_in_recording': p.id,
            'stable_name': p.name,
            'role': p.role,
            'uses_custom_settings': uses_custom,
            'static_effective_settings': static_obj,
            'settings_fingerprint': fp,
            'settings_fingerprint_format': STATIC_SETTINGS_FINGERPRINT_FORMAT,
            'cross_run_identity_policy': 'stable_name_plus_static_effective_settings',
            'cross_run_identity_key': key,
        }
    return {
        'temporary': False,
        'id_in_recording': p.id,
        'stable_name': p.name,
        'role': p.role,
        'cross_run_identity_policy': 'durable_core_profile_identity' if rec.schema_version >= 2 else 'historical_schema1_stable_identity',
        'cross_run_identity_key': (f'core:{p.id}:{p.name}' if rec.schema_version >= 2 else f'schema1:{p.name}'),
        'settings_fingerprint': None,
    }


def reserved_profile_id_observations(rec: ParsedRecording) -> list[dict[str, Any]]:
    if rec.schema_version < 2:
        return []
    out = []
    seen = set()
    for f in rec.frames:
        p = frame_profile(f, rec.registry)
        if p.id != RESERVED_PROFILE_ID:
            continue
        key = (p.id, p.name)
        if key in seen:
            continue
        seen.add(key)
        out.append({'id_in_recording': p.id, 'stable_name': p.name, 'warning': 'profile ID 255 is reserved/invalid'})
    return out


def temporary_profile_identities(rec: ParsedRecording) -> list[dict[str, Any]]:
    found: dict[tuple[int, str, str], dict[str, Any]] = {}
    for f in rec.frames:
        ident = profile_condition_identity_for_frame(f, rec)
        if not ident['temporary']:
            continue
        key = (ident['id_in_recording'], ident['stable_name'], ident['settings_fingerprint'])
        found.setdefault(key, ident)
    return [found[k] for k in sorted(found, key=lambda x: (x[0], x[1], x[2]))]


def temporary_name_collisions(identities: Iterable[dict[str, Any]]) -> list[dict[str, Any]]:
    by_name: dict[str, dict[str, list[dict[str, Any]]]] = collections.defaultdict(lambda: collections.defaultdict(list))
    for ident in identities:
        if not ident.get('temporary'):
            continue
        by_name[str(ident.get('stable_name'))][str(ident.get('settings_fingerprint'))].append(ident)
    out = []
    for name in sorted(by_name):
        fps = by_name[name]
        if len(fps) <= 1:
            continue
        out.append({
            'stable_name': name,
            'fingerprints': sorted(fps),
            'conditions': [
                {
                    'settings_fingerprint': fp,
                    'cross_run_identity_key': rows[0].get('cross_run_identity_key'),
                    'observations': len(rows),
                }
                for fp, rows in sorted(fps.items())
            ],
        })
    return out


# Save the prior implementation and wrap it rather than redesigning other analysis.
_analyse_recording_v51 = analyse_recording
_markdown_report_v51 = markdown_report
_combined_analysis_v51 = combined_analysis
_combined_markdown_v51 = combined_markdown
_self_test_v4_v51 = self_test_v4


def profile_segments(rec: ParsedRecording, times: list[float | None]) -> list[dict[str, Any]]:
    """Recording-local profile intervals, split if a temporary condition fingerprint changes."""
    if not rec.frames:
        return []
    fs = rec.frames
    identities = [profile_condition_identity_for_frame(f, rec) for f in fs]
    out = []
    start = 0

    def boundary(a: int, b: int) -> bool:
        fa, fb = fs[a], fs[b]
        if fa.get('test_profile_id') != fb.get('test_profile_id') or fa.get('test_profile_name') != fb.get('test_profile_name'):
            return True
        ia, ib = identities[a], identities[b]
        if ia['temporary'] or ib['temporary']:
            return ia.get('cross_run_identity_key') != ib.get('cross_run_identity_key')
        return False

    for i in range(1, len(fs) + 1):
        if i == len(fs) or boundary(start, i):
            p = frame_profile(fs[start], rec.registry)
            ident = identities[start]
            out.append({
                'profile_id': p.id,
                'profile': p.name,
                'profile_role': p.role,
                'temporary': ident['temporary'],
                'settings_fingerprint': ident.get('settings_fingerprint'),
                'condition_identity': ident.get('cross_run_identity_key'),
                'start_line': fs[start]['_line'],
                'end_line': fs[i - 1]['_line'],
                'start_serial': fs[start].get('prepared_serial'),
                'end_serial': fs[i - 1].get('prepared_serial'),
                'start_time_s': times[start],
                'end_time_s': times[i - 1],
                'frames': i - start,
            })
            start = i
    return out


def analyse_recording(rec: ParsedRecording, args: argparse.Namespace, annotations: dict[str, Any] | None=None) -> dict[str, Any]:
    r = _analyse_recording_v51(rec, args, annotations)
    mapping_fp = profile_mapping_fingerprint(rec.session_start, rec.schema_version)
    r['analysis_metadata']['profile_mapping_fingerprint'] = mapping_fp
    r['analysis_metadata']['profile_mapping_fingerprint_format'] = PROFILE_MAPPING_FINGERPRINT_FORMAT if mapping_fp else None
    r['analysis_metadata']['dynamic_effective_fields_excluded_from_static_fingerprint'] = sorted(DYNAMIC_EFFECTIVE_FIELDS)
    r['analysis_metadata']['static_settings_fingerprint_format'] = STATIC_SETTINGS_FINGERPRINT_FORMAT

    # Rebuild only profile summary grouping so temporary conditions with changed static
    # settings cannot be silently merged even inside one recording.
    grouped: dict[str, tuple[ProfileStats, dict[str, Any]]] = {}
    for f in rec.frames:
        p = frame_profile(f, rec.registry)
        ident = profile_condition_identity_for_frame(f, rec)
        key = ident['cross_run_identity_key']
        if key not in grouped:
            grouped[key] = (ProfileStats(p), ident)
        grouped[key][0].add(f)
    summaries = []
    for key, (ps, ident) in sorted(grouped.items(), key=lambda kv: (kv[1][0].profile.id, kv[1][0].profile.name, kv[0])):
        row = ps.result()
        row.update({
            'temporary': ident['temporary'],
            'settings_fingerprint': ident.get('settings_fingerprint'),
            'cross_run_identity_policy': ident.get('cross_run_identity_policy'),
            'cross_run_identity_key': ident.get('cross_run_identity_key'),
        })
        if ident['temporary']:
            row['static_effective_settings'] = ident.get('static_effective_settings')
        summaries.append(row)
    r['profile_summaries'] = summaries
    r['profile_segments'] = profile_segments(rec, frame_times(rec))
    r['temporary_profile_identities'] = temporary_profile_identities(rec)
    r['temporary_profile_name_collisions'] = temporary_name_collisions(r['temporary_profile_identities'])
    r['temporary_profiles_detected'] = bool(r['temporary_profile_identities'])
    r['reserved_profile_id_observations'] = reserved_profile_id_observations(rec)
    return r


def markdown_report(r: dict[str, Any]) -> str:
    base = _markdown_report_v51(r)
    md = r['analysis_metadata']
    temps = r.get('temporary_profile_identities', [])
    collisions = r.get('temporary_profile_name_collisions', [])
    pre = [
        '# V5.2 profile provenance', '',
        f"- Profile mapping fingerprint: `{md.get('profile_mapping_fingerprint') or 'N/A (schema 1)'}`",
        f"- Mapping fingerprint semantics: ordered schema-2 `session_start.test_profile_enum_mapping`; **not** a complete experiment-pack settings hash.",
        f"- Static settings fingerprint: `{md.get('static_settings_fingerprint_format')}`",
        f"- Dynamic effective fields excluded: `{json.dumps(md.get('dynamic_effective_fields_excluded_from_static_fingerprint', []))}`",
        ''
    ]
    reserved = r.get('reserved_profile_id_observations', [])
    if reserved:
        pre += ['> **RESERVED PROFILE ID 255 OBSERVED**', '> ID 255 is invalid/reserved and is not treated as either a core or temporary experiment identity.', '']
    if temps:
        pre += [
            '> **TEMPORARY EXPERIMENT PROFILES DETECTED**', '>',
            '> Temporary numeric IDs (128–254) are **recording-local transport identity only**.',
            '> Dirty Git build IDs do **not** uniquely identify the experiment pack.',
            '> Cross-run temporary-profile identity is **stable name + static effective settings**.', '',
            '## Temporary profile identities observed in this recording', '',
            '| Local ID | Stable name | Role | Settings fingerprint | Cross-run condition key |',
            '|---:|---|---|---|---|'
        ]
        for x in temps:
            pre.append(f"| {x['id_in_recording']} | `{x['stable_name']}` | `{x.get('role') or '-'}` | `{x['settings_fingerprint']}` | `{x['cross_run_identity_key']}` |")
        pre.append('')
        if collisions:
            pre += ['> **TEMPORARY PROFILE NAME COLLISION IN THIS RECORDING**', '> The same temporary stable name was observed with multiple static-settings fingerprints. These are different experimental conditions.', '']
            for c in collisions:
                pre.append(f"- `{c['stable_name']}`: " + ', '.join(f"`{fp}`" for fp in c['fingerprints']))
            pre.append('')
    pre += ['## Provenance-aware profile intervals', '', '| Lines | Serials | Time | Local ID | Stable name | Temporary | Settings fingerprint | Condition identity |', '|---|---|---|---:|---|---|---|---|']
    for seg in r.get('profile_segments', []):
        fp = seg.get('settings_fingerprint') or '-'
        pre.append(f"| {seg['start_line']}–{seg['end_line']} | {seg['start_serial']}–{seg['end_serial']} | {fmt(seg['start_time_s'])}–{fmt(seg['end_time_s'])} | {seg['profile_id']} | `{seg['profile']}` | {seg.get('temporary', False)} | `{fp}` | `{seg.get('condition_identity') or '-'}` |")
    return '\n'.join(pre) + '\n\n---\n\n' + base


def combined_analysis(items: list[tuple[ParsedRecording, dict[str, Any]]]) -> dict[str, Any]:
    env_groups = collections.defaultdict(list)
    runs = []
    condition_groups: dict[str, dict[str, Any]] = {}
    all_temp_identities = []
    any_temp = False

    for rec, res in items:
        env_groups[(rec.schema_version, rec.build_commit)].append(rec.path.name)
        mapping_fp = res.get('analysis_metadata', {}).get('profile_mapping_fingerprint')
        temps = res.get('temporary_profile_identities', [])
        any_temp = any_temp or bool(temps)
        for t in temps:
            all_temp_identities.append({'file': rec.path.name,
                'sha256': rec.source_sha256, **t})
        runs.append({
            'file': rec.path.name,
            'schema': rec.schema_version,
            'build': rec.build_commit,
            'sha256': rec.source_sha256,
            'profile_mapping_fingerprint': mapping_fp,
            'temporary_profiles_detected': bool(temps),
        })

        for p in res.get('profile_summaries', []):
            key = p.get('cross_run_identity_key') or (f"core:{p.get('profile_id')}:{p.get('profile')}")
            g = condition_groups.setdefault(key, {
                'cross_run_identity_key': key,
                'temporary': bool(p.get('temporary')),
                'stable_name': p.get('profile'),
                'role': p.get('profile_role'),
                'settings_fingerprint': p.get('settings_fingerprint'),
                'static_effective_settings': p.get('static_effective_settings') if p.get('temporary') else None,
                'observations': [],
            })
            g['observations'].append({
                'file': rec.path.name,
                'sha256': rec.source_sha256,
                'schema': rec.schema_version,
                'build': rec.build_commit,
                'profile_mapping_fingerprint': mapping_fp,
                'id_in_recording': p.get('profile_id'),
                **p,
            })

    groups_out = []
    for key in sorted(condition_groups):
        g = condition_groups[key]
        compat = {(o['schema'], o['build']) for o in g['observations']}
        g['aggregate_compatible'] = len(compat) == 1
        g['pooling_note'] = ('same experimental condition and compatible schema/build' if g['aggregate_compatible'] else 'same identity may be recognized, but per-run metrics are not silently averaged across schema/build differences')
        groups_out.append(g)

    collisions = []
    by_name = collections.defaultdict(lambda: collections.defaultdict(list))
    for x in all_temp_identities:
        by_name[x['stable_name']][x['settings_fingerprint']].append(x)
    for name in sorted(by_name):
        fps = by_name[name]
        if len(fps) > 1:
            collisions.append({
                'stable_name': name,
                'conditions': [
                    {'settings_fingerprint': fp, 'files': sorted({r['file'] for r in rows}), 'local_ids': sorted({r['id_in_recording'] for r in rows})}
                    for fp, rows in sorted(fps.items())
                ],
                'warning': 'same temporary stable name observed with different static effective settings; conditions were not pooled',
            })

    env_compatible = len(env_groups) == 1
    return {
        'analysis_metadata': {
            'analyser_version': ANALYSER_VERSION,
            'analyser_sha256': analyser_sha256(),
            'temporary_profile_identity_policy': 'stable_name_plus_static_effective_settings',
            'static_settings_fingerprint_format': STATIC_SETTINGS_FINGERPRINT_FORMAT,
            'profile_mapping_fingerprint_format': PROFILE_MAPPING_FINGERPRINT_FORMAT,
            'dynamic_effective_fields_excluded_from_static_fingerprint': sorted(DYNAMIC_EFFECTIVE_FIELDS),
        },
        'runs': runs,
        'compatibility_groups': [
            {'schema': k[0], 'build': k[1], 'files': sorted(v)} for k, v in sorted(env_groups.items())
        ],
        'run_environment_compatible': env_compatible,
        # Whole-run global pooling is intentionally disabled when temporary packs are present.
        'compatible_for_global_pooling': env_compatible and not any_temp,
        'temporary_profiles_detected': any_temp,
        'profile_condition_groups': groups_out,
        'temporary_profile_name_collisions': collisions,
    }


def combined_markdown(c: dict[str, Any]) -> str:
    out = [
        '# MCCVR Telemetry Combined Comparison v5.2', '',
        f"Analyser `{c['analysis_metadata']['analyser_version']}` / `{c['analysis_metadata']['analyser_sha256']}`", '',
        f"Run environment compatible by schema/build: **{c['run_environment_compatible']}**", 
        f"Whole-run global pooling allowed: **{c['compatible_for_global_pooling']}**", '',
        '## Run provenance', '',
        '| File | Schema | Build | Profile mapping fingerprint | Temporary profiles |',
        '|---|---:|---|---|---|'
    ]
    for r in c['runs']:
        out.append(f"| `{r['file']}` | {r['schema']} | `{r['build']}` | `{r.get('profile_mapping_fingerprint') or 'N/A'}` | {r['temporary_profiles_detected']} |")
    if c.get('temporary_profiles_detected'):
        out += ['', '> **TEMPORARY EXPERIMENT PROFILES DETECTED**', '>', '> Temporary numeric IDs are recording-local. Dirty Git build IDs do not uniquely identify experiment packs.', '> Temporary conditions are grouped only by **stable name + static effective settings fingerprint**.', '> Whole-run global pooling is disabled; condition-level observations remain available below.', '']
    if c.get('temporary_profile_name_collisions'):
        out += ['> **TEMPORARY PROFILE NAME COLLISION**', '> The same temporary stable name appears with different static-settings fingerprints. These are different experimental conditions and were **not pooled**.', '']
        for col in c['temporary_profile_name_collisions']:
            out.append(f"### `{col['stable_name']}`")
            for cond in col['conditions']:
                out.append(f"- `{cond['settings_fingerprint']}` — files: {', '.join(cond['files'])}; local IDs: {cond['local_ids']}")
            out.append('')
    out += ['## Condition comparison', '', '| Condition | Temp | Fingerprint | Run | Local ID | Build | Frames | Two-hand | B accepted | W natural | Horizontal reach | Offhand span |', '|---|---|---|---|---:|---|---:|---:|---:|---|---|---|']
    for g in c['profile_condition_groups']:
        fp = g.get('settings_fingerprint') or '-'
        name = g.get('stable_name') or g['cross_run_identity_key']
        for o in g['observations']:
            out.append(f"| `{name}` | {g['temporary']} | `{fp}` | `{o['file']}` | {o.get('id_in_recording')} | `{o['build']}` | {o['frames']} | {fmt_pct(o['two_hand_active_pct'])} | {fmt_pct(o['b_accepted_pct'])} | {fmt_stats(o['w_natural'])} | {fmt_stats(o['horizontal_reach_m'])} | {fmt_stats(o['offhand_i0_i1_span_deg'])} |")
        if len(g['observations']) > 1 and not g['aggregate_compatible']:
            out.append(f"| ↳ `{name}` |  |  | **not averaged** |  |  |  |  |  |  |  | `{g['pooling_note']}` |")
    out += ['', '## Profile mapping fingerprint semantics', '', 'The mapping fingerprint hashes the **ordered schema-2 session profile mapping** as recorded (including the fields present in each mapping row). It identifies the registry/catalog seen by that recording. It is **not** an experiment-pack settings hash because telemetry cannot fingerprint settings for temporary profiles that were never selected.', '']
    return '\n'.join(out) + '\n'


def combined_v5_analysis(items):
    c = combined_analysis(items)
    c['analysis_metadata']['analyser_version'] = ANALYSER_VERSION
    c['analysis_metadata']['analyser_sha256'] = sha256_file(Path(__file__))
    c['analysis_metadata']['semantics'] = {
        'temporary_identity': 'stable name + canonical static effective-settings fingerprint; local numeric ID/build string do not define temporary identity',
        'statistical_pooling': 'condition identity and schema/build compatibility are separate; incompatible builds remain per-run even when temporary condition identity matches',
        'profile_mapping_fingerprint': 'ordered session mapping fingerprint only; not a complete experiment-pack settings fingerprint',
        'subjectivity': 'no profile quality is inferred from ID/name',
    }
    return c


def combined_v5_markdown(c):
    return combined_markdown(c)


def _fixture_temp_recording(root: Path, filename: str, pid: int, name: str, static_value: float, *, build: str='same-dirty', mapping_order: int=0, dynamic_latched: bool=True) -> ParsedRecording:
    mapping = [
        {'id': 0, 'name': 'Custom', 'role': 'custom', 'uses_custom_settings': True},
        {'id': pid, 'name': name, 'role': 'tuning', 'uses_custom_settings': False},
    ]
    if mapping_order:
        mapping = list(reversed(mapping))
    ss = _fixture_session(2, mapping)
    ss['build_commit'] = build
    f = _fixture_frame(1, pid, name)
    f['schema_version'] = 2
    f['effective_settings']['hybrid_seat_full_m'] = static_value
    f['effective_settings']['two_hand_latched'] = dynamic_latched
    path = root / filename
    path.write_text(json.dumps(ss, sort_keys=True) + '\n' + json.dumps(f, sort_keys=True) + '\n', encoding='utf-8')
    return parse_jsonl(path)


def run_v52_provenance_self_test() -> int:
    passed = []
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        a1 = _fixture_temp_recording(root, 'a1.jsonl', 128, 'X', 0.30)
        a2 = _fixture_temp_recording(root, 'a2.jsonl', 128, 'X', 0.30)
        x1 = profile_condition_identity_for_frame(a1.frames[0], a1)
        x2 = profile_condition_identity_for_frame(a2.frames[0], a2)
        assert x1['cross_run_identity_key'] == x2['cross_run_identity_key']
        passed.append('1 same ID/name/settings => same temporary condition')

        a3 = _fixture_temp_recording(root, 'a3.jsonl', 129, 'X', 0.30)
        x3 = profile_condition_identity_for_frame(a3.frames[0], a3)
        assert x1['cross_run_identity_key'] == x3['cross_run_identity_key']
        passed.append('2 different temporary ID same name/settings => same condition')

        b = _fixture_temp_recording(root, 'b.jsonl', 128, 'Y', 0.31)
        y = profile_condition_identity_for_frame(b.frames[0], b)
        assert x1['cross_run_identity_key'] != y['cross_run_identity_key']
        passed.append('3 same local ID different name/settings => different condition')

        c = _fixture_temp_recording(root, 'c.jsonl', 128, 'X', 0.35)
        xc = profile_condition_identity_for_frame(c.frames[0], c)
        cols = temporary_name_collisions([x1, xc])
        assert len(cols) == 1 and len(cols[0]['fingerprints']) == 2
        passed.append('4 same temporary name different settings => collision + separation')

        assert a1.build_commit == c.build_commit and x1['cross_run_identity_key'] != xc['cross_run_identity_key']
        passed.append('5 same dirty build different settings => not pooled')

        dyn = dict(a1.frames[0])
        dyn['effective_settings'] = dict(a1.frames[0]['effective_settings'])
        dyn['effective_settings']['two_hand_latched'] = not dyn['effective_settings']['two_hand_latched']
        dyn['effective_settings']['support_endpoint_used_grip'] = not bool(dyn['effective_settings'].get('support_endpoint_used_grip'))
        _, fp0 = static_effective_settings_fingerprint(a1.frames[0]['effective_settings'])
        _, fp_dyn = static_effective_settings_fingerprint(dyn['effective_settings'])
        assert fp0 == fp_dyn
        passed.append('6 dynamic effective-state changes do not alter fingerprint')

        static = dict(a1.frames[0]['effective_settings'])
        static['hybrid_seat_full_m'] = 0.301
        _, fp_static = static_effective_settings_fingerprint(static)
        assert fp_static != fp0
        passed.append('7 genuine static setting change alters fingerprint')

        m1 = profile_mapping_fingerprint(a1.session_start, 2)
        m2 = profile_mapping_fingerprint(a2.session_start, 2)
        assert m1 == m2
        passed.append('8 profile-mapping fingerprint deterministic')

        reord = _fixture_temp_recording(root, 'reordered.jsonl', 128, 'X', 0.30, mapping_order=1)
        mr = profile_mapping_fingerprint(reord.session_start, 2)
        assert mr != m1 and reord.registry.get(128, 'X').name == 'X'
        passed.append('9 reordered mapping changes fingerprint but remains valid')

        core_path = root / 'core.jsonl'
        core_ss = _fixture_session(2, [{'id': 0, 'name': 'Custom', 'role': 'custom'}])
        core_f = _fixture_frame(1, 0, 'Custom'); core_f['schema_version'] = 2
        core_path.write_text(json.dumps(core_ss)+'\n'+json.dumps(core_f)+'\n', encoding='utf-8')
        core_rec = parse_jsonl(core_path)
        ci = profile_condition_identity_for_frame(core_rec.frames[0], core_rec)
        assert not ci['temporary'] and not temporary_profile_identities(core_rec)
        test_args = argparse.Namespace(
            grip_held_threshold=0.5, top_jumps=25, stationary_hand_mm=5.0,
            stationary_hand_rotation_deg=1.0, head_yaw_max_deg=10.0,
            head_episode_step_min_deg=0.05, head_episode_min_excursion_deg=2.0,
            head_episode_min_frames=4,
            horizontal_candidates='O_270_425:0.270:0.425,P_300_450:0.300:0.450,Q_320_460:0.320:0.460',
        )
        old_core = _analyse_recording_v51(core_rec, test_args, None)
        new_core = analyse_recording(core_rec, test_args, None)
        # Existing analytical families must remain byte-equivalent in structure/value;
        # V5.2 may add provenance metadata/identity fields only.
        for key in ('health','profile_registry','setting_changes','grip_latch','head_yaw_episodes','horizontal_release','aim_jumps','annotations','semantic_notes'):
            assert old_core.get(key) == new_core.get(key), key
        old_sum = old_core['profile_summaries'][0]
        new_sum = dict(new_core['profile_summaries'][0])
        for k in ('temporary','settings_fingerprint','cross_run_identity_policy','cross_run_identity_key','static_effective_settings'):
            new_sum.pop(k, None)
        assert old_sum == new_sum
        passed.append('10 core-only recording preserves pre-existing analysis')

        s1_path = root / 'schema1-historical.jsonl'
        s1_path.write_text(json.dumps(_fixture_session(1, [{'id':1,'name':'A_VsOffControl'}]))+'\n'+json.dumps(_fixture_frame(1,1,'A_VsOffControl'))+'\n', encoding='utf-8')
        s1 = parse_jsonl(s1_path)
        assert s1.schema_version == 1 and profile_mapping_fingerprint(s1.session_start, 1) is None
        passed.append('11 schema-1 historical fixture accepted')

        multi_path = root / 'multi.jsonl'
        ss = _fixture_session(2, [
            {'id':128,'name':'X','role':'tuning','uses_custom_settings':False},
            {'id':129,'name':'Y','role':'tuning','uses_custom_settings':False},
        ])
        f1=_fixture_frame(1,128,'X'); f1['schema_version']=2; f1['effective_settings']['hybrid_seat_full_m']=0.30
        f2=_fixture_frame(2,129,'Y'); f2['schema_version']=2; f2['effective_settings']['hybrid_seat_full_m']=0.31
        f3=_fixture_frame(3,128,'X'); f3['schema_version']=2; f3['effective_settings']['hybrid_seat_full_m']=0.30
        multi_path.write_text('\n'.join(json.dumps(x) for x in (ss,f1,f2,f3))+'\n',encoding='utf-8')
        mrrec=parse_jsonl(multi_path)
        segs=profile_segments(mrrec, frame_times(mrrec))
        assert len(segs)==3 and segs[0]['condition_identity']==segs[2]['condition_identity'] and segs[0]['profile']=='X' and segs[2]['profile']=='X'
        passed.append('12 repeated temporary condition represented as separate intervals with same identity')

    print(f'MCCVR analyser v5.2 provenance self-test: {len(passed)} checks passed')
    for x in passed:
        print('  PASS', x)
    return 0


def run_persistent_grip_lab_self_test() -> int:
    passed: list[str] = []
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        mapping = [{'id': 0, 'name': 'Custom', 'role': 'custom',
            'uses_custom_settings': True}]

        def pg_frame(serial: int, **fields: Any) -> dict[str, Any]:
            frame = _fixture_frame(serial, 0, 'Custom')
            frame['schema_version'] = 2
            frame.update({'persistent_support_grip_configured': True,
                'persistent_support_grip_applicable': True,
                'support_relationship_readable': True,
                'support_relationship_engaged': False,
                'support_epoch': 0, 'support_solve_trusted': False,
                'support_force_one_hand': False,
                'support_solve_serial': serial - 1})
            frame.update(fields)
            return frame

        def write_recording(path: Path,
            frames: list[dict[str, Any]]) -> ParsedRecording:
            rows: list[Any] = [_fixture_session(2, mapping)] + frames + [{
                'type': 'session_end', 'schema_version': 2,
                'clean_stop': True, 'producer_calls': len(frames),
                'duplicate_serial_suppressed': 0, 'enqueued': len(frames),
                'written': len(frames), 'dropped_queue_full': 0}]
            path.write_text('\n'.join(
                json.dumps(row, sort_keys=True) for row in rows) + '\n',
                encoding='utf-8')
            return parse_jsonl(path)

        lab_enabled = {'two_hand_lab_enabled': True,
            'two_hand_lab_anchor_fallback': 0}
        f1 = pg_frame(1)
        f2 = pg_frame(2, support_relationship_engaged=True, support_epoch=7,
            support_solve_trusted=True)
        f3 = pg_frame(3, support_relationship_engaged=True, support_epoch=9,
            support_solve_trusted=True)
        f4 = pg_frame(4, support_relationship_engaged=True, support_epoch=9)
        f5 = pg_frame(5, support_relationship_engaged=True, support_epoch=9,
            support_force_one_hand=True)
        f6 = pg_frame(6, support_relationship_engaged=True, support_epoch=9,
            **lab_enabled, two_hand_lab_offhand_influence=0.6,
            two_hand_lab_effective_influence=0.45,
            two_hand_lab_anchor_requested=1, two_hand_lab_anchor_resolved=1,
            two_hand_lab_agreement_mode=1, two_hand_lab_agreement=0.9,
            two_hand_lab_agreement_confidence=0.8,
            two_hand_lab_temporal_mode=1, two_hand_lab_temporal_active=True,
            two_hand_lab_temporal_error_deg=1.5)
        f6['aim_trace'].update({'a_dot_b': 0.2, 'b_accepted': False,
            'b_extreme_rejected': False, 'b_rejected_agreement': 0.2,
            'b_steering_retained': True})
        f7 = pg_frame(7, support_relationship_engaged=True, support_epoch=9,
            **lab_enabled, two_hand_lab_offhand_influence=0.35,
            two_hand_lab_effective_influence=0.35,
            two_hand_lab_anchor_requested=2, two_hand_lab_anchor_resolved=2,
            two_hand_lab_agreement_mode=0, two_hand_lab_agreement=0.5,
            two_hand_lab_agreement_confidence=1.0,
            two_hand_lab_temporal_mode=0, two_hand_lab_temporal_active=False,
            two_hand_lab_temporal_error_deg=0.0)
        f7['aim_trace'].update({'a_dot_b': 0.1, 'b_accepted': False,
            'b_extreme_rejected': False, 'b_rejected_agreement': 0.1,
            'b_steering_retained': False})
        f7['canonical_aim']['two_hand_active'] = False
        f8 = pg_frame(8, support_relationship_engaged=False, support_epoch=0)
        f9 = pg_frame(9, support_relationship_readable=False,
            support_relationship_engaged=False,
            support_epoch=SUPPORT_EPOCH_UNKNOWN,
            support_force_one_hand=True)

        src = root / 'persistent-grip-lab.jsonl'
        rec = write_recording(src,
            [f1, f2, f3, f4, f5, f6, f7, f8, f9])
        args = _fixture_cli_args(str(src))
        p = persistent_grip_lab_analysis(rec, frame_times(rec), args)
        assert p['recorded'] is True and p['lab_recorded'] is True
        passed.append('persistent-grip and Two-Hand Lab families detected')

        c = p['counts']
        assert c['frames_with_family'] == 9 and c['readable_true'] == 8
        assert c['engaged_true'] == 6 and c['trusted_true'] == 2
        assert c['force_one_hand_true'] == 2 and c['configured_true'] == 9
        assert c['epoch_zero_frames'] == 2
        assert c['epoch_unreadable_sentinel_frames'] == 1
        passed.append('family true-counts and epoch sentinel accounting')

        assert [row['kind'] for row in p['relationship_transitions']] == [
            'relationship_acquire', 'relationship_release',
            'relationship_readability_change']
        acquire = p['relationship_transitions'][0]
        assert acquire['line'] == 3
        assert acquire['from']['engaged'] is False
        assert acquire['to']['engaged'] is True
        release = p['relationship_transitions'][1]
        assert release['line'] == 9 and release['to']['engaged'] is False
        passed.append('relationship acquire/release/readability edges')

        epoch_rows = p['epoch_changes']
        assert [row['line'] for row in epoch_rows] == [3, 4, 9, 10]
        assert (epoch_rows[0]['from_epoch'], epoch_rows[0]['to_epoch']) == (0, 7)
        assert (epoch_rows[1]['from_epoch'], epoch_rows[1]['to_epoch']) == (7, 9)
        assert (epoch_rows[2]['from_epoch'], epoch_rows[2]['to_epoch']) == (9, 0)
        assert (epoch_rows[3]['from_epoch'], epoch_rows[3]['to_epoch']) == (
            0, SUPPORT_EPOCH_UNKNOWN)
        assert epoch_rows[3]['to_unreadable_sentinel'] is True
        assert epoch_rows[3]['from_unreadable_sentinel'] is False
        assert all(row['from_unreadable_sentinel'] is False
            for row in epoch_rows[:3])
        passed.append('epoch change and all-ones unreadable sentinel transitions')

        assert len(p['trust_loss_transitions']) == 1
        trust = p['trust_loss_transitions'][0]
        assert trust['line'] == 5 and trust['engaged_after'] is True
        assert trust['epoch_after'] == 9
        passed.append('engaged to untrusted solve transition')

        assert [row['start_line'] for row in p['force_one_hand_runs']] == [6, 10]
        assert p['force_one_hand_runs'][0]['frames'] == 1
        assert p['force_one_hand_runs'][0]['engaged_frames'] == 1
        passed.append('explicit forced one-hand runs')

        assert len(p['agreement_below_floor_runs']) == 1
        run = p['agreement_below_floor_runs'][0]
        assert (run['start_line'], run['end_line'], run['frames']) == (7, 8, 2)
        assert run['a_dot_b']['n'] == 2
        assert abs(run['a_dot_b']['min'] - 0.1) < 1e-12
        assert run['b_accepted_frames'] == 0
        assert run['b_extreme_rejected_frames'] == 0
        assert run['b_steering_retained_frames'] == 1
        assert run['b_rejected_agreement']['n'] == 2
        assert len(p['b_steering_retained_runs']) == 1
        assert p['b_steering_retained_runs'][0]['start_line'] == 7
        passed.append('a_dot_b below floor runs with B context and retained steering')

        assert len(p['two_hand_to_one_hand_while_engaged']) == 1
        two = p['two_hand_to_one_hand_while_engaged'][0]
        assert two['line'] == 8 and two['engaged_after'] is True
        assert two['two_hand_latched_before'] is True
        assert two['two_hand_latched_after'] is True
        passed.append('two-hand to one-hand while engaged; latch stays context')

        lab = p['lab']
        assert lab['steering_frames'] == 2
        assert lab['requested_vs_effective_divergence_frames'] == 1
        assert lab['frames'][0]['line'] == 7
        assert abs(lab['frames'][0]['effective_minus_requested'] + 0.15) < 1e-09
        assert lab['frames'][0]['anchor_requested'] == 1
        assert abs(lab['frames'][0]['agreement'] - 0.9) < 1e-12
        assert lab['frames'][0]['temporal_active'] is True
        assert lab['effective_minus_requested']['n'] == 2
        passed.append('Lab requested versus effective influence around the event')

        lag = p['support_solve_serial_lag']
        assert lag['delta_values'] == {'1': 9}
        assert lag['frames_with_delta_zero'] == 0
        passed.append('prepared_serial minus support_solve_serial lag distribution')

        event_kinds = [row['kind'] for row in p['events']]
        for kind in ('relationship_acquire', 'relationship_release',
                'relationship_readability_change', 'epoch_change', 'trust_loss',
                'two_hand_to_one_hand_while_engaged', 'force_one_hand_begin',
                'force_one_hand_end', 'a_dot_b_below_floor_begin',
                'a_dot_b_below_floor_end', 'b_steering_retained_begin',
                'b_steering_retained_end'):
            assert kind in event_kinds, kind
        lines = [row['line'] for row in p['events']]
        assert lines == sorted(lines)
        below_begin = next(row for row in p['events']
            if row['kind'] == 'a_dot_b_below_floor_begin')
        assert below_begin['lab'] is not None
        assert abs(below_begin['lab']['effective_influence'] - 0.45) < 1e-12
        assert below_begin['pg']['engaged'] is True
        assert below_begin['b_steering_retained'] is True
        passed.append('merged event index carries per-frame PG and Lab context')

        def markdown_section(text: str, heading: str) -> str:
            start = text.index(heading)
            end = text.find('\n## ', start + 1)
            return text[start:end if end != -1 else len(text)]

        markdown = '\n'.join(persistent_grip_lab_markdown(p))
        assert 'never verdicts' in markdown and 'a_dot_b' in markdown
        assert 'support_solve_serial' in markdown
        assert 'not recorded' not in markdown_section(markdown,
            '## Persistent grip (PG) \u00d7 Two-Hand Lab indexed episodes')
        assert p['smoothing_recorded'] is False and p['smoothing'] is None
        assert 'not recorded' in markdown_section(markdown,
            '## Two-Hand transition/input smoothing values')
        assert 'persistent_grip_lab_episodes' in json_text(p)
        passed.append('section markdown caveats and JSON serialization')

        result = analyse_v5(rec, args)
        presets = query_result(result, 'list')
        assert 'pg-lab' in presets and 'pg-lab-events' in presets
        assert query_result(result, 'pg-lab')['recorded'] is True
        assert isinstance(query_result(result, 'pg-lab-events'), list)
        passed.append('query presets expose the structured section and events')

        # Transition/input smoothing: slider-era shapes, boolean-era absence of
        # strength/mix, and a frame that lacks the whole family. The base builder
        # deliberately carries no strength/mix (the boolean-era write shape); the
        # slider frames add them.
        def smoothing_frame(serial: int, **fields: Any) -> dict[str, Any]:
            frame = _fixture_frame(serial, 0, 'Custom')
            frame['schema_version'] = 2
            frame.update({'two_hand_transition_smoothing_configured': True,
                'two_hand_transition_active': True,
                'two_hand_smoothing_configured': True,
                'two_hand_smoothing_applied': True,
                'two_hand_smoothing_alpha': 0.25,
                'two_hand_smoothing_primary_orientation_error_deg': 12.0,
                'two_hand_smoothing_primary_position_error_m': 0.02,
                'two_hand_smoothing_support_position_error_m': 0.03})
            frame.update(fields)
            return frame

        def pg_smoothing_frame(serial: int, engaged: bool, trusted: bool,
                **fields: Any) -> dict[str, Any]:
            return smoothing_frame(serial,
                persistent_support_grip_configured=True,
                persistent_support_grip_applicable=True,
                support_relationship_readable=True,
                support_relationship_engaged=engaged, support_epoch=3,
                support_solve_trusted=trusted, support_force_one_hand=False,
                support_solve_serial=serial - 1, **fields)

        lab_full = {'two_hand_lab_enabled': True}
        s1 = pg_smoothing_frame(1, True, True, **lab_full,
            two_hand_smoothing_strength=12.5, two_hand_smoothing_mix=0.5,
            two_hand_smoothing_alpha=0.25,
            two_hand_smoothing_primary_orientation_error_deg=12.0,
            two_hand_smoothing_primary_position_error_m=0.02,
            two_hand_smoothing_support_position_error_m=0.03,
            two_hand_offhand_influence=0.5,
            two_hand_lab_offhand_influence=0.5,
            two_hand_lab_effective_influence=0.45,
            presented_aim_valid=True, reticle_presented_valid=True,
            reticle_presented_serial=0)
        s2 = pg_smoothing_frame(2, True, True, **lab_full,
            two_hand_smoothing_strength=25.0, two_hand_smoothing_mix=1.0,
            two_hand_smoothing_alpha=0.5,
            two_hand_smoothing_primary_orientation_error_deg=8.0,
            two_hand_smoothing_primary_position_error_m=0.01,
            two_hand_smoothing_support_position_error_m=0.02,
            two_hand_offhand_influence=0.75,
            two_hand_lab_offhand_influence=0.6,
            two_hand_lab_effective_influence=0.6,
            presented_aim_valid=True, reticle_presented_valid=True,
            reticle_presented_serial=2)
        s3 = smoothing_frame(3,
            two_hand_transition_smoothing_configured=False,
            two_hand_transition_active=False,
            two_hand_smoothing_configured=False,
            two_hand_smoothing_applied=False,
            two_hand_smoothing_strength=0.0, two_hand_smoothing_mix=0.0,
            two_hand_smoothing_alpha=1.0,
            two_hand_smoothing_primary_orientation_error_deg=0.0,
            two_hand_smoothing_primary_position_error_m=0.0,
            two_hand_smoothing_support_position_error_m=0.0,
            presented_aim_valid=False, reticle_presented_valid=False,
            reticle_presented_serial=0)
        s3['effective_settings']['two_hand_latched'] = False
        s4 = pg_smoothing_frame(4, True, False,
            two_hand_smoothing_alpha=0.75,
            two_hand_smoothing_primary_orientation_error_deg=5.0,
            two_hand_smoothing_primary_position_error_m=0.005,
            two_hand_smoothing_support_position_error_m=0.001,
            presented_aim_valid=True, reticle_presented_valid=True,
            reticle_presented_serial=3)
        s5 = smoothing_frame(5, two_hand_smoothing_applied=False,
            two_hand_smoothing_alpha=0.1,
            two_hand_smoothing_primary_orientation_error_deg=0.5,
            two_hand_smoothing_primary_position_error_m=0.001,
            two_hand_smoothing_support_position_error_m=0.002,
            presented_aim_valid=False, reticle_presented_valid=False,
            reticle_presented_serial=4)
        s6 = _fixture_frame(6, 0, 'Custom')
        s6['schema_version'] = 2
        s7 = smoothing_frame(7, two_hand_transition_active=False,
            two_hand_smoothing_strength=12.5, two_hand_smoothing_mix=0.5,
            two_hand_smoothing_alpha=0.9,
            two_hand_smoothing_primary_orientation_error_deg=20.0,
            two_hand_smoothing_primary_position_error_m=0.03,
            two_hand_smoothing_support_position_error_m=0.04,
            **lab_full, two_hand_lab_offhand_influence=0.4,
            two_hand_offhand_influence=1.0,
            two_hand_lab_effective_influence=0.3,
            presented_aim_valid=True, reticle_presented_valid=True,
            reticle_presented_serial=5)
        s7['effective_settings']['two_hand_latched'] = False
        smoothing_src = root / 'two-hand-smoothing.jsonl'
        smoothing_rec = write_recording(smoothing_src, [s1, s2, s3, s4, s5, s6, s7])
        smoothing_args = _fixture_cli_args(str(smoothing_src))
        smoothing_result = persistent_grip_lab_analysis(
            smoothing_rec, frame_times(smoothing_rec), smoothing_args)
        assert smoothing_result['smoothing_recorded'] is True
        ss = smoothing_result['smoothing']
        passed.append('smoothing family detected per frame with an absent-family frame')

        c1, c2, c3 = (smoothing_frame_context(s1), smoothing_frame_context(s2),
            smoothing_frame_context(s3))
        assert c1['strength'] == 12.5 and abs(c1['mix'] - 0.5) < 1e-12
        assert c2['strength'] == 25.0 and abs(c2['mix'] - 1.0) < 1e-12
        assert c3['strength'] == 0.0 and c3['mix'] == 0.0
        assert c3['configured'] is False and c3['applied'] is False
        assert smoothing_frame_context(s6) is None
        assert smoothing_frame_context(s5)['strength'] is None
        passed.append('slider shapes 12.5/0.5, 25.0/1.0, 0.0/0.0 and absent strength')

        # Free two-hand offhand authority rides beside the family and is
        # presence-tolerant: s1/s2/s7 record it, s3/s4/s5 predate it.
        assert c1['offhand_influence'] == 0.5
        assert c2['offhand_influence'] == 0.75
        assert c3['offhand_influence'] is None
        assert ss['offhand_influence_recorded_frames'] == 3
        assert ss['offhand_influence']['n'] == 3
        assert ss['offhand_influence']['min'] == 0.5
        assert ss['offhand_influence']['p50'] == 0.75
        assert ss['offhand_influence']['max'] == 1.0
        passed.append('offhand influence recorded with an absent-key frame')

        assert ss['frames_with_family'] == 6 and ss['frames_total'] == 7
        assert (ss['transition_configured_true_frames'],
            ss['transition_active_true_frames']) == (5, 4)
        assert (ss['configured_true_frames'], ss['applied_true_frames']) == (5, 4)
        assert ss['strength_mix_recorded'] is True
        assert (ss['strength_recorded_frames'], ss['mix_recorded_frames']) == (4, 4)
        applied = ss['applied']
        assert applied['frames'] == 4
        assert applied['strength']['n'] == 3
        assert applied['strength']['min'] == 12.5
        assert applied['strength']['p50'] == 12.5
        assert applied['strength']['max'] == 25.0
        assert abs(applied['strength']['mean'] - 50.0 / 3.0) < 1e-09
        assert applied['mix']['n'] == 3
        assert abs(applied['mix']['min'] - 0.5) < 1e-12
        assert abs(applied['mix']['max'] - 1.0) < 1e-12
        assert applied['alpha']['n'] == 4
        assert abs(applied['alpha']['min'] - 0.25) < 1e-12
        assert abs(applied['alpha']['p50'] - 0.625) < 1e-12
        assert abs(applied['alpha']['max'] - 0.9) < 1e-12
        assert applied['primary_orientation_error_deg']['n'] == 4
        assert applied['primary_orientation_error_deg']['max'] == 20.0
        assert applied['primary_position_error_m']['min'] == 0.005
        assert applied['primary_position_error_m']['max'] == 0.03
        assert applied['support_position_error_m']['min'] == 0.001
        assert applied['support_position_error_m']['max'] == 0.04
        passed.append('applied-frame strength/mix/alpha/error distributions')

        assert applied['alpha']['max'] != applied['mix']['max']
        assert applied['alpha']['max'] <= 1.0
        assert applied['alpha']['p50'] != applied['strength']['p50']
        latch = ss['latch_coincidence']
        assert latch['frames_compared'] == 6
        assert latch['pairs'] == {
            'transition_active_true_latched_true': 4,
            'transition_active_true_latched_false': 0,
            'transition_active_false_latched_true': 0,
            'transition_active_false_latched_false': 2}
        assert latch['frames_with_either_absent'] == 0
        passed.append('alpha never conflated with mix or strength; latch pair counts')

        pgc = ss['pg_context_on_applied']
        assert pgc['pg_family_frames'] == 3
        assert pgc['engaged'] == {'true': 3, 'false': 0, 'absent': 1}
        assert pgc['trusted'] == {'true': 2, 'false': 1, 'absent': 1}
        labc = ss['lab_context_on_applied']
        assert labc['lab_family_frames'] == 3 and labc['lab_enabled_true'] == 3
        assert labc['effective_influence']['n'] == 3
        assert abs(labc['effective_influence']['min'] - 0.3) < 1e-12
        assert abs(labc['effective_influence']['max'] - 0.6) < 1e-12
        assert abs(labc['offhand_influence_requested']['min'] - 0.4) < 1e-12
        presc = ss['presented_context_on_applied']
        assert presc['recorded'] is True
        assert presc['presented_aim_valid'] == {'true': 4, 'false': 0, 'absent': 0}
        assert presc['reticle_presented_valid']['true'] == 4
        delta = presc['prepared_minus_reticle_presented_serial']
        assert delta['frames_compared'] == 4 and delta['delta_values'] == {'0': 1, '1': 2, '2': 1}
        assert delta['delta_zero_frames'] == 1
        assert delta['delta_positive_frames'] == 3
        assert delta['delta_negative_frames'] == 0
        passed.append('PG, Lab and presented/reticle context on applied frames')

        smoothing_md = '\n'.join(persistent_grip_lab_markdown(smoothing_result))
        s_sect = markdown_section(smoothing_md,
            '## Two-Hand transition/input smoothing values')
        assert 'not recorded' not in s_sect
        assert 'Smoothing-applied frames: **4**' in s_sect
        assert '12.500' in s_sect and '25.000' in s_sect
        assert 'two_hand_smoothing_alpha' in s_sect
        assert 'two_hand_offhand_influence' in s_sect and '0.500' in s_sect
        assert json_text(smoothing_result)
        passed.append('smoothing section markdown for slider-era recordings')

        boolean_frames = [smoothing_frame(1, two_hand_smoothing_alpha=0.2),
            smoothing_frame(2, two_hand_smoothing_alpha=0.8)]
        boolean_src = root / 'boolean-era-smoothing.jsonl'
        boolean_rec = write_recording(boolean_src, boolean_frames)
        boolean_args = _fixture_cli_args(str(boolean_src))
        boolean = persistent_grip_lab_analysis(
            boolean_rec, frame_times(boolean_rec), boolean_args)
        assert boolean['smoothing_recorded'] is True
        bs = boolean['smoothing']
        assert bs['frames_with_family'] == 2 and bs['applied_true_frames'] == 2
        assert bs['strength_mix_recorded'] is False
        assert (bs['strength_recorded_frames'], bs['mix_recorded_frames']) == (0, 0)
        assert bs['offhand_influence_recorded_frames'] == 0
        assert bs['offhand_influence'] is None
        assert bs['applied']['strength'] is None and bs['applied']['mix'] is None
        assert bs['applied']['alpha']['n'] == 2
        assert abs(bs['applied']['alpha']['max'] - 0.8) < 1e-12
        assert bs['presented_context_on_applied']['recorded'] is False
        assert bs['presented_context_on_applied']['presented_aim_valid'] == {
            'true': 0, 'false': 0, 'absent': 2}
        boolean_md = '\n'.join(persistent_grip_lab_markdown(boolean))
        b_sect = markdown_section(boolean_md,
            '## Two-Hand transition/input smoothing values')
        assert 'not recorded' in b_sect and 'boolean-era' in b_sect
        assert 'Smoothing-applied frames: **2**' in b_sect
        assert 'alpha' in b_sect
        passed.append('boolean-era family stays analysable with strength not recorded')

        legacy_frames = [_fixture_frame(1, 0, 'Custom'),
            _fixture_frame(2, 0, 'Custom')]
        for frame in legacy_frames:
            frame['schema_version'] = 2
        legacy_src = root / 'legacy-without-family.jsonl'
        legacy_rec = write_recording(legacy_src, legacy_frames)
        legacy_args = _fixture_cli_args(str(legacy_src))
        legacy = persistent_grip_lab_analysis(
            legacy_rec, frame_times(legacy_rec), legacy_args)
        assert legacy['recorded'] is False and legacy['lab_recorded'] is False
        assert legacy['smoothing_recorded'] is False and legacy['smoothing'] is None
        assert legacy['counts'] is None and legacy['lab'] is None
        assert legacy['support_solve_serial_lag'] is None
        assert legacy['events'] == [] and legacy['relationship_transitions'] == []
        assert legacy['epoch_changes'] == []
        assert legacy['trust_loss_transitions'] == []
        legacy_md = '\n'.join(persistent_grip_lab_markdown(legacy))
        assert 'not recorded' in legacy_md
        legacy_sect = markdown_section(legacy_md,
            '## Two-Hand transition/input smoothing values')
        assert 'not recorded' in legacy_sect
        assert 'two_hand_transition_smoothing_configured' in legacy_sect
        legacy_result = analyse_v5(legacy_rec, legacy_args)
        assert query_result(legacy_result, 'pg-lab')['recorded'] is False
        passed.append('old-style recording without the family stays not recorded')

    print(f'MCCVR analyser persistent-grip/lab self-test: {len(passed)} checks passed')
    for x in passed:
        print('  PASS', x)
    return 0


def self_test_v4() -> int:
    rc = _self_test_v4_v51()
    if rc:
        return rc
    rc = run_v52_provenance_self_test()
    if rc:
        return rc
    rc = run_persistent_grip_lab_self_test()
    if rc:
        return rc
    return run_sidecar_self_test()

def analyse_v5(rec, args, annotations=None):
    r = analyse_v4(rec, args, annotations)
    times = frame_times(rec)
    r['analysis_metadata']['analyser_version'] = ANALYSER_VERSION
    r['analysis_metadata']['analyser_sha256'] = sha256_file(Path(__file__))
    r['v5'] = {'authority_decomposition': authority_decomposition(rec), 'transition_timeline': transition_timeline(rec, times), 'performance_index': performance_index(rec, times, args), 'persistent_grip_lab': persistent_grip_lab_analysis(rec, times, args)}
    return r

def query_result(r, query):
    v4r = r['v4']
    v5 = r['v5']
    mapping = {'profile-provenance': {'profile_mapping_fingerprint': r['analysis_metadata'].get('profile_mapping_fingerprint'), 'temporary_profile_identities': r.get('temporary_profile_identities', []), 'temporary_profile_name_collisions': r.get('temporary_profile_name_collisions', [])}, 'profile-changes': r['profile_segments'], 'settings': v4r['setting_sweeps'], 'grip-held-aim-inactive': v4r['state_pose_mismatches']['aim_inactive_while_grip_latch_held'], 'latch-release-while-grip-held': v4r['state_pose_mismatches']['grip_latch']['latch_released_while_grip_held'], 'b-boundary': v4r['b_boundary']['events'], 'b-chatter': v4r['b_boundary']['chatter_episodes'], 'w-transitions': v4r['w_transitions'], 'head-yaw': r['head_yaw_episodes'], 'horizontal-reach': v4r['horizontal_reach_episodes'], 'low-held-retention': v4r['low_held_retention'], 'steady-jumps': r['aim_jumps']['steady_state_top'], 'all-jumps': r['aim_jumps']['all_top'], 'singularity': v4r['geometry_indexes'], 'control-oracle': v4r['control_oracles'], 'authority': v5['authority_decomposition'], 'transitions': v5['transition_timeline'], 'performance': v5['performance_index'], 'pg-lab': v5['persistent_grip_lab'], 'pg-lab-events': v5['persistent_grip_lab']['events'], 'events': v4r['other_record_inventory']}
    if query == 'list':
        return sorted(mapping)
    if query not in mapping:
        raise ValueError(f'Unknown query {query!r}; use --query list')
    return mapping[query]

def md_v5(r, args):
    out = [md_v4(r, args), '', '---', '', '# V5.2 authority / query additions', '', '## Neutral authority-distance decomposition', '', 'No control is labelled desirable by the analyser. Solver candidate distances use the **pre-calibration** traced final direction; same-frame control distances use the **post-calibration** canonical output. This prevents gun yaw/pitch/roll calibration from appearing as false solver-authority distance.', '', '| Profile | Solver→A | Solver→B | Solver→Hip | Solver→C | Canonical→VS-OFF | Canonical→Fixed Head | Canonical→Fixed Shoulder |', '|---|---|---|---|---|---|---|---|']
    for p, d in r['v5']['authority_decomposition'].items():
        a = d['solver_precal_angles_deg']
        c = d['control_postcal_angles_deg']
        out.append(f"| `{p}` | {fmt_stats(a.get('A'))} | {fmt_stats(a.get('B'))} | {fmt_stats(a.get('Hip'))} | {fmt_stats(a.get('C'))} | {fmt_stats(c.get('CF_VS_OFF'))} | {fmt_stats(c.get('CF_FIXED_HEAD'))} | {fmt_stats(c.get('CF_FIXED_SHOULDER'))} |")
    perf = r['v5']['performance_index']
    out += ['', '## Recorder/per-frame performance index', '', f"Hot path: {fmt_stats(perf['hotpath_us'], 2)} µs", '', f"Prepared-frame interval / predicted period: {fmt_stats(perf['frame_interval_ratio_to_predicted_period'], 3)}", '', '| Line | Time | Profile | Hot-path µs |', '|---:|---:|---|---:|']
    for x in perf['top_hotpath_frames']:
        out.append(f"| {x['line']} | {fmt(x['time_s'])} | `{x['profile']}` | {x['hotpath_us']:.2f} |")
    out += ['', '## General transition timeline summary', '', f"Indexed semantic/validity/state transitions: **{len(r['v5']['transition_timeline'])}**.", '', 'Use `--query transitions` for the deterministic line/serial/time index.', '']
    out += persistent_grip_lab_markdown(r['v5']['persistent_grip_lab'])
    out += ['', '## Query presets', '', '`profile-provenance`, `profile-changes`, `settings`, `grip-held-aim-inactive`, `latch-release-while-grip-held`, `b-boundary`, `b-chatter`, `w-transitions`, `head-yaw`, `horizontal-reach`, `low-held-retention`, `steady-jumps`, `all-jumps`, `singularity`, `control-oracle`, `authority`, `transitions`, `performance`, `pg-lab`, `pg-lab-events`, `events`.', '']
    return '\n'.join(out)

def add_args(ap):
    add_core_args(ap)
    ap.add_argument('--motion-min-mm', type=float, default=2.0)
    ap.add_argument('--motion-stationary-mm', type=float, default=2.0)
    ap.add_argument('--motion-stationary-rot-deg', type=float, default=0.5)
    ap.add_argument('--rotation-min-deg', type=float, default=1.0)
    ap.add_argument('--chatter-gap-s', type=float, default=0.75)
    ap.add_argument('--singularity-cross-warn', type=float, default=0.02)
    ap.add_argument('--reach-episode-step-min-m', type=float, default=0.001)
    ap.add_argument('--reach-episode-min-delta-m', type=float, default=0.05)
    ap.add_argument('--reach-episode-min-frames', type=int, default=4)
    ap.add_argument('--low-held-below-head-m', type=float, default=0.35)
    ap.add_argument('--setting-sweep-gap-s', type=float, default=1.0)
    ap.add_argument('--a-dot-b-agreement-floor', type=float,
                    default=DEFAULT_A_DOT_B_AGREEMENT_FLOOR)
    ap.add_argument('--top-performance', type=int, default=20)
    ap.add_argument('--frame-interval-warn-ratio', type=float, default=1.5)
    ap.add_argument('--weapon-order', action='store_true',
                    help='Report first-B-frame weapon-ordering evidence (weapon_event lines) instead of the aim analysis')
    ap.add_argument('--weapon-order-self-test', action='store_true',
                    help='Run deterministic self-tests for the weapon-order transition algorithm')

def combined_v5_analysis(items):
    c = combined_analysis(items)
    c['analysis_metadata']['analyser_version'] = ANALYSER_VERSION
    c['analysis_metadata']['analyser_sha256'] = sha256_file(Path(__file__))
    c['analysis_metadata']['semantics'] = {
        'temporary_identity': 'stable name + canonical static effective-settings fingerprint; local numeric ID and dirty build string do not define temporary identity',
        'statistical_pooling': 'condition identity and schema/build compatibility are separate; incompatible builds remain per-run even when condition identity matches',
        'profile_mapping_fingerprint': 'ordered schema-2 session mapping fingerprint only; not a complete experiment-pack settings fingerprint',
        'subjectivity': 'no profile quality is inferred from ID/name',
    }
    return c

def combined_v5_markdown(c):
    return combined_markdown(c)

# ---------------------------------------------------------------------------
# Neutral sidecar layer (A006): manifest, guide, diagnostics envelope, raw tools
# ---------------------------------------------------------------------------
MANIFEST_SCHEMA_VERSION = 1
DIAGNOSTICS_SCHEMA_VERSION = 1
GENERATOR_NAME = 'analyse_mccvr_telemetry'
TELEMETRY_AI_GUIDE_FILENAME = 'TELEMETRY_AI_GUIDE.md'
GUIDE_REVISION = 4
HASH_BASIS_EXACT_INPUT_FILE_BYTES = 'exact_input_file_bytes'
MAX_STRUCTURAL_RUNS = 1000
_MISSING = object()

TELEMETRY_AI_GUIDE_TEXT = f"""# MCCVR telemetry AI orientation guide

Guide revision: {GUIDE_REVISION}
Describes: raw telemetry schema 2 (schema 1 remains readable), manifest schema {MANIFEST_SCHEMA_VERSION}, diagnostics schema {DIAGNOSTICS_SCHEMA_VERSION}

## What these files are

- `<stem>.jsonl` — raw telemetry. This is the evidentiary source.
- `<stem>.manifest.json` — a mechanically derived map of one raw capture.
- `<stem>.diagnostics.json` / `<stem>.diagnostics.md` — optional derived diagnostics.
- `EXPERIMENT_CONTEXT.md` — optional experiment intent written by a human. It is not
  telemetry and it is not neutral.
- `TELEMETRY_AI_GUIDE.md` — this file. It is capture-independent.

The manifest and diagnostics may both contain generator bugs or omissions. They are
aids, not evidence. Verify material claims from the raw telemetry, including any
manifest or diagnostic fact that becomes material to a conclusion.

Diagnostics may contain calculations, heuristics, thresholds, selected intervals,
rankings, simulations, counterfactuals and experiment-specific assumptions. They may be
used as search aids, hypothesis generators, regression aids or cross-checks, but neither
their presence nor their absence establishes a conclusion. If independent raw-derived
analysis disagrees with a sidecar, report the disagreement rather than resolving it in
favour of the sidecar.

Choose analytical methods appropriate to your question. No file here prescribes an
analysis recipe, a preferred metric, an interesting interval, or a conclusion.

## Raw JSONL mechanics

- One JSON object per line (JSON Lines, UTF-8).
- One `frame` object is one complete recorded frame; frames are never split across
  records.
- `prepared_serial` is the preferred frame identity where present.
- Physical line numbers reported by sidecars are 1-based and count every physical line.
- Session-relative seconds are a derived convenience, not an identity. Each source
  declares its clock, origin and conversion in its `timebases` block.
- Validity flags are authoritative. An invalid payload may still contain a numeric
  value; it must not be interpreted as valid. A payload is not valid merely because it
  is numeric.
- QPC values belong to the recorder process that produced the capture. QPC values from
  different captures or processes do not share an origin unless a sidecar explicitly
  says so.

## Manifest mechanics

The manifest is exhaustive but dumb: it enumerates record categories, event categories,
field paths, authoritative validity pairings and explicit structural runs. It does not
rank, score, recommend or encode a research question. Absence from the manifest means
only that a field path or category was not observed under that identity in the
successfully parsed record set. It is not proof of absence from the raw telemetry or
from reality; an equivalent signal may exist under another path or representation.

## Diagnostics mechanics

Diagnostics are optional, derived and non-authoritative. Method definitions, active
parameters, classifications, raw input paths and raw locators are included so that
derived output can be reproduced or falsified. Per-frame derived row dumps are omitted
by default; pass `--include-debug-derived-rows` to include them.

## Persistent-grip and Two-Hand Lab provenance

Newer captures carry frozen solve-time persistent-grip provenance on each frame
(`persistent_support_grip_configured`, `persistent_support_grip_applicable`,
`support_relationship_readable`, `support_relationship_engaged`, `support_epoch`,
`support_solve_trusted`, `support_force_one_hand`, `support_solve_serial`) plus a
Two-Hand Lab family (`two_hand_lab_*`). Older captures simply lack these keys:
absence is not false or zero, and the diagnostics report "not recorded" instead of
inventing a state.

- `support_epoch` is process-local. Compare it only between adjacent frames of one
  capture, never across captures or sessions. Epoch 0 means the relationship was
  readable and coherently disengaged; 18446744073709551615 means it could not be
  read, which is a different thing.
- `support_solve_trusted` is the qualification permission of that invocation, not
  proof the solve consumed support geometry. `support_force_one_hand` is explicit
  persistent-grip forcing, not an ordinary two-hand setting being off.
- `support_solve_serial` is a stamped solve serial that normally lags
  `prepared_serial`; never assert the two are equal.
- `reticle_presented_support_trusted` / `reticle_presented_support_epoch` belong to
  the reticle's own, normally lagged assembly and are never the canonical solve trust.
- The Two-Hand Lab values are meaningful only while `two_hand_lab_enabled` is true.
- Same-frame counterfactual control profiles re-solve independently and do not carry
  the canonical assembly support facts.

The diagnostics index these as episodes (method `persistent_grip_lab_episodes`,
query presets `pg-lab` and `pg-lab-events`). Episodes are recorded-state indexes:
they do not establish causality, a disengaged relationship, or that a solve consumed
support geometry.

## Two-Hand transition/input smoothing values

Newer captures also carry the per-frame transition/input smoothing family
(`two_hand_transition_smoothing_configured`, `two_hand_transition_active`,
`two_hand_smoothing_configured`, `two_hand_smoothing_applied`,
`two_hand_smoothing_alpha`, `two_hand_smoothing_*_error_*`) and, in
strength-slider captures, `two_hand_smoothing_strength` and
`two_hand_smoothing_mix`, plus the free two-hand product setting
`two_hand_offhand_influence`. The family is gated on the presence of
`two_hand_transition_smoothing_configured`; a capture may lack the whole family.

- `two_hand_smoothing_strength` is the user amount in 0..25 (0 = raw/off, 25 = the
  full fixed speed-25 input filter) frozen for that prepared serial. It is never a
  live re-read of the config slider.
- `two_hand_smoothing_mix` is strength/25: the applied wet/dry amount, never a
  filter speed. `two_hand_smoothing_alpha` is the filter's internal
  clamp(25*dt, 0, 1) temporal coefficient, never the user amount.
- `two_hand_smoothing_applied` means the eligible free two-hand path of that prepared
  frame consumed the frozen strength; `two_hand_smoothing_configured` is strength > 0.
- `two_hand_offhand_influence` is the free two-hand product offhand directional
  authority in 0..1, frozen for that prepared serial from the frame's own assembly
  (Virtual Stock solves never read it, and a Lab-active solve uses its own
  `two_hand_lab_offhand_influence`). A capture that predates the field simply lacks
  the key: absence is not a zero authority.
- The `two_hand_smoothing_*_error_*` values are raw-to-full-filtered input
  differences (degrees / OpenXR local metres), never solver or presented-aim errors.
- Boolean-era recordings carry the family without strength/mix: absence is not zero
  or false, and the diagnostics report "not recorded" rather than a value.
- `two_hand_transition_smoothing_configured` / `two_hand_transition_active` are the
  product 200 ms latch-continuity pair. The continuity itself is fixed-on internal
  product behaviour with no user toggle (the retired `two_hand_transition_smoothing`
  key no longer resolves), while `two_hand_transition_smoothing_configured` reports
  the effective truth for the frame; `effective_settings.two_hand_latched` is the
  effective latch and is reported as context only.

The diagnostics summarise these values (strength/mix, offhand influence, alpha,
error distributions and latch coincidence) inside method
`persistent_grip_lab_episodes`. The summaries are descriptive recorded-value
counts; they do not establish a smoothing effect, a direction, or a preference.

## Raw retrieval

The analyser can return original raw records without reinterpretation:

- `--raw-window-serial N --before B --after A`
- `--raw-range-serial FIRST:LAST`
- `--raw-range-lines FIRST:LAST`
- optional `--raw-record-type TYPE` and `--raw-output PATH`

## Bindings

`<stem>.manifest.json` records the exact byte SHA-256, size, telemetry schema, frame
count and prepared-serial range of the raw capture it describes. A manifest whose
binding does not match a raw file belongs to a different capture and must not orient
analysis of that file. `--verify-manifest PATH` checks one manifest against one
recording.
"""

DIAGNOSTIC_DISCLAIMER = (
    'These are derived calculations, heuristics, indexes, counterfactuals and/or '
    'simulations generated by the analyser. They are useful for locating candidate '
    'evidence, generating hypotheses, regression testing and cross-checking independent '
    'calculations. They are not an evidentiary substitute for the source JSONL. '
    'Selection or ranking of an interval or metric does not establish that it is '
    'causally important or relevant to the current research question.'
)

DIAGNOSTIC_CLASSIFICATIONS = (
    'deterministic_derivation',
    'heuristic_index',
    'heuristic_index_and_deterministic_metrics',
    'offline_simulation',
    'counterfactual',
    'health_check',
)

SIGNAL_DEFINITIONS = {
    'a': {'description': 'traced A candidate direction', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'aim_trace.a_direction', 'validity_path': 'aim_trace.a_valid'}]},
    'b': {'description': 'traced B candidate direction', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'aim_trace.b_direction', 'validity_path': 'aim_trace.b_accepted'}]},
    'hip': {'description': 'traced A/B hip-blend direction', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'aim_trace.hip_aim_direction', 'validity_path': 'aim_trace.hip_aim_valid'}]},
    'c': {'description': 'traced C candidate direction', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'aim_trace.c_direction', 'validity_path': 'aim_trace.c_valid'}]},
    'canonical': {'description': 'recorded canonical output direction', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'canonical_aim.forward', 'validity_path': 'canonical_aim.valid'}]},
    'fixed_head': {'description': 'same-frame Fixed Head counterfactual output', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'cf_fixed_head.aim.forward', 'validity_path': 'cf_fixed_head.aim.valid'}]},
    'fixed_shoulder': {'description': 'same-frame Fixed Shoulder counterfactual output', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'cf_fixed_shoulder.aim.forward', 'validity_path': 'cf_fixed_shoulder.aim.valid'}]},
    'vs_off': {'description': 'same-frame VS OFF counterfactual output', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'cf_vs_off.aim.forward', 'validity_path': 'cf_vs_off.aim.valid'}]},
    'primary': {'description': 'semantic primary aim pose after MCC handedness routing', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'semantic_primary_aim', 'validity_path': 'semantic_primary_aim_valid'}]},
    'primary_forward': {'description': 'semantic primary forward vector', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'semantic_primary_forward', 'validity_path': 'semantic_primary_aim_valid'}]},
    'support': {'description': 'semantic support aim pose', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'semantic_support_aim', 'validity_path': 'semantic_support_aim_valid'}]},
    'support_endpoint': {'description': 'effective semantic support endpoint selected for the Virtual Stock support path and for the Two-Hand Lab Production anchor pass-through; it may be sourced from the support aim position or from the position-only grip-action endpoint, and support_endpoint_used_grip records which source was selected. It is not the consumed free two-hand product geometry: with Virtual Stock off (and the Lab inactive) the two-hand solve consumes the fixed primary-Grip -> support-Grip positional pair reported by support_grip_position / semantic_primary_grip_position, so this field is provenance for the VS/Lab paths rather than a VS-off consumption receipt', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'semantic_support_endpoint', 'validity_path': 'semantic_support_endpoint_valid'}]},
    'grip': {'description': 'semantic support grip after handedness routing (schema-1/2: routed pad.gripL)', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'pad.gripL'}]},
    'primary_grip': {'description': 'semantic primary grip (schema-1/2: routed pad.gripR)', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'pad.gripR'}]},
    'support_grip_position': {'description': 'position-only support grip locate', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'support_grip_position', 'validity_path': 'support_grip_valid'}]},
    'head': {'description': 'semantic HMD pose (normalised LOCAL view pose with MCC headset smoothing applied)', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'semantic_hmd', 'validity_path': 'head_sample_valid'}]},
    'w_natural': {'description': 'natural Hybrid authority before diagnostic override', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'aim_trace.w_natural', 'validity_path': 'aim_trace.w_natural_valid'}]},
    'w_effective': {'description': 'effective Hybrid authority recorded for the frame', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'aim_trace.w_effective', 'validity_path': 'aim_trace.w_effective_valid'}]},
    'final_direction': {'description': 'pre-calibration traced solver final direction', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'aim_trace.final_direction', 'validity_path': 'aim_trace.final_direction_valid'}]},
    'test_profile_id': {'description': 'selected test-profile id for the frame', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'test_profile_id'}]},
    'menu_open': {'description': 'menu-open structural state', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'menu_open'}]},
    'contact_space_epoch': {'description': 'contact-space reference epoch', 'bindings': [{'telemetry_schema_versions': [1, 2], 'raw_path': 'contact_space_epoch'}]},
}

DIAGNOSTIC_METHOD_CATALOG = {
    'recorder_health': {
        'classification': 'health_check',
        'description': 'Parse status, serial gaps, session-end accounting identity and recorder hot-path timing.',
        'raw_inputs': ['session_start.qpc_frequency', 'session_end', 'frames[].prepared_serial', 'frames[].capture_begin_qpc', 'frames[].capture_end_qpc'],
        'validity_requirements': [],
        'inclusion': 'all successfully parsed frame records; session_end fields when the record is present',
        'exclusion': 'none',
        'parameters': [],
        'reports_derived_metric': True,
        'metric_definitions': {
            'hotpath_us': '(capture_end_qpc - capture_begin_qpc) * 1e6 / session_start.qpc_frequency',
            'serial_gap': 'prepared_serial differs from the previous parsed frame serial + 1; the previous serial resets when a frame has no finite serial',
            'accounting_ok': 'session_end.producer_calls == duplicate_serial_suppressed + enqueued + dropped_queue_full',
            'written_matches_frames': 'session_end.written == number of parsed frame records',
            'status_vocabulary': 'clean | recoverable_trailing_partial | corrupt_middle',
        },
    },
    'profile_registry': {
        'classification': 'deterministic_derivation',
        'description': 'Session profile registry copy; schema-1 roles are inferred by stable name, never numeric id.',
        'raw_inputs': ['session_start.test_profile_enum_mapping'],
        'validity_requirements': [],
        'inclusion': 'mapping rows that declare an integer id and a string name',
        'exclusion': 'non-object rows',
        'parameters': [],
        'reports_derived_metric': False,
    },
    'profile_timeline': {
        'classification': 'deterministic_derivation',
        'description': 'Recording-local profile intervals, split when a temporary condition fingerprint changes.',
        'raw_inputs': ['frames[].test_profile_id', 'frames[].test_profile_name', 'frames[].effective_settings', 'session_start.test_profile_enum_mapping'],
        'validity_requirements': [],
        'inclusion': 'maximal runs of consecutive parsed frames with identical (test_profile_id, test_profile_name)',
        'exclusion': 'a temporary profile condition change (stable name + static effective-settings fingerprint) splits a run even when id/name are unchanged',
        'parameters': [],
        'reports_derived_metric': True,
        'metric_definitions': {
            'frames': 'number of parsed frame records in the interval, inclusive of both bounds',
            'start_time_s/end_time_s': 'frame_relative_seconds of the first/last interval frame',
        },
    },
    'profile_summaries': {
        'classification': 'deterministic_derivation',
        'description': 'Per-profile aggregation of recorded authority, path and validity fields.',
        'raw_inputs': ['canonical', 'aim_trace.*', 'w_natural', 'w_effective', 'primary', 'head', 'cf_fixed_head.fixed_proximity_influence', 'semantic_primary_aim_valid', 'head_sample_valid'],
        'validity_requirements': ['each statistic only uses frames where its own validity flag is true (w_*_valid, seat_valid, release_geometry_valid, semantic_primary_aim_valid, head_sample_valid, fixed_target_valid + fixed_proximity_enabled + fixed_proximity_calculated)'],
        'inclusion': 'all parsed frame records, grouped by cross-run condition identity',
        'exclusion': 'a value is omitted, never coerced, when its validity flag is false or missing',
        'parameters': [],
        'reports_derived_metric': True,
        'metric_definitions': {
            'two_hand_active_pct': '100 * count(canonical_aim.two_hand_active is true) / profile frames',
            'b_accepted_pct / b_extreme_rejected_pct / b_other_invalid_pct': '100 * count of the corresponding b_state over profile frames; b_state = accepted when b_accepted, extreme_rejected when b_extreme_rejected, other_invalid_or_degenerate when attempted, not_attempted otherwise',
            'stock_contributed_pct': '100 * count(aim_trace.stock_contributed is true) / frames whose aim_trace.path == hybrid',
            'w_natural / w_effective': 'n, min, p50 (median), mean, p95 and p99 (linear interpolation), max over frames with the matching validity flag',
            'seat_error_m': 'same statistics over frames with seat_valid true',
            'rear_to_stock_target_m': 'same statistics over frames with release_geometry_valid true',
            'horizontal_reach_m': 'same statistics over sqrt((primary.x - head.x)^2 + (primary.z - head.z)^2) when semantic_primary_aim_valid and head_sample_valid',
            'fixed_head_proximity_influence': 'same statistics over cf_fixed_head.fixed_proximity_influence when fixed_target_valid, fixed_proximity_enabled and fixed_proximity_calculated are all true',
            'offhand_i0_i1_span_deg': 'angle between recomposed hybrid outputs at offhand influence 0 and 1 (see head_yaw_stationary_hand signal composition)',
        },
    },
    'setting_changes': {
        'classification': 'deterministic_derivation',
        'description': 'Same-profile effective-settings diffs plus explicit raw settings-change events when present.',
        'raw_inputs': ['frames[].effective_settings', 'frames[].test_profile_id', 'frames[].test_profile_name', 'settings-change event records'],
        'validity_requirements': [],
        'inclusion': 'adjacent parsed frames with identical (test_profile_id, test_profile_name) whose nested effective-settings diff is non-empty; plus records whose provider/event matches a settings-change declaration',
        'exclusion': 'dynamic fields two_hand_latched and support_endpoint_used_grip; profile-change boundaries',
        'parameters': [],
        'reports_derived_metric': True,
        'metric_definitions': {
            'change': 'per nested field path: old value on the previous frame -> new value on the current frame',
            'menu_context': 'menu_open on either frame of the diff',
        },
    },
    'setting_sweeps': {
        'classification': 'deterministic_derivation',
        'description': 'Collapses adjacent same-field setting changes into sweeps.',
        'raw_inputs': ['setting_changes'],
        'validity_requirements': [],
        'inclusion': 'consecutive effective_frame_diff changes with identical field and profile whose relative-time gap <= setting_sweep_gap_s',
        'exclusion': 'raw settings events and non-matching fields terminate a sweep',
        'parameters': ['setting_sweep_gap_s'],
        'reports_derived_metric': True,
        'metric_definitions': {
            'steps': 'number of collapsed changes',
            'from/to': 'first old value and last new value of the sweep',
        },
    },
    'grip_latch': {
        'classification': 'deterministic_derivation',
        'description': 'Grip/latch/two-hand-state episode index from recorded grip and effective settings.',
        'raw_inputs': ['grip', 'effective_settings.two_hand_latched', 'canonical_aim.two_hand_active'],
        'validity_requirements': ['semantic support grip is pad.gripL for schema 1/2 after handedness routing; a missing grip value is unavailable, not zero'],
        'inclusion': 'held = semantic support grip >= grip_held_threshold; latch transitions where effective_settings.two_hand_latched changes between adjacent frames',
        'exclusion': 'none; episodes are exact index runs',
        'parameters': ['grip_held_threshold'],
        'reports_derived_metric': True,
        'metric_definitions': {
            'latch_released_while_grip_held': 'a latched -> unlatched transition on a frame whose grip is still held',
            'grip_held_unlatched_runs': 'maximal runs of held frames with latched false',
            'grip_and_latch_held_but_two_hand_aim_inactive_runs': 'maximal runs of held+latch-true frames with canonical_aim.two_hand_active false',
        },
    },
    'head_yaw_stationary_hand': {
        'classification': 'heuristic_index_and_deterministic_metrics',
        'description': 'Indexes intervals useful for examining aim response during approximate stationary-controller head yaw.',
        'raw_inputs': ['head', 'primary', 'support', 'canonical', 'c', 'fixed_head', 'fixed_shoulder', 'vs_off'],
        'validity_requirements': ['head_sample_valid, semantic_primary_aim_valid and semantic_support_aim_valid on both frames of a step', 'each reported signal uses its own validity gate (canonical_aim.valid, aim_trace.c_valid, cf_*.aim.valid)'],
        'inclusion': 'consecutive steps where no transition flag is set, primary and support positions each moved <= stationary_hand_mm, primary and support rotations each <= stationary_hand_rotation_deg, and head yaw step magnitude is within [head_episode_step_min_deg, head_yaw_max_deg]; episodes keep a single yaw sign and need >= head_episode_min_frames frames and >= head_episode_min_excursion_deg net head yaw',
        'exclusion': 'any transition_flags(p, c) non-empty (profile, settings, menu, latch, path, reference-space, tracking-validity, serial gap); steps failing the stationarity or yaw-step bounds; episodes below the minimum frame or excursion thresholds',
        'parameters': ['stationary_hand_mm', 'stationary_hand_rotation_deg', 'head_yaw_max_deg', 'head_episode_step_min_deg', 'head_episode_min_excursion_deg', 'head_episode_min_frames'],
        'reports_derived_metric': True,
        'not_a_claim': 'Selection does not establish that head motion caused the observed aim motion.',
        'metric_definitions': {
            'head_yaw_excursion_deg': 'unwrapped yaw of the head forward vector at the last minus first episode frame',
            'net_excursion_deg / peak_to_peak_deg / path_abs_sum_deg': 'per signal, over unwrapped signal yaw steps',
            'slope': 'sum(head_step * aim_step) / sum(head_step^2)',
            'median_abs_ratio': 'median(abs(aim_step / head_step))',
            'opposite_fraction': 'fraction of included steps where head_step * aim_step < 0',
            'lag_table': 'the same regression computed with the aim step shifted by -2..+2 frames',
            'signal_composition': 'candidate aliases are read from their declared raw path and validity gate; no best candidate is selected',
        },
    },
    'b_boundary': {
        'classification': 'heuristic_index',
        'description': 'B acceptance/rejection boundary transition index and chatter episodes; mechanism and visible consequence are separate.',
        'raw_inputs': ['aim_trace.b_attempted', 'aim_trace.b_accepted', 'aim_trace.b_extreme_rejected', 'aim_trace.a_dot_b', 'aim_trace.b_rejected_agreement', 'aim_trace.w_effective', 'canonical'],
        'validity_requirements': ['b_attempted true on at least one side of the transition', 'w_prev/w_now only reported when w_effective_valid'],
        'inclusion': 'adjacent parsed frames where b_accepted flips',
        'exclusion': 'none; masking is reported rather than filtered',
        'parameters': ['chatter_gap_s'],
        'reports_derived_metric': True,
        'not_a_claim': 'A boundary flip is a recorded state change; the visible aim consequence is reported separately.',
        'metric_definitions': {
            'jump_deg': 'angle between adjacent canonical_aim.forward vectors',
            'agreement': 'a_dot_b when accepted, else b_rejected_agreement when extreme-rejected, else unavailable',
            'stock_masked_high_w': 'both valid w_prev and w_now >= 0.9',
            'chatter_episode': '>= 2 events of one profile whose adjacent relative-time gap <= chatter_gap_s; toggles = event count',
        },
    },
    'w_transitions': {
        'classification': 'deterministic_derivation',
        'description': 'Released/transition/full effective-authority region crossings.',
        'raw_inputs': ['w_effective', 'canonical'],
        'validity_requirements': ['w_effective_valid true on both frames'],
        'inclusion': 'adjacent parsed frames whose region differs; region = released (w <= 0.01), full (w >= 0.99), transition otherwise',
        'exclusion': 'frames with unavailable w',
        'parameters': [],
        'reports_derived_metric': True,
        'metric_definitions': {
            'from/to': 'previous and current region names',
            'jump_deg': 'angle between adjacent canonical_aim.forward vectors',
        },
    },
    'fixed_zero_crossings': {
        'classification': 'deterministic_derivation',
        'description': 'Fixed-stock valid effective-strength zero crossings; inapplicable strength is never coerced to zero.',
        'raw_inputs': ['aim_trace.fixed_effective_strength', 'aim_trace.fixed_target_valid', 'aim_trace.fixed_proximity_enabled', 'aim_trace.fixed_proximity_calculated', 'aim_trace.fixed_configured_strength'],
        'validity_requirements': ['fixed_target_valid, fixed_proximity_enabled and fixed_proximity_calculated true, and fixed_configured_strength finite, on both frames'],
        'inclusion': 'adjacent frames where a <= 1e-6 < b or b <= 1e-6 < a for the valid effective strength',
        'exclusion': 'frames where the fixed-stock trace is inapplicable',
        'parameters': [],
        'reports_derived_metric': True,
        'metric_definitions': {
            'from/to': 'previous and current valid effective strength',
            'from_exact_a/to_exact_a': 'aim_trace.exact_a_endpoint_selected on each frame',
            'jump_deg': 'angle between adjacent canonical_aim.forward vectors',
        },
    },
    'aim_jumps': {
        'classification': 'deterministic_derivation',
        'description': 'Largest adjacent-frame canonical aim jumps, all and transition-filtered.',
        'raw_inputs': ['canonical'],
        'validity_requirements': ['finite canonical_aim.forward vectors on both frames'],
        'inclusion': 'every adjacent parsed frame pair with finite forward vectors',
        'exclusion': 'the steady-state list excludes pairs whose transition_flags intersects profile_change, setting_change, menu_transition, latch_change, path_change, reference_space_change, tracking_validity_change, tracking_invalid or serial_gap',
        'parameters': ['top_jumps'],
        'reports_derived_metric': True,
        'metric_definitions': {
            'jump_deg': 'angle between previous and current canonical_aim.forward',
            'transition_flags': 'the full transition_flags set for the pair',
            'all/steady stats': 'n, min, p50, mean, p95, p99, max of jump_deg',
        },
    },
    'movement_response': {
        'classification': 'heuristic_index',
        'description': 'Adjacent-frame isolated-motion gain indexes for rear/support translation and rotation.',
        'raw_inputs': ['primary', 'support', 'head', 'canonical', 'support_endpoint_used_grip'],
        'validity_requirements': ['all of primary position/quaternion, support position/quaternion, head quaternion and canonical forward finite on both frames'],
        'inclusion': 'pairs with no transition flags where the named source moves at least motion_min_mm or rotation_min_deg while every other tracked source stays within its stationary bound',
        'exclusion': 'pairs with any transition flag; pairs where positions/quaternions are unavailable',
        'parameters': ['motion_min_mm', 'motion_stationary_mm', 'motion_stationary_rot_deg', 'rotation_min_deg'],
        'reports_derived_metric': True,
        'not_a_claim': 'Adjacent-frame isolation heuristics index sensitivity; they are not controlled causal experiments.',
        'metric_definitions': {
            'gain': 'jump_deg / source movement, where source movement is mm for translation and degrees for rotation; reported as n, min, p50, mean, p95, p99, max',
            'support_rotation_grip_endpoint vs support_rotation_aim_endpoint': 'split by support_endpoint_used_grip on the current frame',
        },
    },
    'geometry_indexes': {
        'classification': 'heuristic_index',
        'description': 'Seat projection regions and orientation-builder near-singularity index.',
        'raw_inputs': ['aim_trace.seat_raw_projection', 'aim_trace.seat_valid', 'aim_trace.final_direction', 'aim_trace.final_direction_valid', 'aim_trace.primary_quaternion'],
        'validity_requirements': ['seat_raw_projection only read when seat_valid; cross magnitude only when final_direction_valid'],
        'inclusion': 'frames with the relevant validity flags',
        'exclusion': 'frames without the relevant validity flags',
        'parameters': ['singularity_cross_warn'],
        'reports_derived_metric': True,
        'not_a_claim': 'Projection regions and cross magnitude are descriptive indexes, not defect classifications.',
        'metric_definitions': {
            'seat_projection_regions': 'counts of uRaw < 0, 0 <= uRaw <= 1, uRaw > 1, and |uRaw| < 0.02 or |uRaw - 1| < 0.02',
            'orientation_cross_magnitude': '|normalize(final_direction) x rotate_up(primary_quaternion)| with n, min, p50, mean, p95, p99, max',
            'near_singularity_count': 'count of cross magnitude values < singularity_cross_warn',
        },
    },
    'control_oracles': {
        'classification': 'deterministic_derivation',
        'description': 'Same-frame canonical-versus-control angular identity check using semantic profile roles.',
        'raw_inputs': ['canonical', 'vs_off', 'fixed_head', 'fixed_shoulder', 'session_start.test_profile_enum_mapping'],
        'validity_requirements': ['finite canonical and control forwards'],
        'inclusion': 'frames whose selected profile role maps to a control role (vs_off_control, fixed_head_control, fixed_shoulder_control)',
        'exclusion': 'frames with non-control roles or unavailable vectors',
        'parameters': [],
        'reports_derived_metric': True,
        'metric_definitions': {
            'error_deg': 'angle between canonical_aim.forward and the matching same-frame control forward; reported per control profile as n, min, p50, mean, p95, p99, max',
            'mismatch_examples': 'first frames with error_deg > 0.001, capped at 30',
        },
    },
    'state_pose_mismatches': {
        'classification': 'heuristic_index',
        'description': 'Grip/latch held while canonical two-hand aim is inactive; does not by itself prove a rendered IK hand detached.',
        'raw_inputs': ['grip_latch episodes', 'aim_trace.b_accepted', 'aim_trace.exact_a_endpoint_selected', 'w_effective'],
        'validity_requirements': ['same as grip_latch'],
        'inclusion': 'the grip_and_latch_held_but_two_hand_aim_inactive_runs from grip_latch',
        'exclusion': 'physical latch release episodes are a different classification',
        'parameters': ['grip_held_threshold'],
        'reports_derived_metric': True,
        'not_a_claim': 'A solver/presentation-state mismatch does not by itself prove a rendered IK hand visibly detached.',
        'metric_definitions': {
            'start_b_state': 'b_state of the run first frame',
            'start_exact_a / start_w_effective': 'recorded values at the run first frame',
        },
    },
    'horizontal_rear_release': {
        'classification': 'deterministic_derivation',
        'description': 'Recorded horizontal reach distribution and actual schema-2 horizontal-release fields when present.',
        'raw_inputs': ['primary', 'head', 'w_natural', 'cf_fixed_head', 'effective_settings.horizontal_release_*', 'aim_trace.horizontal_release_*'],
        'validity_requirements': ['reach only computed when semantic_primary_aim_valid and head_sample_valid', 'fixed-head proximity only when fixed_target_valid, fixed_proximity_enabled and fixed_proximity_calculated'],
        'inclusion': 'all frames with valid primary and head samples, binned by reach',
        'exclusion': 'no release criterion is inferred; unavailable values are omitted',
        'parameters': ['horizontal_candidates'],
        'reports_derived_metric': True,
        'metric_definitions': {
            'reach_m': 'sqrt((primary.x - head.x)^2 + (primary.z - head.z)^2); Y is ignored',
            'reach_bins': 'edges [-inf, 0.27, 0.30, 0.32, 0.425, 0.45, 0.46, +inf), membership lo <= reach < hi; each bin reports frame count, w_natural stats, percent w_natural >= 0.9, fixed-head proximity stats and percent <= 0.1',
            'actual_fields': 'schema-2+ recorded fields are reported per frame when any is present',
        },
    },
    'horizontal_offline_candidates': {
        'classification': 'offline_simulation',
        'description': 'Offline O/P/Q cubic release-factor candidates; not recorded production behaviour.',
        'raw_inputs': ['primary', 'head'],
        'validity_requirements': ['semantic_primary_aim_valid and head_sample_valid on the frame'],
        'inclusion': 'all frames with valid primary and head samples',
        'exclusion': 'invalid or unavailable reach',
        'parameters': ['horizontal_candidates'],
        'reports_derived_metric': True,
        'not_a_claim': 'Offline candidates are simulations; they are not recorded production behaviour and no curve is selected as correct.',
        'metric_definitions': {
            'h': '1.0 when reach <= full_m; 0.0 when reach >= release_m; otherwise 1 - (t^2 * (3 - 2t)) with t = (reach - full_m) / (release_m - full_m)',
            'candidate set': 'horizontal_candidates; defaults O_270_425 (0.270/0.425), P_300_450 (0.300/0.450), Q_320_460 (0.320/0.460)',
        },
    },
    'horizontal_reach_episodes': {
        'classification': 'heuristic_index_and_deterministic_metrics',
        'description': 'Outward/inward monotonic XZ reach episode index.',
        'raw_inputs': ['primary', 'head', 'w_natural', 'w_effective', 'cf_fixed_head', 'canonical'],
        'validity_requirements': ['reach available on both frames of a step; no transition flags'],
        'inclusion': 'monotonic runs whose step magnitude >= reach_episode_step_min_m, at least reach_episode_min_frames frames and |delta| >= reach_episode_min_delta_m; direction outward when delta > 0',
        'exclusion': 'steps with transition flags or unavailable reach; episodes below the minimum frame or delta thresholds',
        'parameters': ['reach_episode_step_min_m', 'reach_episode_min_delta_m', 'reach_episode_min_frames', 'horizontal_candidates'],
        'reports_derived_metric': True,
        'not_a_claim': 'Episode selection is a geometric index; it does not establish a release mechanism.',
        'metric_definitions': {
            'reach_start_m/reach_end_m/reach_delta_m': 'XZ reach at first/last episode frame and their difference',
            'vertical_primary_minus_head_start_m/end_m': 'primary Y minus head Y at first/last frame',
            'offline_h': 'per candidate, h at start, end, min and max over the episode',
            'aim_net_jump_deg': 'angle between canonical forwards at first and last frame',
        },
    },
    'low_held_retention': {
        'classification': 'heuristic_index',
        'description': 'Reports offline horizontal-candidate influence for frames with the primary position at least the configured distance below the HMD.',
        'raw_inputs': ['primary', 'head'],
        'validity_requirements': ['finite primary and head positions'],
        'inclusion': 'frames with primary Y - head Y <= -low_held_below_head_m',
        'exclusion': 'frames without finite positions',
        'parameters': ['low_held_below_head_m', 'horizontal_candidates'],
        'reports_derived_metric': True,
        'not_a_claim': 'The vertical condition is geometric; the analyser does not classify the posture as a defect or a false engagement.',
        'metric_definitions': {
            'low_held_frames': 'count of included frames per profile',
            'candidates': 'per candidate: h statistics, percent h >= 0.9 and percent h <= 0.1',
        },
    },
    'authority_decomposition': {
        'classification': 'deterministic_derivation',
        'description': 'Neutral angular-distance decomposition with pre/post-calibration domain separation.',
        'raw_inputs': ['aim_trace.final_direction', 'aim_trace.a_direction', 'aim_trace.b_direction', 'aim_trace.hip_aim_direction', 'aim_trace.c_direction', 'canonical', 'vs_off', 'fixed_head', 'fixed_shoulder'],
        'validity_requirements': ['solver: final_direction_valid plus the per-candidate gate (a_valid, b_accepted, hip_aim_valid, c_valid); control: finite canonical and control forwards'],
        'inclusion': 'every frame where the compared vectors are available',
        'exclusion': 'unavailable candidate or control vectors',
        'parameters': [],
        'reports_derived_metric': True,
        'metric_definitions': {
            'solver_precal_angles_deg': 'angle(final_direction, candidate) for A/B/Hip/C; all pre-calibration',
            'control_postcal_angles_deg': 'angle(canonical_aim.forward, cf_*.aim.forward); all post-calibration',
            'close_counts': 'counts of angles <= 0.1, 1.0 and 5.0 degrees',
        },
    },
    'transition_timeline': {
        'classification': 'deterministic_derivation',
        'description': 'Indexed semantic/validity/state transitions.',
        'raw_inputs': ['frames[] profile/settings/menu/focus/render/session/validity/aim_trace/canonical fields'],
        'validity_requirements': [],
        'inclusion': 'adjacent parsed frame pairs where at least one indexed predicate changes',
        'exclusion': 'pairs with no indexed change are not emitted',
        'parameters': [],
        'reports_derived_metric': True,
        'metric_definitions': {
            'events': 'predicate names that changed: profile_change, setting_change, menu_open/menu_close, focus_change, should_render_change, openxr_session_state_change, per-signal valid/invalid, support_endpoint_source_change, c_valid_change, shoulder_head_fallback_change, exact_a_change, w_region_change, b_state_change, two_hand_active_change, orientation_rebuild_failed, contact_space_epoch_change',
            'aim_jump_deg': 'angle between adjacent canonical aim forwards',
        },
    },
    'performance_index': {
        'classification': 'deterministic_derivation',
        'description': 'Recorder hot-path and prepared-frame interval indexes.',
        'raw_inputs': ['frames[].capture_begin_qpc', 'frames[].capture_end_qpc', 'frames[].predicted_display_period', 'session_start.qpc_frequency'],
        'validity_requirements': ['finite QPC fields; finite positive predicted_display_period for interval ratios'],
        'inclusion': 'all parsed frames with the required fields',
        'exclusion': 'unavailable timing fields',
        'parameters': ['top_performance', 'frame_interval_warn_ratio'],
        'reports_derived_metric': True,
        'metric_definitions': {
            'hotpath_us': '(capture_end_qpc - capture_begin_qpc) * 1e6 / qpc_frequency',
            'frame_interval_ratio_to_predicted_period': '(time_s[i] - time_s[i-1]) / (predicted_display_period / 1e9)',
            'frame_interval_outliers': 'ratios >= frame_interval_warn_ratio',
            'top_hotpath_frames': 'top top_performance frames by hotpath_us',
        },
    },
    'temporary_profile_provenance': {
        'classification': 'deterministic_derivation',
        'description': 'Temporary profile condition identity by stable name plus static effective-settings fingerprint.',
        'raw_inputs': ['frames[].test_profile_id', 'frames[].test_profile_name', 'frames[].effective_settings', 'session_start.test_profile_enum_mapping'],
        'validity_requirements': ['temporary ids are considered only for schema 2+ and when the id appears in the session mapping'],
        'inclusion': 'ids 128..254 present in the session mapping',
        'exclusion': 'reserved id 255; core ids below 128',
        'parameters': [],
        'reports_derived_metric': True,
        'metric_definitions': {
            'settings_fingerprint': 'sha256 over typed canonical tokens of the static effective settings, excluding dynamic fields two_hand_latched and support_endpoint_used_grip (format sha256:mccvr-static-effective-settings-canonical-v1)',
            'cross_run_identity_key': 'temporary:sha256 over canonical tokens of {stable_name, settings_fingerprint}',
            'name_collision': 'one stable name observed with more than one settings fingerprint',
        },
    },
    'event_inventory': {
        'classification': 'deterministic_derivation',
        'description': 'Generic inventory of non-frame record/event categories observed in the raw file.',
        'raw_inputs': ['non-frame, non-session raw records'],
        'validity_requirements': [],
        'inclusion': 'every parsed record whose type is not session_start, session_end or frame',
        'exclusion': 'none; categories are counted, never ranked or dropped',
        'parameters': [],
        'reports_derived_metric': False,
    },
    'annotations': {
        'classification': 'deterministic_derivation',
        'description': 'Subjective annotation sidecar aggregation when an annotations file is supplied.',
        'raw_inputs': ['annotation intervals start_s/end_s', 'frames[].effective_settings.w_effective', 'primary', 'head', 'canonical'],
        'validity_requirements': ['interval bounds finite; per-frame validity gates as for profile_summaries'],
        'inclusion': 'frames whose frame_relative_seconds falls inside [start_s, end_s] for an annotation interval',
        'exclusion': 'intervals with non-finite bounds are skipped',
        'parameters': [],
        'reports_derived_metric': True,
        'metric_definitions': {
            'frames/profiles': 'frame count and profile counter inside the interval',
            'w_effective/horizontal_reach_m/aim_jump_deg': 'the same statistics definitions used by profile_summaries and aim_jumps, restricted to the interval',
        },
    },
    'persistent_grip_lab_episodes': {
        'classification': 'deterministic_derivation',
        'description': 'Indexed persistent-grip relationship/epoch/trust/forcing episodes plus Two-Hand Lab requested-versus-effective influence at those event frames, plus the per-capture Two-Hand transition/input smoothing value counts and distributions.',
        'raw_inputs': ['persistent_support_grip_configured', 'persistent_support_grip_applicable', 'support_relationship_readable', 'support_relationship_engaged', 'support_epoch', 'support_solve_trusted', 'support_force_one_hand', 'support_solve_serial', 'aim_trace.a_dot_b', 'aim_trace.b_accepted', 'aim_trace.b_extreme_rejected', 'aim_trace.b_rejected_agreement', 'aim_trace.b_steering_retained', 'canonical_aim.two_hand_active', 'effective_settings.two_hand_latched', 'two_hand_lab_*', 'two_hand_offhand_influence', 'two_hand_transition_smoothing_configured', 'two_hand_transition_active', 'two_hand_smoothing_configured', 'two_hand_smoothing_applied', 'two_hand_smoothing_strength', 'two_hand_smoothing_mix', 'two_hand_smoothing_alpha', 'two_hand_smoothing_primary_orientation_error_deg', 'two_hand_smoothing_primary_position_error_m', 'two_hand_smoothing_support_position_error_m', 'presented_aim_valid', 'reticle_presented_valid', 'reticle_presented_serial'],
        'validity_requirements': ['the persistent-grip, two-hand-lab and transition/input smoothing families are additive schema-2 families; a key that is absent stays missing (reported as not recorded) and is never coerced to false/zero', 'a_dot_b episodes use only frames with a finite recorded aim_trace.a_dot_b', 'the smoothing family is gated on the presence of its marker two_hand_transition_smoothing_configured per frame; two_hand_smoothing_strength/two_hand_smoothing_mix are a later strength-slider extension, so boolean-era frames carry the same family without them and report them as not recorded rather than zero', 'the present/reticle fields are reported only on smoothing-applied frames and only when the capture actually records them'],
        'inclusion': 'adjacent parsed frame pairs that emit a row: support_relationship_readable/engaged changes (relationship_transitions), support_epoch differences (epoch_changes), support_solve_trusted losses true->false (trust_loss_transitions; a false->true trust gain intentionally emits no row) and canonical_aim.two_hand_active losses true->false while engaged (two_hand_to_one_hand_while_engaged); plus maximal runs of frames whose support_force_one_hand is true, whose aim_trace.a_dot_b is below a_dot_b_agreement_floor, or whose aim_trace.b_steering_retained is true; plus every frame carrying the transition/input smoothing marker for the smoothing counts and the two_hand_smoothing_applied frames for its distributions',
        'exclusion': 'frames or captures without the family (reported as not recorded); frames without a finite a_dot_b are excluded from the floor index; support_epoch is compared only between adjacent frames of one capture; absent smoothing strength/mix and absent present/reticle fields are excluded from their distributions instead of being read as zero',
        'parameters': ['a_dot_b_agreement_floor'],
        'reports_derived_metric': True,
        'not_a_claim': 'Episodes are recorded-state indexes; they establish neither causality nor a disengaged relationship, PG being off, or a solve having consumed support geometry. The smoothing values are recorded amounts and paths, not a smoothness or quality verdict.',
        'metric_definitions': {
            'relationship_transitions': 'adjacent-frame edges of support_relationship_readable/support_relationship_engaged, classified acquire (engaged false->true), release (true->false) or readability change',
            'epoch_changes': 'adjacent-frame support_epoch differences within one capture, flagging the all-ones unreadable sentinel on either side',
            'trust_loss_transitions': 'adjacent frames where support_solve_trusted is true then false, with the engaged state on both sides',
            'force_one_hand_runs': 'maximal runs with support_force_one_hand true, with trusted/engaged frame counts and the epochs observed in the run',
            'agreement_below_floor_runs': 'maximal runs with a finite aim_trace.a_dot_b < a_dot_b_agreement_floor, reporting a_dot_b and b_rejected_agreement statistics plus b_accepted/b_extreme_rejected/b_steering_retained frame counts',
            'b_steering_retained_runs': 'maximal runs with aim_trace.b_steering_retained true',
            'two_hand_to_one_hand_while_engaged': 'adjacent frames where canonical_aim.two_hand_active is true then false while support_relationship_engaged is true on the second frame (the latch is context, not the relationship)',
            'lab_requested_vs_effective': 'two_hand_lab_offhand_influence versus two_hand_lab_effective_influence on frames where two_hand_lab_enabled is true; the other Lab values are reported only under that gate',
            'support_solve_serial_lag': 'distribution of prepared_serial - support_solve_serial within the capture; the pair is never asserted equal',
            'two_hand_smoothing_counts': 'per-capture frame counts of the recorded transition/input smoothing family booleans: two_hand_transition_smoothing_configured, two_hand_transition_active, two_hand_smoothing_configured and two_hand_smoothing_applied',
            'two_hand_smoothing_strength_mix': 'statistics of two_hand_smoothing_strength (the user amount 0..25, 0 = raw/off, 25 = the full fixed speed-25 input filter, frozen for that prepared serial) and two_hand_smoothing_mix (= strength/25, the applied wet/dry amount and never a filter speed) over the two_hand_smoothing_applied frames only',
            'two_hand_smoothing_alpha': 'statistics of two_hand_smoothing_alpha (the filter internal clamp(25*dt, 0, 1) temporal coefficient, never the user amount) over the two_hand_smoothing_applied frames',
            'two_hand_smoothing_errors': 'statistics of two_hand_smoothing_primary_orientation_error_deg, two_hand_smoothing_primary_position_error_m and two_hand_smoothing_support_position_error_m (raw-to-full-filtered input differences in degrees / OpenXR local metres, never solver or presented-aim errors) over the two_hand_smoothing_applied frames',
            'two_hand_offhand_influence': 'statistics of two_hand_offhand_influence (the free two-hand product offhand directional authority 0..1, frozen for that prepared serial from the frame own assembly; never read by Virtual Stock solves) over the family frames that record it; absence is reported as not recorded, never as a zero authority',
            'two_hand_smoothing_latch_coincidence': 'same-frame pair counts of two_hand_transition_active against effective_settings.two_hand_latched, counted only where both are recorded; descriptive coincidence counts, never a continuity verdict',
            'two_hand_smoothing_context': 'context on the two_hand_smoothing_applied frames: persistent-grip engaged/trusted/configured counts, Two-Hand Lab effective/requested influence statistics while two_hand_lab_enabled is true, and presented_aim_valid / reticle_presented_valid / prepared_serial - reticle_presented_serial frame counts when those fields are recorded',
            'events': 'merged chronological index of the episodes above, with each event frame own persistent-grip and Lab context',
        },
    },
}
for _catalog_entry in DIAGNOSTIC_METHOD_CATALOG.values():
    _catalog_entry.setdefault('supported_telemetry_schema_versions', [1, 2])
    _catalog_entry.setdefault('normalisation', 'none')

LOCATOR_CONVENTION = {
    'source_id_field': 'source_id',
    'record_type_field': 'record_type',
    'line_field': 'line',
    'span_start_field': 'start_line',
    'span_end_field': 'end_line',
    'serial_field': 'serial',
    'relative_time_field': 'time_s',
    'relative_time_basis': 'frame_relative_seconds',
    'note': 'Diagnostic locators identify frame records unless a method states otherwise. source_id resolves through scope.sources; time_s uses that source own timebase.',
}

KNOWN_VALIDITY_PAIRS = (
    ('frame', 'semantic_primary_aim', 'semantic_primary_aim_valid'),
    ('frame', 'semantic_primary_forward', 'semantic_primary_aim_valid'),
    ('frame', 'semantic_support_aim', 'semantic_support_aim_valid'),
    ('frame', 'semantic_support_endpoint', 'semantic_support_endpoint_valid'),
    ('frame', 'physical_left_aim', 'physical_left_aim_valid'),
    ('frame', 'physical_right_aim', 'physical_right_aim_valid'),
    ('frame', 'support_grip_position', 'support_grip_valid'),
    ('frame', 'semantic_primary_grip_position', 'semantic_primary_grip_position_valid'),
    ('frame', 'semantic_primary_linear_velocity', 'semantic_primary_linear_velocity_valid'),
    ('frame', 'semantic_support_linear_velocity', 'semantic_support_linear_velocity_valid'),
    ('frame', 'views', 'upcoming_views_valid'),
    ('frame', 'canonical_aim', 'canonical_aim.valid'),
    ('frame', 'cf_vs_off.aim', 'cf_vs_off.aim.valid'),
    ('frame', 'cf_fixed_head.aim', 'cf_fixed_head.aim.valid'),
    ('frame', 'cf_fixed_shoulder.aim', 'cf_fixed_shoulder.aim.valid'),
    ('frame', 'pad', 'pad.valid'),
    ('frame', 'aim_trace.a_direction', 'aim_trace.a_valid'),
    ('frame', 'aim_trace.b_direction', 'aim_trace.b_accepted'),
    ('frame', 'aim_trace.hip_aim_direction', 'aim_trace.hip_aim_valid'),
    ('frame', 'aim_trace.c_direction', 'aim_trace.c_valid'),
    ('frame', 'aim_trace.target', 'aim_trace.target_valid'),
    ('frame', 'aim_trace.raw_head_target', 'aim_trace.raw_head_target_valid'),
    ('frame', 'aim_trace.corrected_head_target', 'aim_trace.corrected_head_target_valid'),
    ('frame', 'aim_trace.virtual_rear', 'aim_trace.virtual_rear_valid'),
    ('frame', 'aim_trace.seat_closest', 'aim_trace.seat_closest_valid'),
    ('frame', 'aim_trace.final_direction', 'aim_trace.final_direction_valid'),
)

STRUCTURAL_RUN_INDEXES = (
    ('frame', 'menu_open'),
    ('frame', 'openxr_session_state'),
    ('frame', 'contact_space_epoch'),
)

DECLARED_SEMANTIC_KEYS = (
    'tracking_space', 'position_units', 'quaternion_order',
    'coordinate_handedness', 'up_axis', 'forward_axis',
    'semantic_primary_support', 'physical_left_right', 'semantic_hmd',
    'stereo_views', 'support_grip_endpoint', 'semantic_grip_positions',
)

DIAGNOSTIC_PARAMETER_KEYS = (
    'grip_held_threshold', 'top_jumps', 'stationary_hand_mm',
    'stationary_hand_rotation_deg', 'head_yaw_max_deg',
    'head_episode_step_min_deg', 'head_episode_min_excursion_deg',
    'head_episode_min_frames', 'horizontal_candidates', 'motion_min_mm',
    'motion_stationary_mm', 'motion_stationary_rot_deg', 'rotation_min_deg',
    'chatter_gap_s', 'singularity_cross_warn', 'reach_episode_step_min_m',
    'reach_episode_min_delta_m', 'reach_episode_min_frames',
    'low_held_below_head_m', 'setting_sweep_gap_s', 'top_performance',
    'frame_interval_warn_ratio', 'include_debug_derived_rows',
    'a_dot_b_agreement_floor',
)


def _configure_console_streams() -> None:
    """Keep decorative/non-ASCII text from crashing legacy Windows consoles."""
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, 'reconfigure', None)
        if reconfigure is None:
            continue
        try:
            reconfigure(errors='replace')
        except (ValueError, OSError):
            pass


def _write_stdout_text(text: str) -> None:
    buffer = getattr(sys.stdout, 'buffer', None)
    if buffer is not None:
        buffer.write(text.encode('utf-8'))
        buffer.flush()
    else:
        sys.stdout.write(text)


def _debug_per_frame_rows(args: argparse.Namespace, rows: list[Any]) -> Any:
    """Per-frame derived rows stay out of default diagnostics (A006 section 25)."""
    if bool(getattr(args, 'include_debug_derived_rows', False)):
        return rows
    return {
        'rows_omitted': True,
        'row_count': len(rows),
        'enable_with': '--include-debug-derived-rows',
        'note': 'Per-frame derived rows are omitted from default diagnostics.',
    }


def json_text(obj: Any) -> str:
    return json.dumps(obj, indent=2, sort_keys=True, ensure_ascii=False,
        allow_nan=False) + '\n'


def _write_text_if_changed(path: Path, text: str) -> bool:
    try:
        if path.exists() and path.read_text(encoding='utf-8') == text:
            return False
    except OSError:
        pass
    parent = path.parent
    if str(parent) not in ('', '.') and not parent.exists():
        parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')
    return True


def _write_raw_text(path: Path, text: str) -> None:
    """Write retrieved raw records without newline translation."""
    parent = path.parent
    if str(parent) not in ('', '.') and not parent.exists():
        parent.mkdir(parents=True, exist_ok=True)
    with path.open('w', encoding='utf-8', newline='') as handle:
        handle.write(text)


def guide_text() -> str:
    return TELEMETRY_AI_GUIDE_TEXT


def generator_identity() -> dict[str, Any]:
    return {'name': GENERATOR_NAME, 'version': ANALYSER_VERSION,
        'sha256': analyser_sha256()}


def sidecar_manifest_path(recording: Path, provisional: bool) -> Path:
    stem = str(recording.with_suffix(''))
    marker = '.provisional' if provisional else ''
    return Path(f'{stem}{marker}.manifest.json')


def sidecar_diagnostics_paths(
    recording: Path, provisional: bool) -> tuple[Path, Path]:
    stem = str(recording.with_suffix(''))
    marker = '.provisional' if provisional else ''
    return (Path(f'{stem}{marker}.diagnostics.json'),
        Path(f'{stem}{marker}.diagnostics.md'))


def sidecar_guide_path(recording: Path) -> Path:
    return recording.parent / TELEMETRY_AI_GUIDE_FILENAME


def assign_source_ids(hashes: Iterable[str]) -> dict[str, str]:
    """Content-derived source identity.

    Algorithm-qualified full content hash: stable across filename changes,
    input order and changes to the participating capture set.
    """
    return {str(h): 'sha256:' + str(h) for h in sorted({str(h) for h in hashes})}


def _get_path(obj: Any, path: str) -> Any:
    current: Any = obj
    for part in str(path).split('.'):
        if not isinstance(current, dict) or part not in current:
            return _MISSING
        current = current[part]
    return current


def _file_size(path: Path) -> int | None:
    try:
        return int(path.stat().st_size)
    except OSError:
        return None


def frame_serial_range(frames: list[dict[str, Any]]) -> tuple[int | None, int | None]:
    serials = [int(f['prepared_serial']) for f in frames
        if fnum(f.get('prepared_serial')) is not None]
    if not serials:
        return None, None
    return serials[0], serials[-1]


def _first_frame_qpc(frames: list[dict[str, Any]]) -> int | None:
    for frame in frames:
        value = fnum(frame.get('capture_begin_qpc'))
        if value is not None:
            return int(value)
    return None


def _last_frame_qpc(frames: list[dict[str, Any]]) -> int | None:
    for frame in reversed(frames):
        value = fnum(frame.get('capture_begin_qpc'))
        if value is not None:
            return int(value)
    return None


def _serial_gap_count(frames: list[dict[str, Any]]) -> int:
    count = 0
    previous: int | None = None
    for frame in frames:
        serial = fnum(frame.get('prepared_serial'))
        if serial is None:
            previous = None
            continue
        current = int(serial)
        if previous is not None and current != previous + 1:
            count += 1
        previous = current
    return count


def source_timebases(rec: ParsedRecording) -> dict[str, Any]:
    fq = int(rec.session_start.get('qpc_frequency', 0) or 0)
    first_qpc = _first_frame_qpc(rec.frames)
    out: dict[str, Any] = {}
    if fq > 0 and first_qpc is not None:
        out['frame_relative_seconds'] = {
            'source_clock': 'QPC',
            'frequency_hz': fq,
            'origin': 'first parsed frame.capture_begin_qpc',
            'origin_qpc': first_qpc,
            'formula': '(record_qpc - origin_qpc) / frequency_hz',
        }
    se = rec.session_end
    if se is not None and fq > 0 and fnum(se.get('duration_qpc_ticks')) is not None:
        out['session_duration_seconds'] = {
            'source': 'session_end.duration_qpc_ticks',
            'frequency_hz': fq,
            'formula': 'duration_qpc_ticks / qpc_frequency',
        }
    return out


def source_entry(rec: ParsedRecording, source_id: str) -> dict[str, Any]:
    first, last = frame_serial_range(rec.frames)
    return {
        'source_id': source_id,
        'input_file': rec.path.name,
        'recorder_declared_file': rec.recorder_declared_file,
        'sha256': rec.source_sha256,
        'hash_basis': HASH_BASIS_EXACT_INPUT_FILE_BYTES,
        'size_bytes': _file_size(rec.path),
        'telemetry_schema_version': rec.schema_version,
        'build_commit': rec.build_commit,
        'frame_count': len(rec.frames),
        'first_prepared_serial': first,
        'last_prepared_serial': last,
        'finalised': rec.finalised,
        'parse_health_status': rec.parse_health_status,
        'timebases': source_timebases(rec),
    }


def diagnostic_parameters(args: argparse.Namespace) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for key in DIAGNOSTIC_PARAMETER_KEYS:
        if hasattr(args, key):
            out[key] = getattr(args, key)
    return out


def _value_shape(value: Any) -> str:
    if isinstance(value, bool):
        return 'boolean'
    if isinstance(value, int):
        return 'integer'
    if isinstance(value, float):
        return 'number'
    if isinstance(value, str):
        return 'string'
    if value is None:
        return 'null'
    if isinstance(value, list):
        if not value:
            return 'array'
        if all(isinstance(x, (int, float)) and not isinstance(x, bool)
               for x in value):
            return f'number[{len(value)}]' if len(value) in (2, 3, 4) else 'array<number>'
        if all(isinstance(x, str) for x in value):
            return 'array<string>'
        if all(isinstance(x, dict) for x in value):
            return 'array<object>'
        return 'array'
    if isinstance(value, dict):
        return 'object'
    return 'unknown'


_PATH_CONCAT_CACHE: dict[tuple[str, str], str] = {}
_VECTOR_SHAPE = {2: 'number[2]', 3: 'number[3]', 4: 'number[4]'}


def _child_path(parent: str, key: str) -> str:
    cached = _PATH_CONCAT_CACHE.get((parent, key))
    if cached is None:
        if key == '[]':
            cached = parent + '[]'
        else:
            cached = f'{parent}.{key}' if parent else key
        _PATH_CONCAT_CACHE[(parent, key)] = cached
    return cached


def _register_leaf(shapes: dict[str, set], instances: dict[str, int],
    path: str, shape: str, count: int = 1) -> None:
    existing = shapes.get(path)
    if existing is None:
        shapes[path] = {shape}
        instances[path] = count
    else:
        existing.add(shape)
        instances[path] += count


def _collect_field_paths(value: Any, path: str, shapes: dict[str, set],
    instances: dict[str, int]) -> None:
    if isinstance(value, dict):
        if not value:
            _register_leaf(shapes, instances, path, 'object')
            return
        for key, child_value in value.items():
            if type(child_value) is dict:
                _collect_field_paths(child_value, _child_path(path, key),
                    shapes, instances)
            elif type(child_value) is list:
                _collect_field_paths(child_value, _child_path(path, key),
                    shapes, instances)
            elif key[:1] != '_':
                _register_leaf(shapes, instances, _child_path(path, key),
                    _value_shape(child_value))
        return
    if isinstance(value, list):
        if not value:
            _register_leaf(shapes, instances, path, 'array')
            return
        if all(isinstance(x, (int, float)) and not isinstance(x, bool)
               for x in value) and len(value) in (2, 3, 4):
            _register_leaf(shapes, instances, path, _VECTOR_SHAPE[len(value)])
            return
        if all(isinstance(x, dict) for x in value):
            array_path = _child_path(path, '[]')
            for element in value:
                _collect_field_paths(element, array_path, shapes, instances)
            return
        _register_leaf(shapes, instances, _child_path(path, '[]'),
            _value_shape(value), len(value))
        return
    _register_leaf(shapes, instances, path, _value_shape(value))


def build_field_inventory(
    records_by_type: dict[str, list[dict[str, Any]]]) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    for record_type in sorted(records_by_type):
        accumulated: dict[str, dict[str, Any]] = {}
        for record in records_by_type[record_type]:
            shapes: dict[str, set] = {}
            instances: dict[str, int] = {}
            _collect_field_paths(record, '', shapes, instances)
            for path, path_shapes in shapes.items():
                row = accumulated.setdefault(path, {
                    'shapes': set(), 'records_present': 0, 'instances': 0})
                row['shapes'].update(path_shapes)
                row['records_present'] += 1
                row['instances'] += instances.get(path, 0)
        for path in sorted(accumulated):
            row = accumulated[path]
            sorted_shapes = sorted(row['shapes'])
            entry = {
                'record_type': record_type,
                'path': path,
                'value_shape': sorted_shapes[0] if len(sorted_shapes) == 1
                    else sorted_shapes,
                'records_present': row['records_present'],
                'instances': row['instances'],
            }
            entries.append(entry)
    return entries


def apply_known_validity(entries: list[dict[str, Any]],
    records_by_type: dict[str, list[dict[str, Any]]]) -> None:
    for record_type, payload_path, flag_path in KNOWN_VALIDITY_PAIRS:
        records = records_by_type.get(record_type, [])
        if not records:
            continue
        true_count = sum(1 for r in records if _get_path(r, flag_path) is True)
        false_count = sum(1 for r in records if _get_path(r, flag_path) is False)
        if true_count + false_count == 0:
            continue
        missing_count = len(records) - true_count - false_count
        for entry in entries:
            if entry['record_type'] != record_type:
                continue
            path = entry['path']
            if path == flag_path:
                continue
            if not (path == payload_path or path.startswith(payload_path + '.')
                    or path.startswith(payload_path + '[]')):
                continue
            validity: dict[str, Any] = {
                'flag_path': flag_path,
                'true_count': true_count,
                'false_count': false_count,
                'pairing_basis': 'known_schema_mapping',
            }
            if missing_count:
                validity['flag_missing_count'] = missing_count
            entry['validity'] = validity


def _record_locator(record: dict[str, Any]) -> dict[str, Any]:
    locator: dict[str, Any] = {'line': record.get('_line')}
    serial = record.get('prepared_serial')
    if serial is not None:
        locator['prepared_serial'] = serial
    return locator


def structural_runs(records: list[dict[str, Any]], record_type: str,
    field_path: str) -> tuple[list[dict[str, Any]], int, bool]:
    runs: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    for record in records:
        value = _get_path(record, field_path)
        if value is _MISSING:
            if current is not None:
                runs.append(current)
                current = None
            continue
        key = (type(value).__name__, value)
        if current is not None and current['_key'] == key:
            current['end'] = _record_locator(record)
            continue
        if current is not None:
            runs.append(current)
        current = {'_key': key, 'value': value,
            'start': _record_locator(record), 'end': _record_locator(record)}
    if current is not None:
        runs.append(current)
    total = len(runs)
    truncated = total > MAX_STRUCTURAL_RUNS
    out = []
    for run in runs[:MAX_STRUCTURAL_RUNS]:
        out.append({
            'record_type': record_type,
            'field_path': field_path,
            'value': run['value'],
            'start': run['start'],
            'end': run['end'],
        })
    return out, total, truncated


def build_event_inventory(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    counter: collections.Counter = collections.Counter()
    for record in records:
        record_type = str(record.get('type'))
        if record_type in ('session_start', 'session_end', 'frame'):
            continue
        kind = record.get('kind')
        event = record.get('event', record.get('name'))
        provider = record.get('provider', record.get('channel'))
        status = record.get('status')
        counter[(record_type,
            None if kind is None else str(kind),
            None if event is None else str(event),
            None if provider is None else str(provider),
            None if status is None else str(status))] += 1
    out = []
    for key in sorted(counter):
        record_type, kind, event, provider, status = key
        row: dict[str, Any] = {'record_type': record_type, 'count': counter[key]}
        if kind is not None:
            row['kind'] = kind
        if event is not None:
            row['event'] = event
        if provider is not None:
            row['provider'] = provider
        if status is not None:
            row['status'] = status
        out.append(row)
    return out


def _declared_registry(session_start: dict[str, Any], key: str
    ) -> tuple[list[dict[str, Any]], str]:
    """Copy a recorder-declared enum registry faithfully; never substitute
    analyser knowledge for an absent declaration."""
    mapping = session_start.get(key)
    if not isinstance(mapping, list):
        return [], 'absent_from_session_start'
    rows = []
    for row in mapping:
        if isinstance(row, dict):
            rows.append({str(k): row[k] for k in sorted(row)
                if not str(k).startswith('_')})
    return rows, 'recorder_declared'


def _explicit_settings_events(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    out = []
    for record in records:
        event = str(record.get('event', record.get('name', ''))).lower()
        provider = str(record.get('provider', record.get('channel', '')))
        if event in ('settings.changed', 'setting_changed') or (
                'setting' in event and provider in ('settings', 'config', 'menu')):
            out.append(_record_locator(record))
    return out


def build_manifest(rec: ParsedRecording) -> dict[str, Any]:
    ss = rec.session_start
    se = rec.session_end
    frames = rec.frames
    all_records = [ss] + frames + list(rec.other_records)
    if se is not None:
        all_records.append(se)
    records_by_type: dict[str, list[dict[str, Any]]] = collections.defaultdict(list)
    for record in all_records:
        records_by_type[str(record.get('type'))].append(record)

    inventory = build_field_inventory(records_by_type)
    apply_known_validity(inventory, records_by_type)

    fq = int(ss.get('qpc_frequency', 0) or 0)
    first_qpc = _first_frame_qpc(frames)
    last_qpc = _last_frame_qpc(frames)
    frame_span = None
    if fq > 0 and first_qpc is not None and last_qpc is not None:
        frame_span = (last_qpc - first_qpc) / fq
    session_duration = None
    if fq > 0 and se is not None and fnum(se.get('duration_qpc_ticks')) is not None:
        session_duration = fnum(se.get('duration_qpc_ticks')) / fq
    first_serial, last_serial = frame_serial_range(frames)
    weapon_events = records_by_type.get('weapon_event', [])

    accounting_checks = None
    if se is not None:
        accounting_checks = {
            'written_equals_parsed_frames': se.get('written') == len(frames),
            'weapon_events_written_equals_parsed_weapon_events':
                se.get('weapon_events_written') == len(weapon_events),
        }
        if frames:
            accounting_checks['first_written_serial_matches_first_parsed_frame'] = (
                se.get('first_prepared_serial_written') == first_serial)
            accounting_checks['last_written_serial_matches_last_parsed_frame'] = (
                se.get('last_prepared_serial_written') == last_serial)

    profile_runs, profile_total, profile_truncated = structural_runs(
        records_by_type.get('frame', []), 'frame', 'test_profile_id')
    if profile_truncated:
        profile_runs.append({'record_type': 'frame',
            'field_path': 'test_profile_id', 'truncated': True,
            'run_count': profile_total})

    structural: list[dict[str, Any]] = []
    for record_type, field_path in STRUCTURAL_RUN_INDEXES:
        runs, total, truncated = structural_runs(
            records_by_type.get(record_type, []), record_type, field_path)
        structural.extend(runs)
        if truncated:
            structural.append({'record_type': record_type,
                'field_path': field_path, 'truncated': True,
                'run_count': total})

    declared_semantics = {key: ss[key] for key in DECLARED_SEMANTIC_KEYS
        if key in ss}
    test_profiles, test_profile_declaration = _declared_registry(
        ss, 'test_profile_enum_mapping')
    hybrid_overrides, hybrid_declaration = _declared_registry(
        ss, 'hybrid_diagnostic_override_enum_mapping')

    return {
        'manifest_schema_version': MANIFEST_SCHEMA_VERSION,
        'kind': 'mccvr.telemetry.manifest' if rec.finalised
            else 'mccvr.telemetry.manifest.provisional',
        'provisional': not rec.finalised,
        'generator': generator_identity(),
        'source': {
            'input_file': rec.path.name,
            'recorder_declared_file': rec.recorder_declared_file,
            'sha256': rec.source_sha256,
            'hash_basis': HASH_BASIS_EXACT_INPUT_FILE_BYTES,
            'size_bytes': _file_size(rec.path),
            'line_count': rec.line_count,
            'telemetry_schema_version': rec.schema_version,
            'build_commit': rec.build_commit,
            'first_record_type': rec.first_record_type,
            'last_record_type': rec.last_record_type,
        },
        'parse_health': {
            'status': rec.parse_health_status,
            'parsed_record_count': len(all_records),
            'malformed_middle_line_count': len(rec.malformed_middle_lines),
            'malformed_middle_lines': [x['line'] for x in rec.malformed_middle_lines],
            'trailing_partial': rec.trailing_partial,
            'trailing_partial_line': rec.trailing_partial_line,
        },
        'capture': {
            'frame_count': len(frames),
            'first_prepared_serial': first_serial,
            'last_prepared_serial': last_serial,
            'serial_gap_count': _serial_gap_count(frames),
            'first_frame_line': frames[0]['_line'] if frames else None,
            'last_frame_line': frames[-1]['_line'] if frames else None,
            'frame_span_seconds': frame_span,
            'session_duration_seconds': session_duration,
            'session_start_present': True,
            'session_end_present': se is not None,
            'source_finalised': rec.finalised,
            'clean_stop': bool(se.get('clean_stop')) if se is not None else None,
            'raw_session_end_accounting': None if se is None else {
                str(k): v for k, v in se.items()
                if str(k) not in ('type', '_line')},
            'accounting_checks': accounting_checks,
        },
        'recorder_runtime': {key: ss[key] for key in (
            'start_wall_clock_utc', 'process_id', 'qpc_frequency', 'ring_slots',
            'ring_usable_capacity', 'drop_policy', 'worker_poll_ms')
            if key in ss},
        'timebases': source_timebases(rec),
        'declared_semantics': declared_semantics,
        'registries': {
            'test_profiles': test_profiles,
            'test_profiles_declaration': test_profile_declaration,
            'hybrid_diagnostic_overrides': hybrid_overrides,
            'hybrid_diagnostic_overrides_declaration': hybrid_declaration,
        },
        'record_inventory': [{'type': t, 'count': len(records_by_type[t])}
            for t in sorted(records_by_type)],
        'event_inventory': build_event_inventory(rec.other_records),
        'field_inventory': inventory,
        'direct_observation_runs': {
            'profile_runs': profile_runs,
            'explicit_settings_events': _explicit_settings_events(
                rec.other_records),
        },
        'structural_runs': structural,
    }


def annotate_locators(value: Any, source_id: str,
    record_type: str = 'frame') -> None:
    if isinstance(value, dict):
        line = value.get('line')
        if isinstance(line, int) and not isinstance(line, bool):
            value.setdefault('source_id', source_id)
            value.setdefault('record_type', record_type)
        else:
            for key in ('start_line', 'end_line'):
                span = value.get(key)
                if isinstance(span, int) and not isinstance(span, bool):
                    value.setdefault('source_id', source_id)
                    value.setdefault('record_type', record_type)
                    break
        for child in value.values():
            annotate_locators(child, source_id, record_type)
    elif isinstance(value, (list, tuple)):
        for child in value:
            annotate_locators(child, source_id, record_type)


def build_diagnostics_document(rec: ParsedRecording, result: dict[str, Any],
    args: argparse.Namespace, context_dependency: dict[str, Any] | None = None
    ) -> dict[str, Any]:
    source_ids = assign_source_ids([rec.source_sha256])
    source_id = source_ids[rec.source_sha256]
    doc = dict(result)
    annotate_locators(doc, source_id)
    doc['diagnostics_schema_version'] = DIAGNOSTICS_SCHEMA_VERSION
    doc['kind'] = 'mccvr.telemetry.diagnostics'
    doc['provisional'] = not rec.finalised
    doc['generator'] = generator_identity()
    doc['scope'] = {'kind': 'single_capture',
        'sources': [source_entry(rec, source_id)]}
    doc['parameters'] = diagnostic_parameters(args)
    doc['signal_definitions'] = SIGNAL_DEFINITIONS
    doc['methods'] = DIAGNOSTIC_METHOD_CATALOG
    doc['locator_convention'] = LOCATOR_CONVENTION
    doc['context_dependency'] = context_dependency
    doc['disclaimer'] = DIAGNOSTIC_DISCLAIMER
    return doc


def build_combined_diagnostics_document(
    items: list[tuple[ParsedRecording, dict[str, Any]]],
    combined: dict[str, Any], args: argparse.Namespace,
    context_dependency: dict[str, Any] | None = None) -> dict[str, Any]:
    source_ids = assign_source_ids(rec.source_sha256 for rec, _ in items)
    sources = [source_entry(rec, source_ids[rec.source_sha256])
        for rec, _ in items]
    doc = dict(combined)
    for run in doc.get('runs', []):
        if isinstance(run, dict):
            source_id = source_ids.get(str(run.get('sha256')))
            if source_id is not None:
                run['source_id'] = source_id
    by_sha = {rec.source_sha256: source_ids[rec.source_sha256]
        for rec, _ in items}
    for group in doc.get('profile_condition_groups', []):
        for observation in group.get('observations', []):
            source_id = by_sha.get(str(observation.get('sha256')))
            if source_id is not None:
                observation['source_id'] = source_id
    doc['diagnostics_schema_version'] = DIAGNOSTICS_SCHEMA_VERSION
    doc['kind'] = 'mccvr.telemetry.diagnostics'
    doc['provisional'] = any(not rec.finalised for rec, _ in items)
    doc['generator'] = generator_identity()
    doc['scope'] = {
        'kind': 'multi_capture' if len(items) > 1 else 'single_capture',
        'sources': sources}
    doc['parameters'] = diagnostic_parameters(args)
    doc['signal_definitions'] = SIGNAL_DEFINITIONS
    doc['methods'] = DIAGNOSTIC_METHOD_CATALOG
    doc['locator_convention'] = LOCATOR_CONVENTION
    doc['context_dependency'] = context_dependency
    doc['disclaimer'] = DIAGNOSTIC_DISCLAIMER
    return doc


def _diagnostics_header_markdown(doc: dict[str, Any]) -> str:
    generator = doc.get('generator', {})
    scope = doc.get('scope', {})
    sources = scope.get('sources', [])
    lines = [
        f"# MCCVR telemetry diagnostics — {scope.get('kind', 'capture')}",
        '',
        f"- Generator: `{generator.get('name')}` {generator.get('version')} / SHA-256 `{generator.get('sha256')}`",
        f"- Diagnostics schema: `{doc.get('diagnostics_schema_version')}`",
        f"- Scope: `{scope.get('kind')}`; provisional: `{doc.get('provisional')}`",
        f"- Locator convention: source id `{LOCATOR_CONVENTION['source_id_field']}`, line `{LOCATOR_CONVENTION['line_field']}`, relative time `{LOCATOR_CONVENTION['relative_time_field']}` on basis `{LOCATOR_CONVENTION['relative_time_basis']}`",
        '',
        '## Sources',
        '',
        '| Source id | Input file | Recorder-declared | Schema | SHA-256 | Finalised | Parse health | Frame relative timebase origin QPC |',
        '|---|---|---|---:|---|---|---|---:|',
    ]
    for source in sources:
        timebase = (source.get('timebases') or {}).get('frame_relative_seconds') or {}
        lines.append(
            f"| `{source.get('source_id')}` | `{source.get('input_file')}` | "
            f"`{source.get('recorder_declared_file') or '-'}` | "
            f"{source.get('telemetry_schema_version')} | `{source.get('sha256')}` | "
            f"{source.get('finalised')} | {source.get('parse_health_status')} | "
            f"{timebase.get('origin_qpc')} |")
    lines += ['', '## Recorded parameters', '',
        '`' + json.dumps(doc.get('parameters', {}), sort_keys=True,
            separators=(',', ':')) + '`']
    if doc.get('context_dependency'):
        context = doc['context_dependency']
        lines += ['',
            f"Experiment context dependency: `{context.get('file')}` SHA-256 "
            f"`{context.get('sha256')}`; used for "
            f"`{json.dumps(context.get('used_for', []))}`."]
    lines += ['', '## Signal definitions', '',
        '| Alias | Raw path | Validity | Meaning |', '|---|---|---|---|']
    for name in sorted(doc.get('signal_definitions', {})):
        entry = doc['signal_definitions'][name]
        binding = (entry.get('bindings') or [{}])[0]
        lines.append(
            f"| `{name}` | `{binding.get('raw_path', '-')}` | "
            f"`{binding.get('validity_path') or '-'}` | "
            f"{entry.get('description', '')} |")
    lines += ['', '## Method catalog', '',
        '| Method | Classification | Definition | Parameters |',
        '|---|---|---|---|']
    for name in sorted(doc.get('methods', {})):
        entry = doc['methods'][name]
        lines.append(
            f"| `{name}` | `{entry.get('classification')}` | "
            f"{entry.get('description', '')} | "
            f"`{json.dumps(entry.get('parameters', []))}` |")
    return '\n'.join(lines)


def diagnostics_markdown(doc: dict[str, Any], args: argparse.Namespace) -> str:
    header = _diagnostics_header_markdown(doc)
    body = md_v5(doc, args)
    for old, new in (
            ('# MCCVR Telemetry Analysis v3 — ', '# MCCVR telemetry diagnostics — '),
            ('# V5.2 profile provenance', '# Temporary-profile provenance'),
            ('# V4 concern-mining additions', '# Concern-mining diagnostics'),
            ('## V4 guardrails', '## Diagnostics guardrails'),
            ('# V5.2 authority / query additions',
             '# Authority, query and performance indexes')):
        body = body.replace(old, new)
    out = [header, '', '---', '', body, '', '---', '',
        '## Diagnostics disclaimer', '', doc.get('disclaimer', DIAGNOSTIC_DISCLAIMER), '',
        '## Raw navigation', '',
        'Raw evidence windows are selected by the investigator, not by the analyser: '
        '`--raw-window-serial N --before B --after A`, `--raw-range-serial FIRST:LAST`, '
        '`--raw-range-lines FIRST:LAST`, optional `--raw-record-type TYPE` and '
        '`--raw-output PATH` return original raw records without reinterpretation.', '']
    return '\n'.join(out) + '\n'


def combined_diagnostics_markdown(doc: dict[str, Any]) -> str:
    header = _diagnostics_header_markdown(doc)
    body = combined_markdown(doc).replace(
        '# MCCVR Telemetry Combined Comparison v5.2',
        '# Consolidated comparison').replace(
        '## V4 same-profile concern metrics',
        '## Same-profile concern metrics')
    return header + '\n\n---\n\n' + body + '\n' + doc.get(
        'disclaimer', DIAGNOSTIC_DISCLAIMER) + '\n'


def _raw_frame_serials(path: Path) -> list[int]:
    serials: list[int] = []
    with path.open('r', encoding='utf-8') as handle:
        for line in handle:
            stripped = line.strip()
            if not stripped or '"frame"' not in stripped:
                continue
            try:
                obj = json.loads(stripped)
            except json.JSONDecodeError:
                continue
            if not isinstance(obj, dict) or obj.get('type') != 'frame':
                continue
            serial = fnum(obj.get('prepared_serial'))
            if serial is not None:
                serials.append(int(serial))
    return serials


def check_manifest_binding(recording: Path, manifest: dict[str, Any]
    ) -> tuple[bool, dict[str, Any]]:
    source = manifest.get('source') if isinstance(manifest, dict) else None
    if not isinstance(source, dict):
        return False, {'reason': 'manifest has no source binding'}
    try:
        actual_sha = sha256_file(recording)
        actual_size = int(recording.stat().st_size)
        serials = _raw_frame_serials(recording)
    except OSError as exc:
        return False, {'reason': f'could not read recording: {exc}'}
    capture = manifest.get('capture') if isinstance(manifest, dict) else None
    if not isinstance(capture, dict):
        capture = {}
    checks = {
        'sha256_matches': str(source.get('sha256')) == actual_sha,
        'size_bytes_matches': source.get('size_bytes') == actual_size,
        'manifest_kind_known': str(manifest.get('kind')) in (
            'mccvr.telemetry.manifest',
            'mccvr.telemetry.manifest.provisional'),
        'frame_count_matches': capture.get('frame_count') == len(serials),
        'first_serial_matches': capture.get('first_prepared_serial') == (
            serials[0] if serials else None),
        'last_serial_matches': capture.get('last_prepared_serial') == (
            serials[-1] if serials else None),
    }
    details = {
        'checks': checks,
        'expected_sha256': source.get('sha256'),
        'actual_sha256': actual_sha,
        'expected_size_bytes': source.get('size_bytes'),
        'actual_size_bytes': actual_size,
        'expected_frame_count': capture.get('frame_count'),
        'actual_frame_count': len(serials),
    }
    return all(checks.values()), details


def run_verify_manifest(recording: Path, manifest_path: Path) -> int:
    try:
        manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    except (OSError, json.JSONDecodeError) as exc:
        print(json.dumps({'match': False,
            'reason': f'could not read manifest: {exc}'}, indent=2,
            sort_keys=True))
        return 1
    match, details = check_manifest_binding(recording, manifest)
    print(json.dumps({'match': match, 'recording': str(recording),
        'manifest': str(manifest_path), **details}, indent=2, sort_keys=True))
    return 0 if match else 1


def _parse_int_range(text: str) -> tuple[int, int]:
    parts = str(text).split(':')
    if len(parts) != 2:
        raise ValueError(f'range must be FIRST:LAST, got {text!r}')
    first, last = int(parts[0]), int(parts[1])
    if first > last:
        raise ValueError(f'range start exceeds end: {text!r}')
    return first, last


def raw_select_lines(path: Path, args: argparse.Namespace
    ) -> list[str]:
    serial_lo: int | None = None
    serial_hi: int | None = None
    line_lo: int | None = None
    line_hi: int | None = None
    if args.raw_window_serial is not None:
        before = max(0, int(args.before))
        after = max(0, int(args.after))
        serial_lo = int(args.raw_window_serial) - before
        serial_hi = int(args.raw_window_serial) + after
    elif args.raw_range_serial:
        serial_lo, serial_hi = _parse_int_range(args.raw_range_serial)
    elif args.raw_range_lines:
        line_lo, line_hi = _parse_int_range(args.raw_range_lines)
    type_filter = {str(x) for x in (args.raw_record_type or [])}
    out: list[str] = []
    with path.open('r', encoding='utf-8', newline='') as f:
        for line_no, line in enumerate(f, 1):
            if line_lo is not None and not (line_lo <= line_no <= line_hi):
                continue
            stripped = line.strip()
            if not stripped:
                continue
            try:
                obj = json.loads(stripped)
            except json.JSONDecodeError:
                continue
            if not isinstance(obj, dict):
                continue
            if type_filter and str(obj.get('type', '')) not in type_filter:
                continue
            if serial_lo is not None:
                serial = fnum(obj.get('prepared_serial'))
                if serial is None or not (serial_lo <= int(serial) <= serial_hi):
                    continue
            out.append(line)
    return out


def run_raw_query(args: argparse.Namespace,
    parser: argparse.ArgumentParser) -> int:
    if len(args.recordings) != 1:
        parser.error('raw navigation requires exactly one recording')
    modes = sum(1 for selected in (args.raw_window_serial is not None,
        bool(args.raw_range_serial), bool(args.raw_range_lines)) if selected)
    if modes != 1:
        parser.error('choose exactly one of --raw-window-serial, '
            '--raw-range-serial, --raw-range-lines')
    try:
        selected = raw_select_lines(args.recordings[0], args)
    except (OSError, ValueError) as exc:
        parser.error(f'raw navigation failed: {exc}')
    text = ''.join(selected)
    if args.raw_output:
        _write_raw_text(args.raw_output, text)
        # Status goes to stderr so redirected stdout stays byte-clean.
        print('Wrote', args.raw_output, file=sys.stderr)
    else:
        _write_stdout_text(text)
    return 0


def load_experiment_context(path: Path | None) -> dict[str, Any] | None:
    if path is None:
        return None
    if not path.is_file():
        raise ValueError(f'experiment context file not found: {path}')
    return {
        'file': path.name,
        'sha256': sha256_file(path),
        'hash_basis': HASH_BASIS_EXACT_INPUT_FILE_BYTES,
        'used_for': [],
        'effect': 'disclosed_only_no_method_consumed_it',
    }


def build_argument_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        description='MCCVR telemetry analyser: neutral manifest, diagnostics and raw retrieval')
    add_args(ap)
    ap.add_argument('--query', help='Print one concise deterministic query result; use list')
    ap.add_argument('--self-test', action='store_true')
    ap.add_argument('--include-debug-derived-rows', action='store_true',
        help='Include per-frame derived rows (for example actual horizontal-release field rows) in diagnostics. Off by default so diagnostics do not duplicate the raw dataset.')
    ap.add_argument('--experiment-context', type=Path,
        help='Optional EXPERIMENT_CONTEXT.md. Disclosed as a hashed dependency; it never changes the neutral manifest.')
    ap.add_argument('--verify-manifest', type=Path,
        help='Verify a manifest binding against the positional recording.')
    ap.add_argument('--raw-window-serial', type=int,
        help='Return raw records around one prepared serial; use --before/--after for context.')
    ap.add_argument('--before', type=int, default=20)
    ap.add_argument('--after', type=int, default=20)
    ap.add_argument('--raw-range-serial',
        help='Return raw records with prepared_serial in FIRST:LAST.')
    ap.add_argument('--raw-range-lines',
        help='Return raw records by 1-based physical line range FIRST:LAST.')
    ap.add_argument('--raw-record-type', action='append',
        help='Restrict raw retrieval to one record type (repeatable).')
    ap.add_argument('--raw-output', type=Path,
        help='Write retrieved raw records here instead of stdout.')
    return ap


def _fixture_cli_args(*argv: str) -> argparse.Namespace:
    return build_argument_parser().parse_args(list(argv))


def _iter_locators(value: Any):
    if isinstance(value, dict):
        if 'source_id' in value and ('line' in value or 'start_line' in value):
            yield value
        for child in value.values():
            yield from _iter_locators(child)
    elif isinstance(value, (list, tuple)):
        for child in value:
            yield from _iter_locators(child)


def _iter_keys(value: Any):
    if isinstance(value, dict):
        for key, child in value.items():
            yield str(key)
            yield from _iter_keys(child)
    elif isinstance(value, (list, tuple)):
        for child in value:
            yield from _iter_keys(child)


def _sidecar_fixture_rows(include_session_end: bool = True,
    profile_transition: bool = True, qpc_origin: int = 2776727698384,
    build_commit: str = 'sidecar-fixture',
    frame_count: int = 4) -> list[dict[str, Any]]:
    mapping = [
        {'id': 0, 'name': 'Custom', 'role': 'none',
         'uses_custom_settings': True, 'horizontal_release_enabled': False},
        {'id': 1, 'name': 'A_VsOffControl', 'role': 'vs_off_control',
         'uses_custom_settings': False, 'horizontal_release_enabled': False},
        {'id': 2, 'name': 'B_FixedHeadControl', 'role': 'fixed_head_control',
         'uses_custom_settings': False, 'horizontal_release_enabled': False},
        {'id': 3, 'name': 'C_FixedShoulderControl',
         'role': 'fixed_shoulder_control', 'uses_custom_settings': False,
         'horizontal_release_enabled': False},
    ]
    rows: list[dict[str, Any]] = [{
        'type': 'session_start', 'schema_version': 2,
        'build_commit': build_commit, 'qpc_frequency': 10000000,
        'file': 'HaloMCCVR-Telemetry-20260923-153031-756.jsonl',
        'start_wall_clock_utc': '2026-09-23T15:30:31.756Z',
        'process_id': 7500, 'ring_slots': 4096, 'ring_usable_capacity': 4095,
        'drop_policy': 'drop_new', 'worker_poll_ms': 10,
        'tracking_space': 'OpenXR LOCAL', 'position_units': 'metres',
        'quaternion_order': 'x,y,z,w',
        'coordinate_handedness': 'OpenXR right-handed', 'up_axis': '+Y',
        'forward_axis': '-Z', 'test_profile_enum_mapping': mapping,
        'hybrid_diagnostic_override_enum_mapping': [
            {'id': 0, 'name': 'Normal'},
            {'id': 1, 'name': 'ForceHip'},
            {'id': 2, 'name': 'ForceStockNew'}]}]
    for index in range(frame_count):
        frame = _fixture_frame(index + 10, 0, 'Custom')
        frame['schema_version'] = 2
        frame['prepared_serial'] = index + 10
        frame['capture_begin_qpc'] = qpc_origin + index * 1000000
        frame['capture_end_qpc'] = frame['capture_begin_qpc'] + 8
        if profile_transition and index >= 2:
            frame['test_profile_id'] = 1
            frame['test_profile_name'] = 'A_VsOffControl'
        frame['menu_open'] = index >= 3
        frame['openxr_session_state'] = index >= 1
        frame['contact_space_epoch'] = 1 if index < 2 else 9
        frame['headset_smoothing'] = 0.25
        frame['views'] = [
            {'pose': {'position': [0.0, 1.6, -0.031],
                      'orientation': [0.0, 0.0, 0.0, 1.0]},
             'fov': [-0.8, 0.8, 0.8, -0.8]},
            {'pose': {'position': [0.0, 1.6, -0.031],
                      'orientation': [0.0, 0.0, 0.0, 1.0]},
             'fov': [-0.8, 0.8, 0.8, -0.8]},
        ]
        if index == 0:
            frame['semantic_primary_aim_valid'] = False
        rows.append(frame)
    rows.append({'type': 'weapon_event', 'seq': 1, 'session': 1, 'qpc': 123,
        'thread_id': 1, 'kind': 'fp_entry', 'status': 'no_observation',
        'title': 1, 'title_generation': 1, 'prepared_serial': 10,
        'controlled_unit': 0, 'primary_weapon': 0, 'aux0': 0, 'aux1': 0})
    rows.append({'type': 'weapon_event', 'seq': 2, 'session': 1, 'qpc': 124,
        'thread_id': 1, 'kind': 'fp_weapon_commit', 'status': 'success',
        'title': 1, 'title_generation': 1, 'prepared_serial': 10,
        'controlled_unit': 4660, 'primary_weapon': 11, 'aux0': 0, 'aux1': 0})
    rows.append({'type': 'event_gap', 'first_missing_seq': 3,
        'last_missing_seq': 3})
    rows.append({'type': 'future_event', 'provider': 'future',
        'event': 'some_future_kind', 'status': 'ok'})
    if include_session_end:
        rows.append({'type': 'session_end', 'schema_version': 2,
            'clean_stop': True, 'stop_reason': 'user',
            'duration_qpc_ticks': 4000000, 'producer_calls': 4,
            'duplicate_serial_suppressed': 0, 'enqueued': 4, 'written': 4,
            'dropped_queue_full': 0, 'writer_failures': 0,
            'first_prepared_serial_written': 10,
            'last_prepared_serial_written': 13, 'weapon_events_enqueued': 2,
            'weapon_events_written': 2,
            'weapon_events_dropped_queue_full': 0,
            'weapon_events_session_skipped': 0})
    return rows


def _write_fixture(path: Path, rows: list[dict[str, Any]]) -> None:
    path.write_text('\n'.join(json.dumps(row) for row in rows) + '\n',
        encoding='utf-8')


def run_sidecar_self_test() -> int:
    passed: list[str] = []
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        src = root / 'HaloMCCVR-Telemetry-20260923-153031-756.jsonl'
        _write_fixture(src, _sidecar_fixture_rows())
        rec = parse_jsonl(src)
        assert rec.finalised and rec.parse_health_status == 'clean'
        manifest = build_manifest(rec)
        assert manifest['manifest_schema_version'] == MANIFEST_SCHEMA_VERSION
        assert manifest['kind'] == 'mccvr.telemetry.manifest'
        assert manifest['source']['sha256'] == rec.source_sha256 == sha256_file(src)
        assert manifest['source']['hash_basis'] == HASH_BASIS_EXACT_INPUT_FILE_BYTES
        assert manifest['source']['recorder_declared_file'] == (
            'HaloMCCVR-Telemetry-20260923-153031-756.jsonl')
        assert manifest['source']['line_count'] == 10
        assert manifest['capture']['frame_count'] == 4
        assert manifest['capture']['first_prepared_serial'] == 10
        assert manifest['capture']['last_prepared_serial'] == 13
        assert manifest['capture']['serial_gap_count'] == 0
        assert manifest['capture']['first_frame_line'] == 2
        assert manifest['capture']['last_frame_line'] == 5
        assert manifest['capture']['frame_span_seconds'] == 0.3
        assert manifest['capture']['session_duration_seconds'] == 0.4
        assert manifest['capture']['accounting_checks']['written_equals_parsed_frames']
        assert manifest['timebases']['frame_relative_seconds']['origin_qpc'] == 2776727698384
        assert manifest['timebases']['frame_relative_seconds']['frequency_hz'] == 10000000
        assert manifest['record_inventory'] == [
            {'type': 'event_gap', 'count': 1},
            {'type': 'frame', 'count': 4},
            {'type': 'future_event', 'count': 1},
            {'type': 'session_end', 'count': 1},
            {'type': 'session_start', 'count': 1},
            {'type': 'weapon_event', 'count': 2}]
        event_inventory = {(row['record_type'], row.get('kind'),
            row.get('status')): row['count']
            for row in manifest['event_inventory']}
        assert event_inventory[('weapon_event', 'fp_weapon_commit', 'success')] == 1
        assert event_inventory[('event_gap', None, None)] == 1
        future_row = next(row for row in manifest['event_inventory']
            if row['record_type'] == 'future_event')
        assert future_row['provider'] == 'future'
        assert future_row['event'] == 'some_future_kind'
        assert future_row['status'] == 'ok'
        registries = manifest['registries']
        assert registries['test_profiles_declaration'] == 'recorder_declared'
        assert registries['hybrid_diagnostic_overrides_declaration'] == (
            'recorder_declared')
        assert registries['hybrid_diagnostic_overrides'] == [
            {'id': 0, 'name': 'Normal'},
            {'id': 1, 'name': 'ForceHip'},
            {'id': 2, 'name': 'ForceStockNew'}]
        inventory = {(row['record_type'], row['path']): row
            for row in manifest['field_inventory']}
        views = inventory[('frame', 'views[].pose.position')]
        assert views['records_present'] == 4 and views['instances'] == 8
        assert views['value_shape'] == 'number[3]'
        assert not any('[0]' in row['path'] or '[1]' in row['path']
            for row in manifest['field_inventory'])
        primary = inventory[('frame', 'semantic_primary_aim.position')]
        assert primary['validity'] == {
            'flag_path': 'semantic_primary_aim_valid',
            'true_count': 3, 'false_count': 1,
            'pairing_basis': 'known_schema_mapping'}
        assert 'validity' not in inventory[('frame', 'headset_smoothing')]
        assert 'validity' not in inventory[('frame', 'test_profile_name')]
        c_direction = inventory[('frame', 'aim_trace.c_direction')]
        assert c_direction['validity']['flag_path'] == 'aim_trace.c_valid'
        profile_runs = manifest['direct_observation_runs']['profile_runs']
        assert [row['value'] for row in profile_runs] == [0, 1]
        assert profile_runs[0]['start'] == {'line': 2, 'prepared_serial': 10}
        assert profile_runs[0]['end'] == {'line': 3, 'prepared_serial': 11}
        assert profile_runs[1]['start']['line'] == 4
        menu_runs = [row for row in manifest['structural_runs']
            if row['field_path'] == 'menu_open']
        assert [row['value'] for row in menu_runs] == [False, True]
        structural_fields = {row['field_path']
            for row in manifest['structural_runs']}
        assert structural_fields <= {
            'menu_open', 'openxr_session_state', 'contact_space_epoch'}
        forbidden = ('interesting', 'anomal', 'severity', 'best',
            'recommend', 'likely', 'winner', 'quality', 'suspicious')
        joined_keys = ' '.join(_iter_keys(manifest)).lower()
        assert not any(word in joined_keys for word in forbidden)
        assert json_text(build_manifest(parse_jsonl(src))) == json_text(manifest)
        passed.append('manifest binding, inventory, validity, runs, neutrality, determinism')

        copy_path = root / 'renamed-copy.jsonl'
        copy_path.write_text(src.read_text(encoding='utf-8'), encoding='utf-8')
        copied = parse_jsonl(copy_path)
        assert copied.source_sha256 == rec.source_sha256
        assert copied.path.name != rec.path.name
        assert build_manifest(copied)['source']['input_file'] == copy_path.name
        flipped_path = root / 'flipped.jsonl'
        flipped_bytes = bytearray(src.read_bytes())
        marker = flipped_bytes.index(b'sidecar-fixture')
        flipped_bytes[marker] = flipped_bytes[marker] ^ 0x01
        flipped_path.write_bytes(bytes(flipped_bytes))
        assert sha256_file(flipped_path) != rec.source_sha256
        assert build_manifest(parse_jsonl(flipped_path))['source']['sha256'] != (
            manifest['source']['sha256'])
        manifest_path = root / 'manifest.json'
        manifest_path.write_text(json_text(manifest), encoding='utf-8')
        match, _ = check_manifest_binding(src, manifest)
        assert match
        mismatch, details = check_manifest_binding(Path(flipped_path), manifest)
        assert not mismatch and not details['checks']['sha256_matches']
        stale_manifest = dict(manifest)
        stale_manifest['capture'] = dict(manifest['capture'])
        stale_manifest['capture']['frame_count'] = 99
        stale_match, stale_details = check_manifest_binding(src, stale_manifest)
        assert not stale_match and not stale_details['checks']['frame_count_matches']
        assert run_verify_manifest(src, manifest_path) == 0
        assert run_verify_manifest(Path(flipped_path), manifest_path) == 1
        ids = assign_source_ids([rec.source_sha256, sha256_file(flipped_path)])
        ids_reordered = assign_source_ids([sha256_file(flipped_path), rec.source_sha256])
        assert ids == ids_reordered and len(set(ids.values())) == 2
        assert ids[rec.source_sha256] == 'sha256:' + rec.source_sha256
        assert assign_source_ids([rec.source_sha256])[rec.source_sha256] == (
            ids[rec.source_sha256])
        passed.append('source binding, rename stability, order-independent source ids')

        novel_src = root / 'novel-field.jsonl'
        novel_rows = _sidecar_fixture_rows()
        novel_rows[1]['novel_future_field'] = {'nested': [1.5, 2.5, 3.5]}
        novel_rows[1]['novel_strings'] = ['alpha', 'beta', 'gamma']
        novel_rows[1]['novel_numbers'] = [1.0, 2.0, 3.0, 4.0, 5.0]
        _write_fixture(novel_src, novel_rows)
        novel_manifest = build_manifest(parse_jsonl(novel_src))
        novel_inventory = {(row['record_type'], row['path']): row
            for row in novel_manifest['field_inventory']}
        assert novel_inventory[('frame', 'novel_future_field.nested')][
            'value_shape'] == 'number[3]'
        assert novel_inventory[('frame', 'novel_future_field.nested')][
            'instances'] == 1
        novel_strings = novel_inventory[('frame', 'novel_strings[]')]
        assert novel_strings['records_present'] == 1
        assert novel_strings['instances'] == 3
        assert novel_strings['value_shape'] == 'array<string>'
        novel_numbers = novel_inventory[('frame', 'novel_numbers[]')]
        assert novel_numbers['records_present'] == 1
        assert novel_numbers['instances'] == 5
        assert novel_numbers['value_shape'] == 'array<number>'
        fixed_vector = inventory[('frame', 'views[].fov')]
        assert fixed_vector['records_present'] == 4
        assert fixed_vector['instances'] == 8
        assert fixed_vector['value_shape'] == 'number[4]'
        object_array = inventory[('frame', 'views[].pose.position')]
        assert object_array['records_present'] == 4
        assert object_array['instances'] == 8
        gap_src = root / 'serial-gap.jsonl'
        gap_rows = _sidecar_fixture_rows()
        del gap_rows[2]
        _write_fixture(gap_src, gap_rows)
        gap_manifest = build_manifest(parse_jsonl(gap_src))
        assert gap_manifest['capture']['serial_gap_count'] == 1
        large_src = root / 'large-capture.jsonl'
        _write_fixture(large_src, _sidecar_fixture_rows(frame_count=40))
        large_manifest = build_manifest(parse_jsonl(large_src))
        assert large_manifest['capture']['frame_count'] == 40
        assert len(large_manifest['field_inventory']) == len(
            manifest['field_inventory'])
        absent_src = root / 'no-declared-registry.jsonl'
        absent_rows = _sidecar_fixture_rows(include_session_end=False)
        absent_rows[0]['schema_version'] = 1
        absent_rows[0].pop('hybrid_diagnostic_override_enum_mapping', None)
        _write_fixture(absent_src, absent_rows)
        absent_manifest = build_manifest(parse_jsonl(absent_src))
        assert absent_manifest['registries']['hybrid_diagnostic_overrides'] == []
        assert absent_manifest['registries'][
            'hybrid_diagnostic_overrides_declaration'] == (
                'absent_from_session_start')
        passed.append('unknown fields, serial gaps and schema-only scaling')

        provisional_src = root / 'open-session.jsonl'
        _write_fixture(provisional_src, _sidecar_fixture_rows(include_session_end=False))
        provisional_rec = parse_jsonl(provisional_src)
        assert not provisional_rec.finalised
        provisional_manifest = build_manifest(provisional_rec)
        assert provisional_manifest['kind'] == 'mccvr.telemetry.manifest.provisional'
        assert provisional_manifest['provisional'] is True
        assert sidecar_manifest_path(provisional_src, True).name == (
            'open-session.provisional.manifest.json')
        assert sidecar_manifest_path(src, False).name == (
            'HaloMCCVR-Telemetry-20260923-153031-756.manifest.json')
        passed.append('finalised versus provisional manifest')

        partial_src = root / 'partial.jsonl'
        partial_rows = _sidecar_fixture_rows()[:2]
        partial_src.write_text(
            '\n'.join(json.dumps(row) for row in partial_rows) + '\n'
            + '{"type":"fra', encoding='utf-8')
        partial_rec = parse_jsonl(partial_src)
        assert partial_rec.parse_health_status == 'recoverable_trailing_partial'
        assert partial_rec.trailing_partial_line == 3
        partial_manifest = build_manifest(partial_rec)
        assert partial_manifest['parse_health']['status'] == (
            'recoverable_trailing_partial')
        assert partial_manifest['parse_health']['trailing_partial_line'] == 3
        corrupt_src = root / 'corrupt.jsonl'
        corrupt_rows = _sidecar_fixture_rows()
        corrupt_lines = [json.dumps(row) for row in corrupt_rows]
        corrupt_lines.insert(1, '{bad}')
        corrupt_src.write_text('\n'.join(corrupt_lines) + '\n', encoding='utf-8')
        try:
            parse_jsonl(corrupt_src)
            raise AssertionError('strict parse accepted corrupt middle content')
        except ValueError:
            pass
        corrupt_rec = parse_jsonl(corrupt_src, tolerate_corruption=True)
        assert corrupt_rec.parse_health_status == 'corrupt_middle'
        assert corrupt_rec.malformed_middle_lines[0]['line'] == 2
        assert len(corrupt_rec.frames) == 4
        assert corrupt_rec.session_end is not None
        assert corrupt_rec.line_count == 11
        corrupt_manifest = build_manifest(corrupt_rec)
        assert corrupt_manifest['parse_health']['status'] == 'corrupt_middle'
        assert corrupt_manifest['parse_health']['malformed_middle_lines'] == [2]
        assert corrupt_manifest['capture']['frame_count'] == 4
        assert corrupt_manifest['capture']['source_finalised'] is True

        tolerant_src = root / 'corrupt-middle.jsonl'
        tolerant_rows = [
            _sidecar_fixture_rows()[0],
            _fixture_frame(1, 0, 'Custom'),
            '{"type":"frame"',
            _fixture_frame(2, 0, 'Custom'),
            {'type': 'session_end', 'schema_version': 2, 'clean_stop': True,
             'duration_qpc_ticks': 2000000, 'producer_calls': 2,
             'duplicate_serial_suppressed': 0, 'enqueued': 2, 'written': 2,
             'dropped_queue_full': 0, 'first_prepared_serial_written': 1,
             'last_prepared_serial_written': 2,
             'weapon_events_enqueued': 0, 'weapon_events_written': 0,
             'weapon_events_dropped_queue_full': 0,
             'weapon_events_session_skipped': 0},
        ]
        tolerant_src.write_text('\n'.join(
            row if isinstance(row, str) else json.dumps(row)
            for row in tolerant_rows) + '\n', encoding='utf-8')
        try:
            parse_jsonl(tolerant_src)
            raise AssertionError('strict parse accepted malformed middle line')
        except ValueError:
            pass
        tolerant_rec = parse_jsonl(tolerant_src, tolerate_corruption=True)
        assert tolerant_rec.parse_health_status == 'corrupt_middle'
        assert len(tolerant_rec.malformed_middle_lines) == 1
        assert tolerant_rec.malformed_middle_lines[0]['line'] == 3
        assert tolerant_rec.line_count == 5
        assert [frame['prepared_serial'] for frame in tolerant_rec.frames] == [1, 2]
        assert tolerant_rec.session_end is not None
        assert tolerant_rec.finalised
        tolerant_manifest = build_manifest(tolerant_rec)
        assert tolerant_manifest['parse_health']['malformed_middle_lines'] == [3]
        assert tolerant_manifest['parse_health']['parsed_record_count'] == 4
        assert tolerant_manifest['capture']['first_prepared_serial'] == 1
        assert tolerant_manifest['capture']['last_prepared_serial'] == 2
        assert tolerant_manifest['record_inventory'] == [
            {'type': 'frame', 'count': 2},
            {'type': 'session_end', 'count': 1},
            {'type': 'session_start', 'count': 1}]
        tolerant_inventory = {(row['record_type'], row['path']): row
            for row in tolerant_manifest['field_inventory']}
        assert tolerant_inventory[('frame', 'prepared_serial')][
            'records_present'] == 2
        passed.append('parse health clean/partial/corrupt with parsed records after corruption')

        post_rows = _sidecar_fixture_rows()
        post_rows.append(_fixture_frame(3, 0, 'Custom'))
        post_src = root / 'post-session-end.jsonl'
        _write_fixture(post_src, post_rows)
        post_rec = parse_jsonl(post_src)
        assert post_rec.session_end is not None
        assert not post_rec.finalised
        post_manifest = build_manifest(post_rec)
        assert post_manifest['kind'] == 'mccvr.telemetry.manifest.provisional'
        assert post_manifest['capture']['session_end_present'] is True
        assert post_manifest['capture']['source_finalised'] is False
        tail_rows = _sidecar_fixture_rows()
        tail_src = root / 'session-end-tail.jsonl'
        tail_src.write_text(
            '\n'.join(json.dumps(row) for row in tail_rows) + '\n'
            + '{"type":"ses', encoding='utf-8')
        tail_rec = parse_jsonl(tail_src)
        assert tail_rec.session_end is not None and tail_rec.trailing_partial
        assert not tail_rec.finalised
        assert build_manifest(tail_rec)['kind'] == (
            'mccvr.telemetry.manifest.provisional')
        normal_rec = parse_jsonl(src)
        assert normal_rec.finalised
        assert normal_rec.last_record_type == 'session_end'
        assert not parse_jsonl(provisional_src).finalised
        passed.append('finalisation requires a terminal session_end without trailing content')

        args = _fixture_cli_args(str(src))
        result = analyse_v5(rec, args, None)
        doc = build_diagnostics_document(rec, result, args)
        assert doc['diagnostics_schema_version'] == DIAGNOSTICS_SCHEMA_VERSION
        assert doc['kind'] == 'mccvr.telemetry.diagnostics'
        for legacy_key in ('analysis_metadata', 'session_start',
                'profile_registry', 'health', 'profile_segments',
                'profile_summaries', 'setting_changes', 'grip_latch',
                'head_yaw_episodes', 'horizontal_release', 'aim_jumps',
                'annotations', 'semantic_notes', 'v4', 'v5'):
            assert legacy_key in doc, legacy_key
        source = doc['scope']['sources'][0]
        assert source['source_id'] == 'sha256:' + rec.source_sha256
        assert source['sha256'] == rec.source_sha256
        assert source['input_file'] == src.name
        assert source['timebases']['frame_relative_seconds']['origin_qpc'] == 2776727698384
        locators = list(_iter_locators(doc))
        assert locators and all('source_id' in row for row in locators)
        assert 'grip_held_threshold' in doc['parameters']
        for entry in doc['signal_definitions'].values():
            assert entry['bindings'] and entry['description']
        endpoint_definition = doc['signal_definitions']['support_endpoint']['description']
        assert 'effective semantic support endpoint' in endpoint_definition
        assert 'support_endpoint_used_grip' in endpoint_definition
        assert 'not the consumed free two-hand product geometry' in endpoint_definition
        assert 'support grip endpoint position' not in endpoint_definition
        assert 'position-only' in doc['signal_definitions'][
            'support_grip_position']['description']
        expected_derived_methods = (
            'recorder_health', 'profile_timeline', 'profile_summaries',
            'setting_changes', 'setting_sweeps', 'grip_latch',
            'head_yaw_stationary_hand', 'b_boundary', 'w_transitions',
            'fixed_zero_crossings', 'aim_jumps', 'movement_response',
            'geometry_indexes', 'control_oracles', 'state_pose_mismatches',
            'horizontal_rear_release', 'horizontal_offline_candidates',
            'horizontal_reach_episodes', 'low_held_retention',
            'authority_decomposition', 'transition_timeline',
            'performance_index', 'temporary_profile_provenance',
            'persistent_grip_lab_episodes', 'annotations')
        for name, entry in doc['methods'].items():
            assert entry['classification'] in DIAGNOSTIC_CLASSIFICATIONS, name
            assert entry.get('raw_inputs'), name
            assert entry.get('supported_telemetry_schema_versions'), name
            assert entry.get('normalisation') is not None, name
            assert entry.get('inclusion'), name
            assert entry.get('exclusion') is not None, name
            if entry['classification'] in (
                    'heuristic_index',
                    'heuristic_index_and_deterministic_metrics',
                    'offline_simulation', 'counterfactual'):
                assert entry.get('not_a_claim'), name
            if entry.get('reports_derived_metric'):
                assert entry.get('validity_requirements') is not None, name
                assert entry.get('metric_definitions'), name
        for name in expected_derived_methods:
            entry = doc['methods'][name]
            assert entry.get('reports_derived_metric') is True, name
            assert entry.get('metric_definitions'), name
        preference_phrases = (
            'user preference', 'headset preference', 'c-dominant',
            'release hypothesis', 'hypothesis under test', 'under investigation',
            'current user', 'product goal', 'desirable feel', 'preservation check')
        default_text = json.dumps(doc, ensure_ascii=False).lower()
        assert not any(phrase in default_text for phrase in preference_phrases)
        default_report = diagnostics_markdown(doc, args).lower()
        assert not any(phrase in default_report for phrase in preference_phrases)
        for method_name in doc['methods']:
            assert f'`{method_name}`' in default_report, method_name
        assert 'verdict' not in ' '.join(_iter_keys(doc)).lower()
        assert 'recommended' not in ' '.join(_iter_keys(doc)).lower()
        assert doc['horizontal_release']['actual_horizontal_fields']['rows_omitted'] is True
        debug_args = _fixture_cli_args('--include-debug-derived-rows', str(src))
        debug_doc = build_diagnostics_document(rec, analyse_v5(rec, debug_args, None), debug_args)
        assert isinstance(
            debug_doc['horizontal_release']['actual_horizontal_fields'], list)
        markdown = diagnostics_markdown(doc, args)
        assert doc['generator']['version'] in markdown
        assert 'Diagnostics schema: `1`' in markdown
        assert str(doc['health']['frames_parsed']) in markdown
        for name in doc['signal_definitions']:
            assert f'`{name}`' in markdown
        mutated = dict(doc)
        mutated['health'] = dict(doc['health'])
        mutated['health']['frames_parsed'] = 999
        assert '999' in diagnostics_markdown(mutated, args)
        assert '# V4' not in markdown and '# V5' not in markdown
        passed.append('diagnostics envelope, locators, methods, debug gating, markdown model')

        with src.open('r', encoding='utf-8', newline='') as handle:
            original_lines = handle.read().splitlines(keepends=True)
        window_args = _fixture_cli_args('--raw-window-serial', '12',
            '--before', '1', '--after', '1', str(src))
        selected = raw_select_lines(src, window_args)
        assert selected == original_lines[2:5]
        raw_output = root / 'raw-window.jsonl'
        raw_output_args = _fixture_cli_args('--raw-window-serial', '12',
            '--before', '1', '--after', '1', '--raw-output', str(raw_output),
            str(src))
        assert run_raw_query(raw_output_args,
            build_argument_parser()) == 0
        assert raw_output.read_bytes() == ''.join(original_lines[2:5]).encode('utf-8')
        range_args = _fixture_cli_args('--raw-range-serial', '12:13', str(src))
        assert raw_select_lines(src, range_args) == original_lines[3:5]
        serial_shared_args = _fixture_cli_args('--raw-range-serial', '10:10',
            str(src))
        assert raw_select_lines(src, serial_shared_args) == [
            original_lines[1], original_lines[5], original_lines[6]]
        line_args = _fixture_cli_args('--raw-range-lines', '6:7', str(src))
        assert raw_select_lines(src, line_args) == original_lines[5:7]
        typed_args = _fixture_cli_args('--raw-range-lines', '1:9',
            '--raw-record-type', 'weapon_event', str(src))
        assert raw_select_lines(src, typed_args) == original_lines[5:7]
        for segment in doc['profile_segments']:
            parsed_start = json.loads(
                original_lines[segment['start_line'] - 1].strip())
            assert parsed_start['prepared_serial'] == segment['start_serial']
            assert segment['record_type'] == 'frame'
            assert segment['source_id'] == source['source_id']
        raw_stdout = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()),
             '--raw-window-serial', '12', '--before', '1', '--after', '1',
             str(src)], capture_output=True)
        assert raw_stdout.returncode == 0, raw_stdout.stderr
        assert raw_stdout.stdout == ''.join(original_lines[2:5]).encode('utf-8')
        assert raw_stdout.stderr == b''
        raw_all_path = root / 'raw-all.jsonl'
        raw_file_stdout = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()),
             '--raw-range-lines', '1:10', '--raw-output', str(raw_all_path),
             str(src)], capture_output=True)
        assert raw_file_stdout.returncode == 0, raw_file_stdout.stderr
        assert raw_file_stdout.stdout == b''
        assert b'Wrote' in raw_file_stdout.stderr
        assert raw_all_path.read_bytes() == ''.join(original_lines).encode('utf-8')
        passed.append('raw navigation returns original lines and locators resolve')

        explicit_dir = root / 'explicit-output'
        explicit_report = explicit_dir / 'custom report.md'
        explicit_json = explicit_dir / 'custom analysis.json'
        explicit = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()), str(src),
             '--report', str(explicit_report),
             '--json-out', str(explicit_json)], capture_output=True)
        assert explicit.returncode == 0, explicit.stderr
        canonical_json = Path(str(src.with_suffix('')) + '.diagnostics.json')
        canonical_md = Path(str(src.with_suffix('')) + '.diagnostics.md')
        assert canonical_json.is_file() and canonical_md.is_file()
        assert explicit_json.read_bytes() == canonical_json.read_bytes()
        assert explicit_report.read_bytes() == canonical_md.read_bytes()
        assert (src.parent / 'TELEMETRY_AI_GUIDE.md').is_file()
        passed.append('explicit --report/--json-out remain compatible')

        second_src = root / 'second-capture.jsonl'
        _write_fixture(second_src, _sidecar_fixture_rows(
            qpc_origin=2777000000000, build_commit='sidecar-fixture-2'))
        second_rec = parse_jsonl(second_src)
        second_result = analyse_v5(second_rec, args, None)
        items = [(rec, result), (second_rec, second_result)]
        combined = combined_v5_analysis(items)
        combined_doc = build_combined_diagnostics_document(items, combined, args)
        assert combined_doc['scope']['kind'] == 'multi_capture'
        assert len(combined_doc['scope']['sources']) == 2
        source_ids = {row['source_id'] for row in combined_doc['scope']['sources']}
        assert len(source_ids) == 2
        origins = {row['timebases']['frame_relative_seconds']['origin_qpc']
            for row in combined_doc['scope']['sources']}
        assert len(origins) == 2
        for run in combined_doc.get('runs', []):
            assert 'source_id' in run
        reordered = list(reversed(items))
        combined_reordered = build_combined_diagnostics_document(
            reordered, combined_v5_analysis(reordered), args)
        assert {row['source_id'] for row in combined_reordered['scope']['sources']} == source_ids
        passed.append('multi-capture sources, per-source timebases, order independence')

        combined_dir = root / 'combined-output'
        combined_run = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()),
             str(src), str(second_src),
             '--report', str(combined_dir),
             '--json-out', str(combined_dir)], capture_output=True)
        assert combined_run.returncode == 0, combined_run.stderr
        assert (combined_dir / 'combined.diagnostics.md').is_file()
        assert (combined_dir / 'combined.diagnostics.json').is_file()
        assert not (combined_dir / 'combined.analysis.v5_2.md').exists()
        assert not (combined_dir / 'combined.analysis.v5_2.json').exists()
        passed.append('canonical consolidated outputs carry no implicit legacy aliases')

        schema1_src = root / 'schema1-capture.jsonl'
        schema1_rows = _sidecar_fixture_rows()
        for row in schema1_rows:
            if isinstance(row, dict) and 'schema_version' in row:
                row['schema_version'] = 1
        _write_fixture(schema1_src, schema1_rows)
        schema1_rec = parse_jsonl(schema1_src)
        assert schema1_rec.schema_version == 1
        schema1_result = analyse_v5(schema1_rec, args, None)
        cross_items = [(rec, result), (schema1_rec, schema1_result)]
        cross_combined = build_combined_diagnostics_document(
            cross_items, combined_v5_analysis(cross_items), args)
        schema_versions = {row['telemetry_schema_version']
            for row in cross_combined['scope']['sources']}
        assert schema_versions == {1, 2}
        assert cross_combined['run_environment_compatible'] is False
        assert cross_combined['compatible_for_global_pooling'] is False
        assert 'timebases' not in cross_combined
        for entry in cross_combined['methods'].values():
            assert entry['supported_telemetry_schema_versions']
        passed.append('cross-schema sources stay separate and attributed')

        context_path = root / 'EXPERIMENT_CONTEXT.md'
        context_path.write_text('# Experiment context\n\n## Purpose\nfixture\n',
            encoding='utf-8')
        context = load_experiment_context(context_path)
        assert context['sha256'] == sha256_file(context_path)
        assert context['used_for'] == []
        assert build_manifest(rec) == manifest
        context_doc = build_diagnostics_document(rec, result, args,
            context_dependency=context)
        assert context_doc['context_dependency']['sha256'] == context['sha256']
        passed.append('experiment context disclosed without changing the manifest')

        guide = guide_text()
        for phrase in ('evidentiary source', 'not proof of absence',
                'Validity flags are authoritative', 'prepare', '--raw-window-serial',
                'establishes a conclusion', 'support_solve_serial',
                'pg-lab-events', 'process-local',
                'two_hand_smoothing_strength', 'two_hand_smoothing_alpha',
                'two_hand_offhand_influence', 'no user toggle'):
            assert phrase in guide, phrase
        assert guide == guide_text()
        if os.name == 'nt':
            unicode_dir = root / 'unicode-\u6d4b\u8bd5'
            unicode_dir.mkdir()
            unicode_src = unicode_dir / 'HaloMCCVR-Telemetry-\u8bb0\u5f55.jsonl'
            unicode_src.write_text(src.read_text(encoding='utf-8'), encoding='utf-8')
            environment = dict(os.environ)
            environment['PYTHONIOENCODING'] = 'cp1252'
            process = subprocess.run(
                [sys.executable, str(Path(__file__).resolve()), '--query', 'list',
                 str(unicode_src)], capture_output=True, encoding='cp1252',
                errors='replace', env=environment)
            assert process.returncode == 0, process.stderr
        passed.append('guide contract and cp1252 console safety')

    print(f'MCCVR analyser sidecar self-test: {len(passed)} checks passed')
    for label in passed:
        print('  PASS', label)
    return 0


def main():
    ap = build_argument_parser()
    args = ap.parse_args()
    _configure_console_streams()
    if args.self_test:
        rc = self_test_v4()
        print('MCCVR analyser self-test: neutral sidecar layer loaded')
        return rc
    if args.weapon_order_self_test:
        return weapon_order_self_test()
    if (args.raw_window_serial is not None or args.raw_range_serial
            or args.raw_range_lines):
        return run_raw_query(args, ap)
    if args.raw_output is not None or args.raw_record_type:
        ap.error('--raw-output/--raw-record-type require a raw selection mode')
    if args.verify_manifest is not None:
        if len(args.recordings) != 1:
            ap.error('--verify-manifest requires exactly one recording')
        return run_verify_manifest(args.recordings[0], args.verify_manifest)
    if args.weapon_order:
        if not args.recordings:
            ap.error('recordings required')
        for src in args.recordings:
            rec = parse_jsonl(src)
            r = weapon_order_analysis(rec)
            rep = weapon_order_markdown(r)
            if args.report and len(args.recordings) == 1 and not args.report.is_dir():
                args.report.write_text(rep, encoding='utf-8')
                print('Wrote', args.report)
            else:
                _write_stdout_text(rep + '\n')
            if args.json_out:
                jp = args.json_out
                write_json(jp, r)
                print('Wrote', jp)
        return 0
    if not args.recordings:
        ap.error('recordings required')
    if args.query and len(args.recordings) != 1:
        ap.error('--query currently requires exactly one recording')
    try:
        context = load_experiment_context(args.experiment_context)
    except ValueError as exc:
        ap.error(str(exc))
    multi = len(args.recordings) > 1
    # Combined-only mode is used by RUN_ANALYSER.bat after it has already
    # created each 1:1 report. Do not render/print per-run Markdown again in
    # this pass. Besides avoiding duplicate work/noise, this prevents Windows
    # legacy console encodings (for example cp1252) from choking on Unicode
    # symbols contained in reports that are not meant for the console.
    combined_only = bool(multi and (args.combined_report or args.combined_json)
                         and not args.report and not args.json_out)
    items = []
    for src in args.recordings:
        rec = parse_jsonl(src, tolerate_corruption=True)
        provisional = not rec.finalised
        _write_text_if_changed(
            sidecar_manifest_path(src, provisional),
            json_text(build_manifest(rec)))
        _write_text_if_changed(sidecar_guide_path(src), guide_text())
        if rec.malformed_middle_lines:
            first = rec.malformed_middle_lines[0]
            raise ValueError(
                f"Corrupt JSONL: malformed middle line {first['line']} in "
                f"{src.name}: {first['error']}")
        annp = resolve_annotation_arg(args.annotations, src, multi)
        ann = load_annotations(annp)
        r = analyse_v5(rec, args, ann)
        doc = build_diagnostics_document(rec, r, args, context)
        diagnostic_text = json_text(doc)
        markdown_text = diagnostics_markdown(doc, args)
        diagnostics_json, diagnostics_md = sidecar_diagnostics_paths(
            src, provisional)
        _write_text_if_changed(diagnostics_json, diagnostic_text)
        _write_text_if_changed(diagnostics_md, markdown_text)
        items.append((rec, r))
        if args.query:
            print(json.dumps(query_result(r, args.query), indent=2, sort_keys=True, ensure_ascii=False, allow_nan=False))
            continue
        if not combined_only:
            if args.report:
                if multi:
                    args.report.mkdir(parents=True, exist_ok=True)
                    rp = args.report / (src.stem + '.analysis.v5_2.md')
                else:
                    rp = args.report
                _write_text_if_changed(rp, markdown_text)
                print('Wrote', rp)
            else:
                _write_stdout_text(markdown_text)
        if args.json_out:
            if multi:
                args.json_out.mkdir(parents=True, exist_ok=True)
                jp = args.json_out / (src.stem + '.analysis.v5_2.json')
            else:
                jp = args.json_out
            _write_text_if_changed(jp, diagnostic_text)
            print('Wrote', jp)
    if (multi or args.combined_report or args.combined_json) and (not args.query):
        combined = combined_v5_analysis(items)
        combined_doc = build_combined_diagnostics_document(
            items, combined, args, context)
        combined_text = json_text(combined_doc)
        combined_md = combined_diagnostics_markdown(combined_doc)
        cr = args.combined_report
        if cr is None and args.report and args.report.is_dir():
            cr = args.report / 'combined.diagnostics.md'
        if cr is not None:
            cr.parent.mkdir(parents=True, exist_ok=True)
            _write_text_if_changed(cr, combined_md)
            print('Wrote', cr)
        cj = args.combined_json
        if cj is None and args.json_out and args.json_out.is_dir():
            cj = args.json_out / 'combined.diagnostics.json'
        if cj is not None:
            cj.parent.mkdir(parents=True, exist_ok=True)
            _write_text_if_changed(cj, combined_text)
            print('Wrote', cj)
    return 0
WO_INVALID_HANDLE = 0xFFFFFFFF
WO_KIND_BY_NAME = {'present_begin': 0, 'after_present_before_prepare': 1, 'capture_pre_latch': 2, 'capture_probe_begin': 3, 'capture_probe_result': 4, 'fp_entry': 5, 'fp_weapon_commit': 6}
WO_STATUS_BY_NAME = {'no_observation': 0, 'success': 1, 'reader_returned_false': 2, 'guard_rejected': 3, 'exception_or_fault': 4, 'not_attempted_no_safe_thread': 5, 'not_attempted_thread_mismatch': 6, 'definitively_absent': 7}
WO_TITLE_BY_ID = {0: 'none', 1: 'halo3', 2: 'odst', 3: 'reach', 4: 'halo4', 5: 'ce', 6: 'halo2'}
# Probe result statuses that do not positively observe a weapon identity.
# A failed/rejected/not-attempted probe can hide the A->B transition, so it
# prevents the negative proof required for FP_B_FIRST. A completed positive
# observation is never erased by a later failure.
WO_UNAVAILABLE_STATUSES = frozenset({
    'no_observation', 'reader_returned_false', 'guard_rejected',
    'exception_or_fault', 'not_attempted_no_safe_thread',
    'not_attempted_thread_mismatch'})

def wo_weapon_order_events(rec: ParsedRecording) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    events = [o for o in rec.other_records if isinstance(o, dict) and o.get('type') == 'weapon_event']
    gaps = [o for o in rec.other_records if isinstance(o, dict) and o.get('type') == 'event_gap']
    events.sort(key=lambda e: (int(e.get('seq', 0) or 0), int(e.get('_line', 0) or 0)))
    gaps.sort(key=lambda g: (int(g.get('first_missing_seq', 0) or 0), int(g.get('_line', 0) or 0)))
    return events, gaps

def wo_gap_covered(gaps: list[dict[str, Any]], lo: int, hi: int) -> bool:
    for g in gaps:
        try:
            a, b = int(g.get('first_missing_seq', 0) or 0), int(g.get('last_missing_seq', 0) or 0)
        except (TypeError, ValueError):
            continue
        if a <= hi and b >= lo:
            return True
    return False

def wo_seq(e: dict[str, Any]) -> int:
    return int(e.get('seq', 0) or 0)

def wo_status(e: dict[str, Any]) -> str:
    return str(e.get('status', ''))

def wo_weapon(e: dict[str, Any]) -> int:
    return int(e.get('primary_weapon', WO_INVALID_HANDLE) or 0)

def wo_owner_tuple(c: dict[str, Any]) -> tuple[int, int, int]:
    return (int(c.get('session', 0) or 0),
            int(c.get('title_generation', 0) or 0),
            int(c.get('controlled_unit', WO_INVALID_HANDLE) or 0))

def wo_valid_owner(c: dict[str, Any]) -> bool:
    session, generation, unit = wo_owner_tuple(c)
    return unit not in (0, WO_INVALID_HANDLE) and generation != 0 and session != 0

def wo_stable_invocation(inv: dict[str, Any]) -> bool:
    """A stable invocation is a paired commit that proves one weapon identity.

    The commit must have succeeded, name a real weapon handle and name a
    complete lifecycle owner. Unstable invocations stay in the invocation
    list so barriers can see that an FP frame happened without a stable
    identity.
    """
    c = inv.get('commit')
    if c is None or wo_status(c) != 'success':
        return False
    if wo_weapon(c) == WO_INVALID_HANDLE or not wo_valid_owner(c):
        return False
    return True

def wo_build_invocations(entries: list[dict[str, Any]],
    commits: list[dict[str, Any]]) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """Pair every FpEntry with the stable commit of its own invocation.

    Pairing rules: the commit follows the entry on the same thread, matches
    session and title generation, matches prepared_serial when both sides
    carry one, and lands before the next FpEntry on that thread. The first
    qualifying commit pairs, and a commit pairs with at most one entry.
    Entries whose invocation produced no commit stay unpaired; commits that
    no entry can claim are returned as orphan commits and are never silently
    discarded.
    """
    entries_sorted = sorted(entries, key=wo_seq)
    commits_sorted = sorted(commits, key=wo_seq)
    entries_by_thread: dict[int, list[dict[str, Any]]] = {}
    for e in entries_sorted:
        entries_by_thread.setdefault(int(e.get('thread_id', 0) or 0), []).append(e)
    commits_by_thread: dict[int, list[dict[str, Any]]] = {}
    for c in commits_sorted:
        commits_by_thread.setdefault(int(c.get('thread_id', 0) or 0), []).append(c)
    invocations: list[dict[str, Any]] = []
    claimed: set[int] = set()
    for thread, thread_entries in entries_by_thread.items():
        thread_commits = commits_by_thread.get(thread, [])
        first = 0
        for i, entry in enumerate(thread_entries):
            eseq = wo_seq(entry)
            while first < len(thread_commits) and wo_seq(thread_commits[first]) <= eseq:
                first += 1
            boundary = wo_seq(thread_entries[i + 1]) if i + 1 < len(thread_entries) else None
            paired = None
            for j in range(first, len(thread_commits)):
                c = thread_commits[j]
                cseq = wo_seq(c)
                if boundary is not None and cseq >= boundary:
                    break
                if id(c) in claimed:
                    continue
                if int(c.get('session', 0) or 0) != int(entry.get('session', 0) or 0):
                    continue
                if int(c.get('title_generation', 0) or 0) != int(entry.get('title_generation', 0) or 0):
                    continue
                eserial = int(entry.get('prepared_serial', 0) or 0)
                cserial = int(c.get('prepared_serial', 0) or 0)
                if eserial and cserial and eserial != cserial:
                    continue
                paired = c
                claimed.add(id(c))
                break
            invocations.append({'entry': entry, 'commit': paired})
    invocations.sort(key=lambda inv: wo_seq(inv['entry']))
    orphan_commits = [c for c in commits_sorted if id(c) not in claimed]
    return invocations, orphan_commits

def wo_analyse_title(title_id: int, events: list[dict[str, Any]], gaps: list[dict[str, Any]]) -> dict[str, Any]:
    own = [e for e in events if int(e.get('title', 0) or 0) == title_id]
    all_commits = sorted([e for e in own if e.get('kind') == 'fp_weapon_commit'], key=wo_seq)
    entries = sorted([e for e in own if e.get('kind') == 'fp_entry'], key=wo_seq)
    invocations, orphan_commits = wo_build_invocations(entries, all_commits)
    success_commits = [c for c in all_commits
                       if wo_status(c) == 'success' and wo_weapon(c) != WO_INVALID_HANDLE]

    # A transition is the gap between the last stable invocation of weapon A
    # and the first stable invocation of weapon B inside one lifecycle owner.
    # B_START is that invocation's FpEntry: the commit only proves identity
    # retrospectively, so the entry, not the commit, is the ordering boundary.
    transitions: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    for inv in invocations:
        if not wo_stable_invocation(inv):
            continue
        c = inv['commit']
        w = wo_weapon(c)
        owner = wo_owner_tuple(c)
        if current is None or current['owner'] != owner:
            current = {'weapon': w, 'commit_seq': wo_seq(c), 'owner': owner}
            continue
        if w == current['weapon']:
            current['commit_seq'] = wo_seq(c)
            continue
        b_entry = inv['entry']
        transitions.append({
            'a': current['weapon'], 'b': w,
            'a_commit_seq': current['commit_seq'],
            'b_entry_seq': wo_seq(b_entry),
            'b_commit_seq': wo_seq(c),
            'owner': owner,
            'fp_prepared_serial': int(c.get('prepared_serial', 0) or 0),
        })
        current = {'weapon': w, 'commit_seq': wo_seq(c), 'owner': owner}

    begins = {wo_seq(e) for e in own if e.get('kind') == 'capture_probe_begin'}
    results = sorted([e for e in own if e.get('kind') == 'capture_probe_result'], key=wo_seq)
    success_results = [r for r in results
                       if wo_status(r) == 'success' and wo_weapon(r) != WO_INVALID_HANDLE]

    def window_start(r: dict[str, Any]) -> int:
        bseq = int(r.get('aux0', 0) or 0)
        return bseq if bseq in begins else wo_seq(r)

    def has_begin(r: dict[str, Any]) -> bool:
        return int(r.get('aux0', 0) or 0) in begins

    # Probe evidence is bound to ONE transition: a probe may classify the
    # current transition only when its lifecycle owner matches and its whole
    # Begin/Result interval sits before B_START. The interval
    # FpEntry(B)..FpWeaponCommit(B) is an ambiguity window: the engine may
    # have switched B at any point inside it, so an observation that begins
    # there can never prove an ordering.
    analysed = []
    for t in transitions:
        a_commit = t['a_commit_seq']
        b_entry = t['b_entry_seq']
        b_commit = t['b_commit_seq']
        owner_session, owner_generation, owner_unit = t['owner']

        def same_lifecycle(r: dict[str, Any]) -> bool:
            return (int(r.get('session', 0) or 0) == owner_session and
                    int(r.get('title_generation', 0) or 0) == owner_generation)

        def probe_unit(r: dict[str, Any]) -> int:
            return int(r.get('controlled_unit', WO_INVALID_HANDLE) or 0)

        def qualifying_success(r: dict[str, Any]) -> bool:
            return (wo_status(r) == 'success' and same_lifecycle(r) and
                    probe_unit(r) == owner_unit and
                    wo_weapon(r) != WO_INVALID_HANDLE and has_begin(r) and
                    window_start(r) > a_commit)

        # Barrier 1: event-gap loss in the interval makes the ordering
        # unknowable regardless of any probe observation.
        dropped = wo_gap_covered(gaps, a_commit, b_entry)
        # Barrier 2: an FP invocation between the last stable A commit and
        # B_START that produced no stable commit could itself be the frame
        # where B first became active.
        missing_stable = False
        for inv in invocations:
            eseq = wo_seq(inv['entry'])
            if not (a_commit < eseq < b_entry):
                continue
            if not wo_stable_invocation(inv):
                missing_stable = True
                break
        # Barrier 3: a stable commit with no matching FpEntry could belong to
        # an invocation whose start was lost.
        missing_entry = any(a_commit < wo_seq(c) <= b_entry for c in orphan_commits)
        # Barrier 4: a successful observation naming a different valid
        # controlled unit means FP and Capture evidence do not describe one
        # owner lifecycle.
        owner_mismatch = False
        for r in results:
            if wo_status(r) != 'success' or not same_lifecycle(r):
                continue
            unit = probe_unit(r)
            if unit in (0, WO_INVALID_HANDLE) or unit == owner_unit:
                continue
            if window_start(r) < b_entry and wo_seq(r) > a_commit:
                owner_mismatch = True
                break
        # Barrier 5: a probe interval spanning B_START cannot be attributed
        # to either side of the boundary.
        overlap = False
        for r in results:
            if same_lifecycle(r) and window_start(r) < b_entry < wo_seq(r):
                overlap = True
                break
        # Barrier 6: a successful observation that begins inside the
        # FpEntry(B)..FpWeaponCommit(B) window is ambiguous about whether the
        # engine had switched yet.
        observed_window = False
        for r in success_results:
            if (same_lifecycle(r) and probe_unit(r) == owner_unit and
                    has_begin(r) and b_entry <= window_start(r) < b_commit):
                observed_window = True
                break
        # Barrier 7: a successful observation of a third weapon before B_START
        # contradicts the two-weapon model of this transition.
        other_weapon = False
        for r in success_results:
            if not (same_lifecycle(r) and probe_unit(r) == owner_unit and has_begin(r)):
                continue
            if not (a_commit < window_start(r) and wo_seq(r) < b_entry):
                continue
            if wo_weapon(r) not in (t['a'], t['b']):
                other_weapon = True
                break
        # A completed qualifying B observation before B_START proves the
        # pre-latch ordering; later failures cannot erase it.
        pre = [r for r in success_results
               if qualifying_success(r) and
               wo_weapon(r) == t['b'] and wo_seq(r) < b_entry]
        qualifying_a = [r for r in success_results
                        if qualifying_success(r) and
                        wo_weapon(r) == t['a'] and wo_seq(r) < b_entry]

        if dropped:
            outcome = 'INDETERMINATE_DROPPED_EVENTS'
        elif missing_stable:
            outcome = 'INDETERMINATE_FP_COMMIT_MISSING'
        elif missing_entry:
            outcome = 'INDETERMINATE_FP_ENTRY_MISSING'
        elif owner_mismatch:
            outcome = 'INDETERMINATE_OWNER_MISMATCH'
        elif overlap:
            outcome = 'OVERLAP_INDETERMINATE'
        elif observed_window:
            outcome = 'INDETERMINATE_FP_OBSERVATION_WINDOW'
        elif other_weapon:
            outcome = 'INDETERMINATE_OTHER_WEAPON'
        elif pre:
            outcome = 'PRE_LATCH_B_FIRST'
        elif qualifying_a:
            # FP_B_FIRST requires a qualifying A observation inside this
            # transition, with every later observation before B_START also a
            # qualifying A observation (any unavailable or malformed
            # observation could be hiding B). No later B probe is required.
            last_a = wo_seq(qualifying_a[-1])
            blocked = False
            for r in results:
                if not same_lifecycle(r):
                    continue
                if wo_seq(r) <= last_a or window_start(r) >= b_entry:
                    continue
                if (wo_status(r) == 'success' and
                        probe_unit(r) == owner_unit and
                        wo_weapon(r) == t['a'] and has_begin(r) and
                        window_start(r) > a_commit):
                    continue
                blocked = True
                break
            outcome = 'INDETERMINATE' if blocked else 'FP_B_FIRST'
        else:
            outcome = 'INDETERMINATE'

        last_a_ref = wo_seq(qualifying_a[-1]) if qualifying_a else a_commit
        flags = []
        if dropped:
            flags.append('dropped_events')
        if missing_stable:
            flags.append('invocation_without_stable_commit')
        if missing_entry:
            flags.append('commit_without_matching_entry')
        if owner_mismatch:
            flags.append('owner_mismatch')
        if overlap:
            flags.append('probe_overlaps_b_entry')
        if observed_window:
            flags.append('probe_inside_fp_window')
        if other_weapon:
            flags.append('other_weapon_before_b_entry')
        analysed.append({
            **t,
            'outcome': outcome,
            'qualifying_probes_before_b_entry': len(
                [r for r in success_results
                 if qualifying_success(r) and wo_seq(r) < b_entry]),
            'probe_overlaps_b_entry': overlap,
            'unavailable_after_last_a': len(
                [r for r in results
                 if same_lifecycle(r) and
                 wo_status(r) in WO_UNAVAILABLE_STATUSES and
                 last_a_ref < window_start(r) < b_entry]),
            'data_quality_flags': flags,
        })

    paired_count = sum(1 for inv in invocations if inv['commit'] is not None)
    unpaired_count = sum(1 for inv in invocations if inv['commit'] is None)
    stable_count = sum(1 for inv in invocations if wo_stable_invocation(inv))
    unstable_count = sum(1 for inv in invocations
                         if inv['commit'] is not None and not wo_stable_invocation(inv))
    gap_count = 0
    if own:
        seqs = [wo_seq(e) for e in own]
        gap_count = sum(1 for g in gaps
                        if wo_gap_covered([g], min(seqs), max(seqs)))
    data_quality_clean = (gap_count == 0 and len(orphan_commits) == 0 and
                          unpaired_count == 0)
    outcomes = [t['outcome'] for t in analysed]
    pre_any = 'PRE_LATCH_B_FIRST' in outcomes
    fp_any = 'FP_B_FIRST' in outcomes
    if pre_any and fp_any:
        verdict = 'MIXED_ORDERING'
    elif fp_any:
        verdict = 'FP_B_CAN_PRECEDE_CAPTURE'
    elif pre_any:
        if all(o == 'PRE_LATCH_B_FIRST' for o in outcomes) and data_quality_clean:
            verdict = 'PRE_LATCH_READ_VIABLE'
        else:
            verdict = 'PRE_LATCH_OBSERVED_BUT_UNRESOLVED'
    else:
        not_attempted = [r for r in results if wo_status(r) in ('not_attempted_no_safe_thread', 'not_attempted_thread_mismatch')]
        verdict = 'DIRECT_PROBE_NOT_SAFE_THREAD' if not_attempted and not success_results else 'STILL_UNRESOLVED'
    present_threads = sorted({int(e.get('thread_id', 0) or 0) for e in own if e.get('kind') in ('present_begin', 'after_present_before_prepare') and int(e.get('thread_id', 0) or 0)})
    capture_threads = sorted({int(e.get('thread_id', 0) or 0) for e in own if e.get('kind') in ('capture_pre_latch', 'capture_probe_begin', 'capture_probe_result') and int(e.get('thread_id', 0) or 0)})
    fp_threads = sorted({int(e.get('thread_id', 0) or 0) for e in own if e.get('kind') in ('fp_entry', 'fp_weapon_commit') and int(e.get('thread_id', 0) or 0)})
    if capture_threads and fp_threads:
        if len(capture_threads) == 1 and len(fp_threads) == 1 and capture_threads[0] == fp_threads[0]:
            thread_result = 'Capture == FP consistently observed'
        elif not set(capture_threads) & set(fp_threads):
            thread_result = 'Capture != FP observed'
        else:
            thread_result = 'thread identity varied'
    else:
        thread_result = 'insufficient evidence'
    return {'title_id': title_id, 'title': WO_TITLE_BY_ID.get(title_id, f'title_{title_id}'),
            'event_count': len(own), 'commit_count': len(success_commits),
            'stable_invocation_count': stable_count,
            'paired_fp_invocations': paired_count,
            'unpaired_fp_entries': unpaired_count,
            'unstable_fp_invocations': unstable_count,
            'orphan_fp_commits': len(orphan_commits),
            'event_gaps_in_title_span': gap_count,
            'completed_probe_count': len(success_results),
            'data_quality_clean': data_quality_clean,
            'transitions': analysed,
            'verdict': verdict, 'present_threads': present_threads,
            'capture_threads': capture_threads, 'fp_threads': fp_threads,
            'thread_result': thread_result}

def weapon_order_analysis(rec: ParsedRecording) -> dict[str, Any]:
    events, gaps = wo_weapon_order_events(rec)
    titles = sorted({int(e.get('title', 0) or 0) for e in events if int(e.get('title', 0) or 0) not in (0,)})
    per_title = {t: wo_analyse_title(t, events, gaps) for t in titles}
    return {'recording': rec.path.name, 'source_sha256': rec.source_sha256,
            'qpc_frequency': int(rec.session_start.get('qpc_frequency', 0) or 0),
            'total_weapon_events': len(events), 'total_event_gaps': len(gaps),
            'session_end': rec.session_end, 'per_title': per_title}

def weapon_order_markdown(r: dict[str, Any]) -> str:
    out = [f"# Weapon-order analysis: {r['recording']}", f"source_sha256: {r['source_sha256']}",
           f"weapon_events: {r['total_weapon_events']}, event_gaps: {r['total_event_gaps']}", '']
    if not r['per_title']:
        out.append('No weapon-order events in this recording. Enable the telemetry recorder and exercise weapon switches.')
        return '\n'.join(out)
    for tid, t in r['per_title'].items():
        out.append(f"## {t['title']} (id {tid}): {t['verdict']}")
        out.append(f"events={t['event_count']} stable_commits={t['commit_count']} "
                   f"stable_invocations={t['stable_invocation_count']} "
                   f"paired_invocations={t['paired_fp_invocations']} "
                   f"unpaired_entries={t['unpaired_fp_entries']} "
                   f"unstable_invocations={t['unstable_fp_invocations']} "
                   f"orphan_commits={t['orphan_fp_commits']} "
                   f"gaps_in_title_span={t['event_gaps_in_title_span']} "
                   f"completed_probes={t['completed_probe_count']} "
                   f"data_quality_clean={str(t['data_quality_clean']).lower()}")
        out.append(f"threads: {t['thread_result']} (present={t['present_threads']}, capture={t['capture_threads']}, fp={t['fp_threads']})")
        if tid == 6:
            out.append('H2 note: the Capture candidate is the same first-person user-data slot the packet builder consumes, validated through the object table; disagreement against the FP commit therefore measures update/admission timing, not two fully independent sources.')
        for tr in t['transitions']:
            out.append(f"- A=0x{tr['a']:08X} -> B=0x{tr['b']:08X} "
                       f"a_commit={tr['a_commit_seq']} b_entry={tr['b_entry_seq']} "
                       f"b_commit={tr['b_commit_seq']}: {tr['outcome']} "
                       f"(qualifying_probes_before_b_entry={tr['qualifying_probes_before_b_entry']}, "
                       f"probe_overlaps_b_entry={str(tr['probe_overlaps_b_entry']).lower()}, "
                       f"unavailable_after_last_a={tr['unavailable_after_last_a']}, "
                       f"flags={','.join(tr['data_quality_flags']) or 'none'})")
        out.append('')
    out.append('Outcomes: PRE_LATCH_B_FIRST = a qualifying probe completed after the last stable A commit and before the FpEntry that begins the first stable B invocation, and saw B; FP_B_FIRST = a qualifying A observation completed in that same interval with every later observation before B\'s FpEntry also a qualifying A observation, so no later B probe is required; INDETERMINATE_FP_OBSERVATION_WINDOW = a successful observation began inside the FpEntry(B)..FpWeaponCommit(B) window, where B\'s first active frame is unknowable; OVERLAP_INDETERMINATE = a probe interval spans B\'s FpEntry; INDETERMINATE_DROPPED_EVENTS = event-gap evidence loss in the interval; INDETERMINATE_FP_COMMIT_MISSING = an FP invocation in the interval produced no stable commit; INDETERMINATE_FP_ENTRY_MISSING = a stable commit in the interval has no matching FpEntry; INDETERMINATE_OWNER_MISMATCH = Capture and FP evidence name different controlled units; INDETERMINATE_OTHER_WEAPON = a probe saw a third weapon before B\'s FpEntry; INDETERMINATE = other missing or rejected evidence. Evidence is transition-local: probes from before the last stable A commit or from another session/generation/unit can neither prove nor deny this transition. Event gaps always win over any ordering conclusion, and a completed positive observation is never erased by a later failure.')
    out.append('Interpretation limit: this report describes observed behaviour of this recording on this build and session; it is not an engine theorem. Missing evidence is never inferred as either ordering.')
    return '\n'.join(out)

def weapon_order_self_test() -> int:
    failures = 0
    def check(cond: bool, msg: str) -> None:
        nonlocal failures
        if not cond:
            failures += 1
            print(f'WEAPON-ORDER SELF-TEST FAIL: {msg}')
    A, B, C = 0xA, 0xB, 0xC
    def ev(seq: int, kind: str, status: str, weapon: int, aux0: int = 0,
           session: int = 1, generation: int = 1, unit: int = 4660,
           thread: int = 100, serial: int = 7) -> dict[str, Any]:
        return {'type': 'weapon_event', 'seq': seq, 'session': session,
                'kind': kind, 'status': status, 'title': 1,
                'title_generation': generation, 'prepared_serial': serial,
                'thread_id': thread,
                'controlled_unit': (unit if kind in ('fp_weapon_commit', 'capture_probe_result') else WO_INVALID_HANDLE),
                'primary_weapon': (weapon if kind in ('fp_weapon_commit', 'capture_probe_result') else WO_INVALID_HANDLE),
                'aux0': aux0, 'aux1': 0, '_line': seq}
    def begin(seq: int, **kw: Any) -> dict[str, Any]:
        return ev(seq, 'capture_probe_begin', 'no_observation', WO_INVALID_HANDLE, **kw)
    def result(seq: int, status: str, weapon: int, aux0: int = 0, **kw: Any) -> dict[str, Any]:
        return ev(seq, 'capture_probe_result', status, weapon, aux0, **kw)
    def entry(seq: int, **kw: Any) -> dict[str, Any]:
        return ev(seq, 'fp_entry', 'no_observation', WO_INVALID_HANDLE, **kw)
    def commit(seq: int, weapon: int, **kw: Any) -> dict[str, Any]:
        return ev(seq, 'fp_weapon_commit', 'success', weapon, **kw)
    def gap(a: int, b: int) -> dict[str, Any]:
        return {'type': 'event_gap', 'first_missing_seq': a,
                'last_missing_seq': b, '_line': 10000 + a}
    def title_of(events: list[dict[str, Any]],
                 gaps: list[dict[str, Any]] | None = None) -> dict[str, Any]:
        return wo_analyse_title(1, events, gaps or [])
    def outcome(events: list[dict[str, Any]],
                gaps: list[dict[str, Any]] | None = None) -> str | None:
        t = title_of(events, gaps)
        return t['transitions'][0]['outcome'] if t['transitions'] else None

    # E1 clean PRE_LATCH_B_FIRST: the qualifying observation completes before
    # the FpEntry that starts the first stable B invocation.
    e1 = [entry(1), commit(2, A), begin(3), result(4, 'success', B, 3),
          entry(5), commit(6, B)]
    t1 = title_of(e1)
    check(len(t1['transitions']) == 1 and
          t1['transitions'][0]['outcome'] == 'PRE_LATCH_B_FIRST',
          'E1 clean PRE_LATCH_B_FIRST')
    check(t1['verdict'] == 'PRE_LATCH_READ_VIABLE',
          'E1 verdict PRE_LATCH_READ_VIABLE')
    check(t1['data_quality_clean'] and t1['unpaired_fp_entries'] == 0 and
          t1['orphan_fp_commits'] == 0, 'E1 data quality clean')

    # E2 clean FP_B_FIRST: a qualifying A observation completes before B's
    # FpEntry; no later B probe is required.
    e2 = [entry(1), commit(2, A), begin(3), result(4, 'success', A, 3),
          entry(5), commit(6, B)]
    t2 = title_of(e2)
    check(len(t2['transitions']) == 1 and
          t2['transitions'][0]['outcome'] == 'FP_B_FIRST',
          'E2 clean FP_B_FIRST without a later B probe')
    check(t2['verdict'] == 'FP_B_CAN_PRECEDE_CAPTURE',
          'E2 verdict FP_B_CAN_PRECEDE_CAPTURE')

    # E3 (R1 reproduction): a B observation that begins inside the B
    # invocation window cannot prove PRE.
    e3 = [entry(1), commit(2, A), entry(3), begin(4),
          result(5, 'success', B, 4), commit(6, B)]
    t3 = title_of(e3)
    check(len(t3['transitions']) == 1 and
          t3['transitions'][0]['outcome'] == 'INDETERMINATE_FP_OBSERVATION_WINDOW',
          'E3 B capture inside the B invocation window is indeterminate')
    check(t3['verdict'] == 'STILL_UNRESOLVED',
          'E3 verdict unresolved, not PRE')

    # E4 (R2 reproduction): an A observation inside the B invocation window
    # cannot prove FP-first.
    e4 = [entry(1), commit(2, A), entry(3), begin(4),
          result(5, 'success', A, 4), commit(6, B)]
    check(outcome(e4) == 'INDETERMINATE_FP_OBSERVATION_WINDOW',
          'E4 A capture inside the B invocation window is indeterminate')

    # E5 (R3 reproduction): a probe interval spanning B's FpEntry.
    e5 = [entry(1), commit(2, A), begin(3), entry(4),
          result(5, 'success', B, 3), commit(6, B)]
    check(outcome(e5) == 'OVERLAP_INDETERMINATE',
          'E5 probe spanning B entry is OVERLAP_INDETERMINATE')

    # E6 (R4 reproduction): an orphan stable commit in the interval has no
    # entry and could be the B invocation start.
    e6 = [entry(1), commit(2, A), commit(3, C), entry(4), commit(5, B)]
    t6 = title_of(e6)
    check(t6['transitions'][0]['outcome'] == 'INDETERMINATE_FP_ENTRY_MISSING',
          'E6 orphan commit in the interval blocks a confident transition')
    check(t6['orphan_fp_commits'] == 1 and not t6['data_quality_clean'],
          'E6 orphan commit reported and marks data dirty')

    # E7 an unpaired FP entry inside the interval could itself be B's start.
    e7 = [entry(1), commit(2, A), entry(3), begin(4),
          result(5, 'success', B, 4), entry(6), commit(7, B)]
    check(outcome(e7) == 'INDETERMINATE_FP_COMMIT_MISSING',
          'E7 unpaired entry in the interval blocks PRE')

    # E8 event-gap loss in the interval always wins over probe evidence.
    e8 = [entry(1), commit(2, A), begin(3), result(4, 'success', B, 3),
          entry(5), commit(6, B)]
    check(outcome(e8, [gap(3, 3)]) == 'INDETERMINATE_DROPPED_EVENTS',
          'E8 gap beats a positive PRE observation')

    # E9 an unavailable probe before the fresh A observation does not block.
    e9 = [entry(1), commit(2, A), begin(3),
          result(4, 'guard_rejected', WO_INVALID_HANDLE, 3),
          begin(5), result(6, 'success', A, 5), entry(7), commit(8, B)]
    t9 = title_of(e9)
    check(t9['transitions'][0]['outcome'] == 'FP_B_FIRST',
          'E9 unavailable before the fresh A observation does not block')
    check(t9['transitions'][0]['unavailable_after_last_a'] == 0,
          'E9 unavailable count is only after the last A')

    # E10 an unavailable probe after the fresh A observation blocks FP-first.
    e10 = [entry(1), commit(2, A), begin(3), result(4, 'success', A, 3),
           begin(5), result(6, 'guard_rejected', WO_INVALID_HANDLE, 5),
           entry(7), commit(8, B)]
    t10 = title_of(e10)
    check(t10['transitions'][0]['outcome'] == 'INDETERMINATE' and
          t10['transitions'][0]['unavailable_after_last_a'] == 1,
          'E10 unavailable after the last A blocks FP-first')

    # E11 a successful third-weapon observation before B's FpEntry.
    e11 = [entry(1), commit(2, A), begin(3), result(4, 'success', C, 3),
           entry(5), commit(6, B)]
    check(outcome(e11) == 'INDETERMINATE_OTHER_WEAPON',
          'E11 third weapon before B entry')

    # E12 a positive B observation is never erased by a later A observation.
    e12 = [entry(1), commit(2, A), begin(3), result(4, 'success', B, 3),
           begin(5), result(6, 'success', A, 5), entry(7), commit(8, B)]
    check(outcome(e12) == 'PRE_LATCH_B_FIRST',
          'E12 B-then-A before B entry still PRE')

    # E13 a probe beginning before the last stable A commit is not
    # transition-local evidence.
    e13 = [entry(1), begin(2), commit(3, A), result(4, 'success', A, 2),
           entry(5), commit(6, B)]
    check(outcome(e13) == 'INDETERMINATE',
          'E13 probe beginning before the A commit is not evidence')

    # E14/E15/E16 lifecycle boundaries never form an ordinary transition.
    def transition_count(events: list[dict[str, Any]]) -> int:
        return len(title_of(events)['transitions'])
    check(transition_count([entry(1, generation=10), commit(2, A, generation=10),
                            entry(3, generation=11), commit(4, B, generation=11)]) == 0,
          'E14 title generation boundary is not a weapon transition')
    check(transition_count([entry(1), commit(2, A, unit=111),
                            entry(3), commit(4, B, unit=222)]) == 0,
          'E15 controlled unit boundary is not a weapon transition')
    check(transition_count([entry(1, session=1), commit(2, A, session=1),
                            entry(3, session=2), commit(4, B, session=2)]) == 0,
          'E16 session boundary is not a weapon transition')

    # E17 a successful observation naming a different valid unit inside the
    # interval is an owner contradiction.
    e17 = [entry(1), commit(2, A), begin(3),
           result(4, 'success', B, 3, unit=999), entry(5), commit(6, B)]
    check(outcome(e17) == 'INDETERMINATE_OWNER_MISMATCH',
          'E17 foreign-unit success inside the interval is an owner mismatch')

    # E18 (R5 reproduction): one PRE transition plus an indeterminate one
    # must not claim PRE_LATCH_READ_VIABLE.
    e18 = [entry(1), commit(2, A), begin(3), result(4, 'success', B, 3),
           entry(5), commit(6, B), entry(7), commit(8, A)]
    t18 = title_of(e18)
    check([x['outcome'] for x in t18['transitions']] ==
          ['PRE_LATCH_B_FIRST', 'INDETERMINATE'],
          'E18 PRE plus indeterminate outcomes')
    check(t18['verdict'] == 'PRE_LATCH_OBSERVED_BUT_UNRESOLVED',
          'E18 PRE plus indeterminate verdict is unresolved')

    # E18b dirty data (an orphan commit) downgrades an all-PRE title too.
    e18b = e1 + [commit(7, C)]
    t18b = title_of(e18b)
    check([x['outcome'] for x in t18b['transitions']] == ['PRE_LATCH_B_FIRST'] and
          t18b['verdict'] == 'PRE_LATCH_OBSERVED_BUT_UNRESOLVED',
          'E18b PRE with dirty data is unresolved')

    # E19 all-PRE clean title keeps the viable verdict.
    e19 = [entry(1), commit(2, A), begin(3), result(4, 'success', B, 3),
           entry(5), commit(6, B), begin(7), result(8, 'success', A, 7),
           entry(9), commit(10, A)]
    t19 = title_of(e19)
    check([x['outcome'] for x in t19['transitions']] ==
          ['PRE_LATCH_B_FIRST', 'PRE_LATCH_B_FIRST'],
          'E19 two clean PRE transitions')
    check(t19['verdict'] == 'PRE_LATCH_READ_VIABLE', 'E19 verdict viable')

    # E20 FP-first plus indeterminate transitions keep the FP capability.
    e20 = [entry(1), commit(2, A), begin(3), result(4, 'success', A, 3),
           entry(5), commit(6, B), entry(7), commit(8, A)]
    t20 = title_of(e20)
    check([x['outcome'] for x in t20['transitions']] ==
          ['FP_B_FIRST', 'INDETERMINATE'], 'E20 FP plus indeterminate outcomes')
    check(t20['verdict'] == 'FP_B_CAN_PRECEDE_CAPTURE',
          'E20 FP-first wins over an indeterminate transition')

    # E21 five A<->B cycles: a probe from the first transition never bleeds
    # into later transitions.
    e21 = list(e1)
    next_seq = 7
    for w in [A, B, A, B, A, B, A, B, A]:
        e21 += [entry(next_seq), commit(next_seq + 1, w)]
        next_seq += 2
    t21 = title_of(e21)
    outcomes21 = [x['outcome'] for x in t21['transitions']]
    check(len(outcomes21) == 10, f'E21 ten transitions, got {len(outcomes21)}')
    check(outcomes21[0] == 'PRE_LATCH_B_FIRST',
          'E21 first transition keeps its probe')
    check(all(o == 'INDETERMINATE' for o in outcomes21[1:]),
          'E21 later transitions have no bleed-through evidence')
    check(t21['verdict'] == 'PRE_LATCH_OBSERVED_BUT_UNRESOLVED', 'E21 verdict')

    # Gap precedence also beats an FP-first proof.
    check(outcome([entry(1), commit(2, A), begin(3), result(4, 'success', A, 3),
                   entry(5), commit(6, B)], [gap(5, 5)]) ==
          'INDETERMINATE_DROPPED_EVENTS', 'gap beats an FP-first proof')

    # Exception/fault and result-only thread mismatch after the last A block.
    check(outcome([entry(1), commit(2, A), begin(3), result(4, 'success', A, 3),
                   begin(5), result(6, 'exception_or_fault', WO_INVALID_HANDLE, 5),
                   entry(7), commit(8, B)]) == 'INDETERMINATE',
          'exception/fault after the last A blocks FP-first')
    check(outcome([entry(1), commit(2, A), begin(3), result(4, 'success', A, 3),
                   result(5, 'not_attempted_thread_mismatch', WO_INVALID_HANDLE),
                   entry(6), commit(7, B)]) == 'INDETERMINATE',
          'result-only thread mismatch after the last A blocks FP-first')

    # A failed probe spanning B's FpEntry is also an overlap.
    check(outcome([entry(1), commit(2, A), begin(3), result(4, 'success', A, 3),
                   begin(5), entry(6), commit(7, B),
                   result(8, 'guard_rejected', WO_INVALID_HANDLE, 5)]) ==
          'OVERLAP_INDETERMINATE',
          'failed probe spanning B entry is an overlap')

    # A completed positive B observation survives a later failure.
    check(outcome([entry(1), commit(2, A), begin(3), result(4, 'success', B, 3),
                   result(5, 'guard_rejected', WO_INVALID_HANDLE),
                   entry(6), commit(7, B)]) == 'PRE_LATCH_B_FIRST',
          'positive B observation survives a later failure')

    # Transition-local evidence: an earlier probe cannot classify a later
    # transition. Old R1: an old B capture cannot prove the later A->B.
    r1 = title_of([entry(1), commit(2, B), begin(3), result(4, 'success', A, 3),
                   entry(5), commit(6, A), entry(7), commit(8, B)])
    check([x['outcome'] for x in r1['transitions']] ==
          ['PRE_LATCH_B_FIRST', 'INDETERMINATE'],
          'old B capture cannot prove the later A->B transition')

    # Old R2: an A observation before the current A commit cannot prove
    # FP-first.
    check(outcome([entry(1), begin(2), commit(3, A), result(4, 'success', A, 2),
                   entry(5), commit(6, B)]) == 'INDETERMINATE',
          'old A capture cannot prove FP-first')

    # Old R3: a foreign-generation capture cannot classify the later one.
    r3 = title_of([entry(1, generation=10), begin(2, generation=10),
                   commit(3, A, generation=10),
                   result(4, 'success', B, 2, generation=10),
                   entry(5, generation=11), commit(6, A, generation=11),
                   entry(7, generation=11), commit(8, B, generation=11)])
    check(len(r3['transitions']) == 1 and
          r3['transitions'][0]['outcome'] == 'INDETERMINATE',
          'foreign-generation capture cannot classify the transition')

    # Old R4: a foreign-unit capture cannot classify the current unit.
    r4 = title_of([entry(1), begin(2), result(3, 'success', B, 2, unit=111),
                   commit(4, A, unit=222), entry(5), commit(6, B, unit=222)])
    check(len(r4['transitions']) == 1 and
          r4['transitions'][0]['outcome'] == 'INDETERMINATE',
          'foreign-unit capture cannot classify the transition')

    # Mixed ordering and thread evidence.
    both = [entry(1), commit(2, A), begin(3), result(4, 'success', B, 3),
            entry(5), commit(6, B), begin(7), result(8, 'success', B, 7),
            entry(9), commit(10, A)]
    t_mixed = title_of(both)
    check([x['outcome'] for x in t_mixed['transitions']] ==
          ['PRE_LATCH_B_FIRST', 'FP_B_FIRST'], 'mixed transition outcomes')
    check(t_mixed['verdict'] == 'MIXED_ORDERING',
          f'mixed verdict, got {t_mixed["verdict"]}')
    th = [ev(1, 'capture_pre_latch', 'no_observation', WO_INVALID_HANDLE, thread=100),
          ev(2, 'fp_entry', 'no_observation', WO_INVALID_HANDLE, thread=200)]
    check(title_of(th)['thread_result'] == 'Capture != FP observed',
          'thread mismatch detected')

    # Required report text survives and documents the new labels.
    fake = {'recording': 'x', 'source_sha256': 'y', 'total_weapon_events': 2,
            'total_event_gaps': 0, 'qpc_frequency': 1, 'session_end': None,
            'per_title': {6: {'title': 'halo2', 'event_count': 1,
                              'commit_count': 0, 'stable_invocation_count': 0,
                              'paired_fp_invocations': 0,
                              'unpaired_fp_entries': 0,
                              'unstable_fp_invocations': 0,
                              'orphan_fp_commits': 0,
                              'event_gaps_in_title_span': 0,
                              'completed_probe_count': 0,
                              'data_quality_clean': True, 'transitions': [],
                              'verdict': 'STILL_UNRESOLVED',
                              'present_threads': [], 'capture_threads': [],
                              'fp_threads': [],
                              'thread_result': 'insufficient evidence'}}}
    md = weapon_order_markdown(fake)
    check('H2 note:' in md, 'H2 limitation text present')
    check('not an engine theorem' in md,
          'build/session interpretation limit present')
    check('INDETERMINATE_FP_OBSERVATION_WINDOW' in md and
          'INDETERMINATE_FP_ENTRY_MISSING' in md,
          'outcome legend documents the new labels')

    # Deterministic permutation oracle: every causally possible order of the
    # six atoms (A entry, A commit, capture begin, capture result, B entry,
    # B commit) must classify exactly when the order proves the boundary.
    atoms = ['EA', 'CA', 'CB', 'CR', 'EB', 'CC']
    constraints = [('EA', 'CA'), ('CA', 'EB'), ('EB', 'CC'), ('CB', 'CR')]
    oracle_cases = 0
    for perm in itertools.permutations(atoms):
        pos = {name: i for i, name in enumerate(perm)}
        if not all(pos[x] < pos[y] for x, y in constraints):
            continue
        for status, weapon in (('success', A), ('success', B),
                               ('guard_rejected', WO_INVALID_HANDLE)):
            seq = {name: i + 1 for i, name in enumerate(perm)}
            events = [entry(seq['EA']), commit(seq['CA'], A),
                      begin(seq['CB']),
                      result(seq['CR'], status, weapon, seq['CB']),
                      entry(seq['EB']), commit(seq['CC'], B)]
            t = title_of(events)
            o = t['transitions'][0]['outcome'] if t['transitions'] else None
            expected_confident = (status == 'success' and
                                  pos['CA'] < pos['CB'] and
                                  pos['CB'] < pos['CR'] < pos['EB'])
            got_pre = o == 'PRE_LATCH_B_FIRST'
            got_fp = o == 'FP_B_FIRST'
            oracle_cases += 1
            if (got_pre or got_fp) != expected_confident:
                check(False, f'oracle {perm} {status}: confident={got_pre or got_fp} '
                             f'expected={expected_confident} outcome={o}')
                continue
            if expected_confident and weapon == B:
                check(got_pre, f'oracle {perm} {status} expected PRE, got {o}')
            if expected_confident and weapon == A:
                check(got_fp, f'oracle {perm} {status} expected FP, got {o}')
            if status == 'success' and pos['EB'] < pos['CB'] < pos['CC']:
                check(o == 'INDETERMINATE_FP_OBSERVATION_WINDOW',
                      f'oracle {perm} {status} observation window, got {o}')
            if pos['CB'] < pos['EB'] < pos['CR']:
                check(o == 'OVERLAP_INDETERMINATE',
                      f'oracle {perm} {status} straddle, got {o}')
    check(oracle_cases > 0, 'permutation oracle executed cases')

    # End-to-end JSONL cases through the real --weapon-order CLI path.
    def e2e(name: str, event_rows: list[dict[str, Any]], verdict: str,
            outcomes: list[str]) -> None:
        with tempfile.TemporaryDirectory() as td:
            rec = Path(td) / f'{name}.jsonl'
            out_json = Path(td) / f'{name}.json'
            rows = ([_fixture_session(1, [])] + event_rows +
                    [{'type': 'session_end', 'clean_stop': True}])
            rec.write_text('\n'.join(json.dumps(row) for row in rows) + '\n',
                           encoding='utf-8')
            proc = subprocess.run(
                [sys.executable, str(Path(__file__).resolve()),
                 '--weapon-order', str(rec), '--json-out', str(out_json)],
                capture_output=True, text=True, encoding='utf-8',
                errors='replace')
            if proc.returncode != 0:
                check(False, f'E2E {name} CLI failed rc={proc.returncode}: '
                             f'{(proc.stderr or "")[:160]}')
                return
            r = json.loads(out_json.read_text(encoding='utf-8'))
            t = r['per_title'].get('1')
            if t is None:
                check(False, f'E2E {name} title data missing')
                return
            check(t['verdict'] == verdict,
                  f'E2E {name} verdict {t["verdict"]} != {verdict}')
            got = [tr['outcome'] for tr in t['transitions']]
            check(got == outcomes, f'E2E {name} outcomes {got} != {outcomes}')
            if t['transitions']:
                check(t['transitions'][0]['outcome'] in proc.stdout,
                      f'E2E {name} report prints the outcome')
    e2e('pre_clean', e1, 'PRE_LATCH_READ_VIABLE', ['PRE_LATCH_B_FIRST'])
    e2e('fp_clean', e2, 'FP_B_CAN_PRECEDE_CAPTURE', ['FP_B_FIRST'])
    e2e('window_b', e3, 'STILL_UNRESOLVED', ['INDETERMINATE_FP_OBSERVATION_WINDOW'])
    e2e('window_a', e4, 'STILL_UNRESOLVED', ['INDETERMINATE_FP_OBSERVATION_WINDOW'])
    e2e('straddle', e5, 'STILL_UNRESOLVED', ['OVERLAP_INDETERMINATE'])
    e2e('orphan_commit', e6, 'STILL_UNRESOLVED', ['INDETERMINATE_FP_ENTRY_MISSING'])
    e2e('unpaired_entry', e7, 'STILL_UNRESOLVED', ['INDETERMINATE_FP_COMMIT_MISSING'])
    e2e('gap', e8 + [gap(3, 3)], 'STILL_UNRESOLVED', ['INDETERMINATE_DROPPED_EVENTS'])
    e2e('pre_plus_indeterminate', e18, 'PRE_LATCH_OBSERVED_BUT_UNRESOLVED',
        ['PRE_LATCH_B_FIRST', 'INDETERMINATE'])
    e2e('pre_dirty_data', e18b, 'PRE_LATCH_OBSERVED_BUT_UNRESOLVED',
        ['PRE_LATCH_B_FIRST'])
    e2e('mixed', both, 'MIXED_ORDERING', ['PRE_LATCH_B_FIRST', 'FP_B_FIRST'])
    e2e('five_cycles', e21, 'PRE_LATCH_OBSERVED_BUT_UNRESOLVED',
        ['PRE_LATCH_B_FIRST'] + ['INDETERMINATE'] * 9)

    print(f'weapon-order self-test: {"PASS" if failures == 0 else f"{failures} FAILURES"}')
    return 1 if failures else 0

if __name__ == '__main__':
    raise SystemExit(main())
