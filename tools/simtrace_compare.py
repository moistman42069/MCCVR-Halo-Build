"""Compare two simulation traces written by the mod's dev key dev_sim_trace (src/dll/reach_sim_trace.inl).

usage: python simtrace_compare.py --list FILE
       python simtrace_compare.py FILE_A[:RUN] FILE_B[:RUN] [--show N] [--fields position,parent,frame,motion,nodes,aim]

A file holds one or more RUNS (a new run starts when the game's generation changes or the tick goes back: a film
started again, or the next game). RUN is the run's number as --list prints it (default: the last run of the file).
Ticks are matched by number. For every tick both runs hold, each object's record is compared field by field; the
report says how long the two agree and, where they part, on which tick, in which objects and by how much.
"""
import struct
import sys

HEADER = struct.Struct("<IIIIQII")
RECORD = struct.Struct("<HhBBHi3fIIII")
MAGIC = 0x544D4953
KINDS = {0: "biped", 1: "vehicle", 2: "weapon", 3: "equipment", 4: "terminal", 5: "projectile", 6: "scenery",
         7: "machine", 8: "control", 9: "sound scenery", 10: "crate", 11: "creature", 12: "giant", 13: "effect scenery"}
FIELDS = ["position", "parent", "frame", "motion", "nodes", "aim"]


def load_runs(path):
    data = open(path, "rb").read()
    runs, current, at, last = [], None, 0, None
    while at + HEADER.size <= len(data):
        magic, generation, tick, count, wall, probe, probes_run = HEADER.unpack_from(data, at)
        if magic != MAGIC:
            raise SystemExit("%s: no tick header at byte %d (the file is cut or not a trace)" % (path, at))
        size = count * RECORD.size
        body = at + HEADER.size
        if body + size > len(data):
            break   # the last tick was still being written
        if current is None or last is None or generation != last[0] or tick <= last[1]:
            current = {"generation": generation, "ticks": []}
            runs.append(current)
        current["ticks"].append((tick, body, count, wall, probe, probes_run))
        last = (generation, tick)
        at = body + size
    return data, runs


def describe(path):
    data, runs = load_runs(path)
    print("%s: %d bytes, %d run(s)" % (path, len(data), len(runs)))
    for i, run in enumerate(runs):
        ticks = run["ticks"]
        probes = sorted(set(t[4] for t in ticks))
        made = ticks[-1][5] - ticks[0][5]
        seconds = (ticks[-1][3] - ticks[0][3]) / 1000.0
        print("  run %d: generation %d, ticks %d..%d (%d ticks over %.1f s), objects %d..%d, probe %s, probe calls %d"
              % (i, run["generation"], ticks[0][0], ticks[-1][0], len(ticks), seconds,
                 min(t[2] for t in ticks), max(t[2] for t in ticks), probes, made))


def pick(spec):
    path, _, number = spec.rpartition(":") if spec.count(":") > 1 or (":" in spec and spec[-1].isdigit() and not spec[1:3] == ":\\"[:2]) else (spec, "", "")
    if not path:
        path, number = spec, ""
    data, runs = load_runs(path)
    if not runs:
        raise SystemExit("%s holds no complete tick" % path)
    run = runs[int(number)] if number else runs[-1]
    return path, data, run


def records(data, body, count):
    out = {}
    for i in range(count):
        r = RECORD.unpack_from(data, body + i * RECORD.size)
        out[(r[0], r[1])] = r
    return out


def diff_fields(a, b):
    out = []
    if a[6:9] != b[6:9]:
        out.append("position")
    if a[5] != b[5]:
        out.append("parent")
    if a[9] != b[9]:
        out.append("frame")
    if a[10] != b[10]:
        out.append("motion")
    if a[11] != b[11] or a[4] != b[4]:
        out.append("nodes")
    if a[12] != b[12]:
        out.append("aim")
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--list" in sys.argv:
        for path in args:
            describe(path)
        return
    show = int(sys.argv[sys.argv.index("--show") + 1]) if "--show" in sys.argv else 12
    watch = sys.argv[sys.argv.index("--fields") + 1].split(",") if "--fields" in sys.argv else FIELDS
    if "--show" in sys.argv:
        args = [a for a in args if a != str(show)]
    if "--fields" in sys.argv:
        args = [a for a in args if a != ",".join(watch)]
    if len(args) != 2:
        raise SystemExit(__doc__)
    path_a, data_a, run_a = pick(args[0])
    path_b, data_b, run_b = pick(args[1])
    ticks_a = {t[0]: t for t in run_a["ticks"]}
    ticks_b = {t[0]: t for t in run_b["ticks"]}
    common = sorted(set(ticks_a) & set(ticks_b))
    print("A %s generation %d: ticks %d..%d (%d)" % (path_a, run_a["generation"], run_a["ticks"][0][0],
                                                      run_a["ticks"][-1][0], len(ticks_a)))
    print("B %s generation %d: ticks %d..%d (%d)" % (path_b, run_b["generation"], run_b["ticks"][0][0],
                                                      run_b["ticks"][-1][0], len(ticks_b)))
    if not common:
        raise SystemExit("the two runs share no tick number")
    print("ticks in both: %d (%d..%d)" % (len(common), common[0], common[-1]))
    first = {}
    per_field = {f: 0 for f in FIELDS}
    differing_ticks = 0
    only_ticks = 0
    shown = 0
    timeline = []
    for tick in common:
        ta, tb = ticks_a[tick], ticks_b[tick]
        raw_a = data_a[ta[1]:ta[1] + ta[2] * RECORD.size]
        raw_b = data_b[tb[1]:tb[1] + tb[2] * RECORD.size]
        if raw_a == raw_b:
            timeline.append(0)
            continue
        ra, rb = records(data_a, ta[1], ta[2]), records(data_b, tb[1], tb[2])
        only_a, only_b = sorted(set(ra) - set(rb)), sorted(set(rb) - set(ra))
        changed = []
        for key in sorted(set(ra) & set(rb)):
            fields = [f for f in diff_fields(ra[key], rb[key]) if f in watch]
            if fields:
                changed.append((key, fields))
                for f in fields:
                    per_field[f] += 1
                    first.setdefault(f, tick)
        if only_a or only_b:
            only_ticks += 1
            first.setdefault("objects", tick)
        if not (changed or only_a or only_b):
            timeline.append(0)
            continue
        differing_ticks += 1
        timeline.append(len(changed) + len(only_a) + len(only_b))
        if shown < show:
            shown += 1
            print("tick %d: %d objects differ, %d only in A, %d only in B" % (tick, len(changed), len(only_a), len(only_b)))
            for key, fields in changed[:6]:
                a, b = ra[key], rb[key]
                d = [b[6 + i] - a[6 + i] for i in range(3)]
                print("    object %04X:%04X %-10s %-28s A at (%.4f %.4f %.4f), B off by (%.5f %.5f %.5f)"
                      % (key[1] & 0xFFFF, key[0], KINDS.get(a[2], "kind %d" % a[2]), "+".join(fields), a[6], a[7], a[8],
                         d[0], d[1], d[2]))
            for key in only_a[:3]:
                print("    only in A: %04X:%04X %s" % (key[1] & 0xFFFF, key[0], KINDS.get(ra[key][2], "kind %d" % ra[key][2])))
            for key in only_b[:3]:
                print("    only in B: %04X:%04X %s" % (key[1] & 0xFFFF, key[0], KINDS.get(rb[key][2], "kind %d" % rb[key][2])))
    print()
    if not differing_ticks:
        print("IDENTICAL on all %d shared ticks (fields compared: %s)" % (len(common), ", ".join(watch)))
        return
    print("DIFFERENT on %d of %d shared ticks; %d ticks hold objects the other run lacks" % (differing_ticks, len(common), only_ticks))
    for name in ["objects"] + FIELDS:
        if name in first:
            where = common.index(first[name])
            print("  first difference in %-8s tick %d (shared tick %d of %d)%s"
                  % (name + ":", first[name], where + 1, len(common),
                     "" if name == "objects" else "; %d object-ticks in all" % per_field[name]))
    step = max(1, len(timeline) // 40)
    cells = []
    for i in range(0, len(timeline), step):
        chunk = timeline[i:i + step]
        cells.append("%d" % max(chunk) if max(chunk) < 10 else "+")
    print("  objects differing, in %d-tick windows (0 = none, + = ten or more):" % step)
    print("  " + "".join(cells))


if __name__ == "__main__":
    main()
