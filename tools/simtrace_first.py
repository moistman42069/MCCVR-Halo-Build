"""For two sim trace runs: the first tick each object differs on (any field but the skeleton hash), in order.
usage: python simtrace_first.py A.bin:run B.bin:run [max lines]"""
import sys
from simtrace_compare import load_runs, RECORD, KINDS
def pick(spec):
    path, number = spec.rsplit(":", 1)
    data, runs = load_runs(path)
    return data, runs[int(number)]
da, ra = pick(sys.argv[1]); db, rb = pick(sys.argv[2])
limit = int(sys.argv[3]) if len(sys.argv) > 3 else 40
ta = {t[0]: t for t in ra["ticks"]}; tb = {t[0]: t for t in rb["ticks"]}
def recs(d, t): return {(r[0], r[1]): r for r in (RECORD.unpack_from(d, t[1] + i * RECORD.size) for i in range(t[2]))}
first = {}
for tick in sorted(set(ta) & set(tb)):
    a, b = ta[tick], tb[tick]
    if da[a[1]:a[1] + a[2] * RECORD.size] == db[b[1]:b[1] + b[2] * RECORD.size]: continue
    A, B = recs(da, a), recs(db, b)
    for k in sorted(set(A) ^ set(B)):
        if k not in first:
            r = A.get(k) or B.get(k)
            first[k] = (tick, "only in %s" % ("A" if k in A else "B"), r)
    for k in sorted(set(A) & set(B)):
        if k in first: continue
        x, y = A[k], B[k]
        what = []
        if x[6:9] != y[6:9]: what.append("position")
        if x[5] != y[5]: what.append("parent")
        if x[9] != y[9]: what.append("frame")
        if x[10] != y[10]: what.append("motion")
        if x[12] != y[12]: what.append("aim")
        if what:
            d = [y[6 + i] - x[6 + i] for i in range(3)]
            first[k] = (tick, "+".join(what) + " (B off by %.5f %.5f %.5f)" % tuple(d), x)
for k, (tick, what, r) in sorted(first.items(), key=lambda kv: kv[1][0])[:limit]:
    print("tick %d: %04X:%04X %-10s parent %08X at (%.3f %.3f %.3f): %s" % (tick, k[1] & 0xFFFF, k[0], KINDS.get(r[2], "kind %d" % r[2]), r[5] & 0xFFFFFFFF, r[6], r[7], r[8], what))
print("%d objects differ at some tick" % len(first))
