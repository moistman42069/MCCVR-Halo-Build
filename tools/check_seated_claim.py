"""Guard the seated converged claim's call site in game.cpp.

The rule: convergence is never switched off for a NATURAL, in-game
cause (a seat's stop, a turret's rate cap, a stall, an aim the game is still
turning toward the hand). The decision itself is a pure function,
``converge::SeatedClaimDecide`` (src/common/converge_policy.h), whose facts
hold no natural cause and are pinned by tests/converge_policy_tests.cpp. But
that function only decides if the call site uses it and nothing else: a
realistic regression is an extra term at the call site, e.g.

    const bool sightSeatedClaim = seatedClaim.claim && !LegacyDriveSeatLimited(t);

or ``sightSeatedConverged = false;`` under a turret check. No behavioural test
compiles game.cpp, so this checks the wiring in the source. It asserts only:

  1. ``SeatedClaimDecide(`` is called exactly once in game.cpp, as
     ``const converge::SeatedClaim seatedClaim = converge::SeatedClaimDecide(seatedClaimFacts);``;
  2. ``sightSeatedClaim`` is assigned exactly once, as
     ``const bool sightSeatedClaim = seatedClaim.claim;`` -- nothing added;
  3. ``sightSeatedConverged`` is only ever set to ``true`` after its
     declaration (``bool sightSeatedConverged = false;``) -- nothing clears it;
  4. the seated mark is placed exactly once, as
     ``if (sightSeatedClaim) Sight_MarkConverged(sightSeatedTitle, ...)`` -- no
     extra condition on the mark itself;
  5. every ``seatedClaimFacts.<field> = ...;`` names a field of
     SeatedClaimFacts, and neither those right-hand sides nor any assignment to
     ``sightSeatedPointFinite`` mention a natural-cause symbol.

``--self-test`` exercises this logic on synthetic sources first.
"""

import argparse
import re
import sys
from pathlib import Path


DEFAULT_SOURCE = Path(__file__).resolve().parent.parent / "src" / "dll" / "game.cpp"
DEFAULT_POLICY = Path(__file__).resolve().parent.parent / "src" / "common" / "converge_policy.h"

DECIDE_CALL = re.compile(r"\bSeatedClaimDecide\s*\(")
DECIDE_SITE = re.compile(
    r"const\s+converge::SeatedClaim\s+seatedClaim\s*=\s*"
    r"converge::SeatedClaimDecide\s*\(\s*seatedClaimFacts\s*\)\s*;"
)
CLAIM_ASSIGN = re.compile(r"\bsightSeatedClaim\s*(?:[-+*/|&^]?=)(?!=)\s*([^;]*);")
CLAIM_SITE_RHS = "seatedClaim.claim"
MARK_CALL = re.compile(r"\bSight_MarkConverged\s*\(\s*sightSeatedTitle\b")
MARK_SITE = re.compile(r"\bif\s*\(\s*sightSeatedClaim\s*\)\s*Sight_MarkConverged\s*\(\s*sightSeatedTitle\b")
CONVERGED_ASSIGN = re.compile(r"\bsightSeatedConverged\s*(?:[-+*/|&^]?=)(?!=)\s*([^;]*);")
CONVERGED_DECL = re.compile(r"\bbool\s+sightSeatedConverged\s*=\s*false\s*;")
POINT_ASSIGN = re.compile(r"\bsightSeatedPointFinite\s*(?:[-+*/|&^]?=)(?!=)\s*([^;]*);")
FACT_ASSIGN = re.compile(r"\bseatedClaimFacts\.(\w+)\s*(?:[-+*/|&^]?=)(?!=)\s*([^;]*);")
FACTS_STRUCT = re.compile(r"struct\s+SeatedClaimFacts\s*\{(.*?)\};", re.S)
FACT_FIELD = re.compile(r"\bbool\s+(\w+)\s*=")

# Symbols of the natural causes the decision must never read (the witnesses' own names included).
NATURAL = re.compile(
    r"natural|Latch|latch|Stall|stall|Turret|turret|Outcome|outcome|Limited|limited|"
    r"yawShort|YawShort|HeldAtLimit|heldAtLimit|errYaw|errPitch",
)


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


def fact_fields(policy_text):
    match = FACTS_STRUCT.search(strip_comments(policy_text))
    require(match is not None, "converge_policy.h: struct SeatedClaimFacts not found")
    fields = set(FACT_FIELD.findall(match.group(1)))
    require(fields, "converge_policy.h: SeatedClaimFacts has no fields")
    return fields


def check_source(text, fields):
    code = strip_comments(text)
    violations = []

    calls = list(DECIDE_CALL.finditer(code))
    sites = list(DECIDE_SITE.finditer(code))
    if len(calls) != 1 or len(sites) != 1:
        violations.append(
            f"SeatedClaimDecide( must be called exactly once, as the seatedClaim definition "
            f"(found {len(calls)} calls, {len(sites)} matching definitions)")

    claims = list(CLAIM_ASSIGN.finditer(code))
    if len(claims) != 1:
        violations.append(f"sightSeatedClaim must be assigned exactly once (found {len(claims)})")
    for m in claims:
        rhs = " ".join(m.group(1).split())
        if rhs != CLAIM_SITE_RHS:
            violations.append(
                f"line {line_number(code, m.start())}: sightSeatedClaim = '{rhs}', "
                f"expected exactly '{CLAIM_SITE_RHS}' (the decision is SeatedClaimDecide's alone)")

    marks = list(MARK_CALL.finditer(code))
    mark_sites = list(MARK_SITE.finditer(code))
    if len(marks) != 1 or len(mark_sites) != 1:
        violations.append(
            f"the seated mark must be placed exactly once, as 'if (sightSeatedClaim) Sight_MarkConverged("
            f"sightSeatedTitle, ...)' (found {len(marks)} marks, {len(mark_sites)} matching sites)")

    decls = list(CONVERGED_DECL.finditer(code))
    if len(decls) != 1:
        violations.append(f"expected one 'bool sightSeatedConverged = false;' declaration (found {len(decls)})")
    # The declaration's own initialiser matches from its identifier; skip exactly that one.
    decl_names = {d.start() + d.group(0).index("sightSeatedConverged") for d in decls}
    for m in CONVERGED_ASSIGN.finditer(code):
        if m.start() in decl_names:
            continue
        rhs = " ".join(m.group(1).split())
        if rhs != "true":
            violations.append(
                f"line {line_number(code, m.start())}: sightSeatedConverged = '{rhs}'; it may only be set "
                f"true once solved (nothing may clear it: a withheld claim is SeatedClaimDecide's)")

    for m in POINT_ASSIGN.finditer(code):
        rhs = m.group(1)
        if NATURAL.search(rhs):
            violations.append(
                f"line {line_number(code, m.start())}: sightSeatedPointFinite reads a natural cause: "
                f"'{' '.join(rhs.split())}'")

    seen = set()
    for m in FACT_ASSIGN.finditer(code):
        field, rhs = m.group(1), m.group(2)
        where = line_number(code, m.start())
        if field not in fields:
            violations.append(f"line {where}: seatedClaimFacts.{field} is not a field of SeatedClaimFacts")
        if field in seen:
            violations.append(f"line {where}: seatedClaimFacts.{field} is assigned twice")
        seen.add(field)
        if NATURAL.search(rhs):
            violations.append(
                f"line {where}: seatedClaimFacts.{field} reads a natural cause: '{' '.join(rhs.split())}'")
    missing = fields - seen
    if missing:
        violations.append(f"seatedClaimFacts never sets: {', '.join(sorted(missing))}")
    return violations


SELF_TEST_FIELDS = {"solved", "pointFinite", "aimFeedbackFinite"}
SELF_TEST_GOOD = """
    bool sightSeatedConverged = false;
    bool sightSeatedPointFinite = true;
    if (x) sightSeatedConverged = true;   // sightSeatedConverged = false; in a comment is fine
    sightSeatedPointFinite = halo4SeatPointValid;
    converge::SeatedClaimFacts seatedClaimFacts{};
    seatedClaimFacts.solved = sightSeatedConverged;
    seatedClaimFacts.pointFinite = sightSeatedPointFinite;
    seatedClaimFacts.aimFeedbackFinite = converge::AimFeedbackFinite(ax, ay, az);
    const converge::SeatedClaim seatedClaim = converge::SeatedClaimDecide(seatedClaimFacts);
    const bool sightSeatedClaim = seatedClaim.claim;
    if (sightSeatedClaim)
        Sight_MarkConverged(sightSeatedTitle, g_worldScale.load(std::memory_order_relaxed));
    if (sightSeatedClaim == true) {}
"""


def run_self_test():
    require(not check_source(SELF_TEST_GOOD, SELF_TEST_FIELDS), "the good synthetic source must pass")

    def must_fail(source, why):
        require(check_source(source, SELF_TEST_FIELDS), why)

    must_fail(SELF_TEST_GOOD.replace(
        "const bool sightSeatedClaim = seatedClaim.claim;",
        "const bool sightSeatedClaim = seatedClaim.claim && !LegacyDriveSeatLimited(t);"),
        "an extra term at the claim must be rejected")
    must_fail(SELF_TEST_GOOD + "    if (OccupiedTurretSeat()) sightSeatedConverged = false;\n",
              "clearing sightSeatedConverged must be rejected")
    must_fail(SELF_TEST_GOOD.replace(
        "seatedClaimFacts.aimFeedbackFinite = converge::AimFeedbackFinite(ax, ay, az);",
        "seatedClaimFacts.aimFeedbackFinite = converge::AimFeedbackFinite(ax, ay, az) && !natural.stall;"),
        "a natural cause in a fact must be rejected")
    must_fail(SELF_TEST_GOOD.replace(
        "sightSeatedPointFinite = halo4SeatPointValid;",
        "sightSeatedPointFinite = halo4SeatPointValid && !turretSeat;"),
        "a natural cause in the point's finiteness must be rejected")
    must_fail(SELF_TEST_GOOD + "    seatedClaimFacts.stalled = s;\n", "an unknown fact must be rejected")
    must_fail(SELF_TEST_GOOD + "    const auto again = converge::SeatedClaimDecide(seatedClaimFacts);\n",
              "a second decision must be rejected")
    must_fail(SELF_TEST_GOOD + "    sightSeatedClaim &= !latched;\n", "a second assignment must be rejected")
    must_fail(SELF_TEST_GOOD.replace("if (sightSeatedClaim)\n", "if (sightSeatedClaim && !stalled)\n"),
              "an extra condition on the mark must be rejected")
    must_fail(SELF_TEST_GOOD.replace("    seatedClaimFacts.pointFinite = sightSeatedPointFinite;\n", ""),
              "a fact never set must be rejected")
    require(fact_fields("struct SeatedClaimFacts {\n bool a = false; // x\n bool b = true;\n};") == {"a", "b"},
            "the fields of SeatedClaimFacts are read from the header")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--policy", type=Path, default=DEFAULT_POLICY)
    parser.add_argument("--self-test", action="store_true", help="also exercise the logic on synthetic sources")
    args = parser.parse_args()

    if args.self_test:
        run_self_test()

    fields = fact_fields(args.policy.read_text(encoding="utf-8-sig"))
    violations = check_source(args.source.read_text(encoding="utf-8-sig"), fields)
    if violations:
        for violation in violations:
            print(f"FAIL: {violation}", file=sys.stderr)
        return 1
    print(f"seated claim guard passed: the claim is SeatedClaimDecide's alone over "
          f"{len(fields)} facts in {args.source.as_posix()}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ValueError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
