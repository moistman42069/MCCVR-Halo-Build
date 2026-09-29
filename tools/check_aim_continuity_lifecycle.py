"""Guard the Virtual Stock aim-continuity invalidation topology in vr.cpp.

The continuity layer must be invalidated on exactly the exceptional lifecycle
paths that own it, and must NOT be invalidated by the routine prepared-frame
retire. That invariant was silently broken once before: a reset on the routine
path made every prepared frame a first observation, so no grab/release edge
could ever seed. No behavioural test can catch it, because the defect is in
the wiring rather than in the state machine.

The check is deliberately narrow. It asserts only:

      1. the identifier ``InvalidateAimContinuityLayer(`` occurs exactly ten
      times in ``src/dll/vr.cpp``: its definition plus exactly nine call sites
      (EndPreparedFrameWithoutLayers, EnterFrameWaitFatalDrain, the LOCAL
      reference-space change, session EXITING/LOSS_PENDING, instance loss, the
      seam title/generation/epoch change, the seam !applicable branch, the
      handedness change, and the persistent-support-grip owner break /
      generation replacement). That owner break normally invalidates prior
      geometry. The narrowly qualified VS-OFF product-release bridge instead
      preserves an already-presented latch release to raw one-hand aim, but
      never across title/generation identity loss;
  2. the body of ``ResetPreparedFrame`` (brace-matched from its definition
     signature) contains neither ``InvalidateAimContinuityLayer`` nor
     ``ResetAimContinuity``.

It deliberately does not freeze the surrounding implementation text: which
exceptional path each call belongs to stays with human review and the
lifecycle documentation. ``--self-test`` exercises this checking logic against
synthetic sources and never touches the real file.
"""

import argparse
import re
import sys
from pathlib import Path


INVALIDATE = "InvalidateAimContinuityLayer"
RESET = "ResetAimContinuity"
ROUTINE_FUNCTION = "ResetPreparedFrame"
EXPECTED_REFERENCES = 10
FORBIDDEN_IN_ROUTINE = (INVALIDATE, RESET)
DEFAULT_SOURCE = (
    Path(__file__).resolve().parent.parent / "src" / "dll" / "vr.cpp"
)

IDENTIFIER_REFERENCE = re.compile(r"\b" + re.escape(INVALIDATE) + r"\s*\(")
ROUTINE_DEFINITION = re.compile(
    r"\bvoid\s+" + re.escape(ROUTINE_FUNCTION) + r"\s*\([^;{}]*\)\s*"
    r"(?:noexcept\s*)?\{"
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def find_routine_body(text: str) -> tuple[int, int] | None:
    """Brace-match the body of ``ResetPreparedFrame``.

    Returns the (start, end) offsets of the body text between the definition's
    braces, or None when the definition cannot be located or its braces do not
    balance. Braces inside comments, string literals and character literals are
    ignored so the match cannot drift.
    """
    match = ROUTINE_DEFINITION.search(text)
    if match is None:
        return None
    open_index = match.end() - 1
    depth = 0
    index = open_index
    length = len(text)
    while index < length:
        character = text[index]
        following = text[index + 1] if index + 1 < length else ""
        if character == "/" and following == "/":
            newline = text.find("\n", index)
            index = length if newline == -1 else newline + 1
            continue
        if character == "/" and following == "*":
            closing = text.find("*/", index + 2)
            index = length if closing == -1 else closing + 2
            continue
        if character == '"' or character == "'":
            quote = character
            index += 1
            while index < length:
                if text[index] == "\\":
                    index += 2
                    continue
                if text[index] == quote:
                    index += 1
                    break
                index += 1
            continue
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return (open_index + 1, index)
        index += 1
    return None


def check_source(text: str) -> list[str]:
    """Return the violation messages for ``text`` (empty when it is clean)."""
    violations: list[str] = []

    references = [match.start() for match in IDENTIFIER_REFERENCE.finditer(text)]
    if len(references) != EXPECTED_REFERENCES:
        lines = ", ".join(str(line_number(text, offset)) for offset in references)
        violations.append(
            f"{INVALIDATE}( occurs {len(references)} times, expected exactly "
            f"{EXPECTED_REFERENCES} (definition plus 9 call sites); found on "
            f"line(s): {lines or 'none'}"
        )

    body = find_routine_body(text)
    if body is None:
        violations.append(
            f"could not locate the {ROUTINE_FUNCTION} definition and "
            "brace-match its body"
        )
    else:
        routine_text = text[body[0]:body[1]]
        for identifier in FORBIDDEN_IN_ROUTINE:
            offset = routine_text.find(identifier)
            if offset != -1:
                violations.append(
                    f"{ROUTINE_FUNCTION} calls {identifier} at line "
                    f"{line_number(text, body[0] + offset)}: the routine "
                    "prepared-frame retire must not reset the continuity "
                    "layer"
                )
    return violations


# ---------------------------------------------------------------------------
# Self-test: the checking logic is exercised on synthetic sources only.
# ---------------------------------------------------------------------------

SELF_TEST_CALL_SITES = (
    "EndPreparedFrameWithoutLayers",
    "EnterFrameWaitFatalDrain",
    "locReferenceSpaceChanged",
    "sessionExiting",
    "instanceLossPending",
    "seamIdentityChanged",
    "seamNotApplicable",
    "handednessChanged",
    "persistentGripOwnerBreak",
)


def build_sample(routine_body_lines: list[str], call_sites: tuple[str, ...]) -> str:
    parts = [
        "// Synthetic source for the aim-continuity lifecycle guard self-test.",
        "namespace",
        "{",
        f"    void {INVALIDATE}() noexcept",
        "    {",
        "        ResetAimContinuity(g_aimContinuityLayer.transition);",
        "    }",
        "",
    ]
    for site in call_sites:
        parts += [f"    void {site}()", "    {", f"        {INVALIDATE}();", "    }", ""]
    parts += [
        f"    void {ROUTINE_FUNCTION}()",
        "    {",
        *routine_body_lines,
        "    }",
        "",
        "} // namespace",
    ]
    return "\n".join(parts)


def require_violation(text: str, needle: str, message: str) -> None:
    violations = check_source(text)
    require(bool(violations), f"{message}: expected a violation, got none")
    require(
        any(needle in violation for violation in violations),
        f"{message}: expected a violation mentioning {needle!r}, got "
        f"{violations!r}",
    )


def run_self_test() -> None:
    good = build_sample(
        [
            "        // The layer deliberately survives the routine retire; a",
            "        // comment containing braces { } and the word "
            f'"{ROUTINE_FUNCTION}" must not confuse the matcher.',
            "        const char* label = \"}\";",
            "        g_preparedFrame.begun = false;",
        ],
        SELF_TEST_CALL_SITES,
    )
    good_violations = check_source(good)
    require(
        good_violations == [],
        f"the clean synthetic sample was rejected: {good_violations!r}",
    )

    routine_call = build_sample(
        [
            "        g_preparedFrame.begun = false;",
            f"        {INVALIDATE}();",
        ],
        SELF_TEST_CALL_SITES,
    )
    require_violation(
        routine_call,
        str(EXPECTED_REFERENCES + 1),
        "a routine-path invalidation call must be rejected",
    )

    wrong_count = build_sample(
        ["        g_preparedFrame.begun = false;"],
        SELF_TEST_CALL_SITES[:-1],
    )
    require_violation(
        wrong_count,
        str(EXPECTED_REFERENCES - 1),
        "a wrong invalidation call count must be rejected",
    )

    moved_call = build_sample(
        [
            "        g_preparedFrame.begun = false;",
            f"        {INVALIDATE}();",
        ],
        SELF_TEST_CALL_SITES[:-1],
    )
    require_violation(
        moved_call,
        ROUTINE_FUNCTION,
        "a routine-path call with the total count preserved must be rejected",
    )

    reset_in_routine = build_sample(
        [
            "        g_preparedFrame.begun = false;",
            f"        {RESET}(g_aimContinuityLayer.transition);",
        ],
        SELF_TEST_CALL_SITES,
    )
    require_violation(
        reset_in_routine,
        ROUTINE_FUNCTION,
        "a routine-path ResetAimContinuity call must be rejected",
    )

    unbalanced = "void OtherFunction()\n{\n}\n"
    require_violation(
        unbalanced,
        ROUTINE_FUNCTION,
        "a source without the routine definition must be rejected",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "source",
        nargs="?",
        type=Path,
        default=DEFAULT_SOURCE,
        help="vr.cpp to check (defaults to the repository source)",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="also exercise the checking logic on synthetic sources",
    )
    parser.add_argument(
        "--self-test-only",
        action="store_true",
        help="run only the synthetic self-test",
    )
    args = parser.parse_args()

    if args.self_test or args.self_test_only:
        run_self_test()
        if args.self_test_only:
            print("aim-continuity lifecycle guard self-test passed")
            return 0

    text = args.source.read_text(encoding="utf-8-sig")
    violations = check_source(text)
    if violations:
        for violation in violations:
            print(f"FAIL: {violation}", file=sys.stderr)
        return 1
    print(
        "aim-continuity lifecycle guard passed: "
        f"{EXPECTED_REFERENCES} {INVALIDATE}( references and no routine-path "
        f"reset in {args.source.as_posix()}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
