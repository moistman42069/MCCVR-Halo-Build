"""Extract the shipping support-grip helpers for offline tests.

Three functions are taken verbatim from ``src/dll/vr.cpp``:

  * ``TryLocateSupportGripPosition`` - the OpenXR support-grip locator;
  * ``TwoHandGrabZoneHit`` - the BASE (PG-off) acquisition geometry;
  * ``PersistentSupportGrabZoneHit`` - the PG-on A003 acquisition helper.

The grip locator has no feature/config gate: admission is the OpenXR
action/space availability plus the runtime's own tracked validity, because the
sampled grip positions are the positional pivots of the free two-hand
production geometry (Grip -> Grip) and of the support endpoint selector, and
must exist whatever Virtual Stock or "Reduce Support-Hand Rotation" is
selected. The fixture therefore proves the tracking-only admission rules (null
action/space, inactive action, failed state/locate, missing position-validity
bit and non-finite positions all fail closed) rather than any enable flag.

The base and persistent helpers are extracted together so one fixture can
prove the gate seam on the shipping code itself: the base helper keeps the
pre-fix palm-depth shift along the support controller's own forward (so a
wrist rotation can swing acquisition), while the persistent helper evaluates
the pure A003 predicate on the primary acquisition axis and never reads the
support wrist orientation.
"""

import argparse
from pathlib import Path
from generate_haptics_runtime_fixture import extract_function


SIGNATURES = (
    "bool TryLocateSupportGripPosition(XrPath handPath,\n"
    "                                      XrSpace gripSpace, XrTime time,\n"
    "                                      XrVector3f& outPosition)",
    "bool TwoHandGrabZoneHit(const XrPosef& rpose,\n"
    "        const XrPosef& lpose) noexcept",
    "bool PersistentSupportGrabZoneHit(const XrPosef& rpose,\n"
    "        const XrPosef& lpose) noexcept",
)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source_path = args.source_root.resolve() / "src" / "dll" / "vr.cpp"
    source = source_path.read_text(encoding="utf-8-sig")
    output = [
        "// Generated from current shipping source; do not edit.",
    ]
    for signature in SIGNATURES:
        body, line = extract_function(source, signature)
        output.append(f'#line {line} "{source_path.as_posix()}"')
        output.append(body)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n\n".join(output) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
