"""Guard the sight layer's ONE clock.

Every stamp and every age in the sight layer (src/dll/sight_layer.cpp) is
QueryPerformanceCounter time: Sight_NowMs() / Sight_NowUs(). A publication
stamped on another clock and aged against this one loses its freshness window
to the clocks' offset: a seat limit ray stamped with GetTickCount64 and
aged against Sight_NowMs (36 ms apart, drifting) can read never
fresh. No behavioural test compiles the layer, so this checks the source:

  1. sight_layer.cpp never reads GetTickCount64 (nor timeGetTime, nor
     GetTickCount): its stamps, its ages and its report throttles are all the
     sight clock;
  2. a ray publication stores Sight_NowMs() as its sampleMs -- never a value
     the caller supplied (`<x>.sampleMs.store(ray.sampleMs ...)`), so no
     caller's clock can reach a reader;
  3. every caller of Sight_TraceDue (any src/dll .cpp / .inl) passes
     Sight_NowMs() as its time.

``--self-test`` exercises this logic on synthetic sources first.
"""

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_LAYER = ROOT / "src" / "dll" / "sight_layer.cpp"
DEFAULT_SOURCES = ROOT / "src" / "dll"

OTHER_CLOCK = re.compile(r"\b(GetTickCount64|GetTickCount|timeGetTime)\s*\(")
SAMPLE_STORE = re.compile(r"\b\w+\.sampleMs\.store\s*\(\s*([^,]*?)\s*,")
TRACE_DUE_CALL = re.compile(r"\bSight_TraceDue\s*\(")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def line_number(text, offset):
    return text.count("\n", 0, offset) + 1


def strip_comments(text):
    # Keep offsets stable: blank out comment characters but keep newlines.
    def blank(match):
        return re.sub(r"[^\n]", " ", match.group(0))
    text = re.sub(r"/\*.*?\*/", blank, text, flags=re.S)
    return re.sub(r"//[^\n]*", blank, text)


def call_argument(code, start, index):
    """The `index`-th top-level argument of the call whose '(' is at `start`, or None."""
    depth, args, current = 0, [], []
    for at in range(start, len(code)):
        c = code[at]
        if c == "(":
            depth += 1
            if depth == 1:
                continue
        elif c == ")":
            depth -= 1
            if depth == 0:
                args.append("".join(current))
                return " ".join(args[index].split()) if index < len(args) else None
        elif c == "," and depth == 1:
            args.append("".join(current))
            current = []
            continue
        current.append(c)
    return None


def check_layer(text):
    code = strip_comments(text)
    violations = []
    for m in OTHER_CLOCK.finditer(code):
        violations.append(f"sight_layer.cpp line {line_number(code, m.start())}: {m.group(1)}() -- the sight "
                          f"layer reads one clock, Sight_NowMs()/Sight_NowUs()")
    stores = 0
    for m in SAMPLE_STORE.finditer(code):
        stores += 1
        value = " ".join(m.group(1).split())
        if re.search(r"\.sampleMs\b", value):
            violations.append(f"sight_layer.cpp line {line_number(code, m.start())}: sampleMs stored from "
                              f"'{value}' -- a publication stamps Sight_NowMs() itself, never the caller's value")
    require(stores, "sight_layer.cpp: no sampleMs.store( found -- the check no longer sees the publications")
    return violations


def check_callers(name, text):
    code = strip_comments(text)
    violations = []
    for m in TRACE_DUE_CALL.finditer(code):
        open_paren = m.end() - 1
        argument = call_argument(code, open_paren, 1)
        if argument is None:
            violations.append(f"{name} line {line_number(code, m.start())}: Sight_TraceDue( call not parsed")
        elif argument.startswith("uint64_t"):
            continue   # a declaration's parameter list
        elif argument != "Sight_NowMs()":
            violations.append(f"{name} line {line_number(code, m.start())}: Sight_TraceDue(..., {argument}) -- "
                              f"its time must be Sight_NowMs()")
    return violations


SELF_TEST_LAYER = """
uint64_t Sight_NowMs() { return Sight_NowUs() / 1000ull; }   // GetTickCount64() in a comment is fine
void PublishAuxRay(AuxRayPublication& p, const SightAuxRay& ray)
{
    p.sampleMs.store(Sight_NowMs(), std::memory_order_relaxed);
}
void Sight_PublishTrace()
{
    const uint64_t now = Sight_NowMs();
    t.sampleMs.store(now, std::memory_order_relaxed);
}
"""
SELF_TEST_CALLER = """
bool Sight_TraceDue(GameTitle title, uint64_t nowMs);
void Tick()
{
    if (Sight_TraceFaulted(kTitle) || !Sight_TraceDue(kTitle, Sight_NowMs())) return;
    if (!Sight_TraceDue(title, Sight_NowMs()))
        return;
}
"""


def run_self_test():
    require(not check_layer(SELF_TEST_LAYER), "the good synthetic layer must pass")
    require(not check_callers("good.inl", SELF_TEST_CALLER), "the good synthetic caller must pass")
    require(check_layer(SELF_TEST_LAYER.replace("return Sight_NowUs() / 1000ull;", "return GetTickCount64();")),
            "GetTickCount64 in the layer must be rejected")
    require(check_layer(SELF_TEST_LAYER + "bool Live() { return Fresh(at, GetTickCount(), 150); }\n"),
            "GetTickCount in the layer must be rejected")
    require(check_layer(SELF_TEST_LAYER.replace("p.sampleMs.store(Sight_NowMs(),", "p.sampleMs.store(ray.sampleMs,")),
            "a publication that stores the caller's stamp must be rejected")
    require(check_callers("bad.inl", SELF_TEST_CALLER.replace(
        "!Sight_TraceDue(kTitle, Sight_NowMs())", "!Sight_TraceDue(kTitle, GetTickCount64())")),
        "a trace driver on another clock must be rejected")
    require(check_callers("bad.inl", SELF_TEST_CALLER.replace(
        "!Sight_TraceDue(title, Sight_NowMs())", "!Sight_TraceDue(title, nowMs)")),
        "a trace driver passing an unproven local must be rejected")
    try:
        check_layer("void f() {}\n")
        raise AssertionError("a layer with no publication must not pass silently")
    except ValueError:
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--layer", type=Path, default=DEFAULT_LAYER)
    parser.add_argument("--sources", type=Path, default=DEFAULT_SOURCES)
    parser.add_argument("--self-test", action="store_true", help="also exercise the logic on synthetic sources")
    args = parser.parse_args()

    if args.self_test:
        run_self_test()

    violations = check_layer(args.layer.read_text(encoding="utf-8-sig"))
    callers = 0
    for path in sorted(list(args.sources.glob("*.cpp")) + list(args.sources.glob("*.inl"))):
        text = path.read_text(encoding="utf-8-sig", errors="replace")
        if "Sight_TraceDue" not in text:
            continue
        callers += 1
        violations += check_callers(path.name, text)
    require(callers, "no Sight_TraceDue caller found -- the check no longer sees the trace drivers")
    if violations:
        for violation in violations:
            print(f"FAIL: {violation}", file=sys.stderr)
        return 1
    print(f"sight clock guard passed: sight_layer.cpp reads one clock and stamps its own publications; "
          f"{callers} trace-driver files pass Sight_NowMs()")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ValueError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
