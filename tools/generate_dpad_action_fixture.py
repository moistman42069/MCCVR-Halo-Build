"""Extract shipping OpenXR D-pad action setup/query for offline regression tests."""

import argparse
from pathlib import Path
from generate_haptics_runtime_fixture import extract_function


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source_path = args.source_root.resolve() / "src" / "dll" / "vr.cpp"
    source = source_path.read_text(encoding="utf-8-sig")
    output = ["// Generated from current shipping source; do not edit."]
    for signature in ("uint32_t WeaponGestureBinding(GameTitle title,vr_mapping::Action action,uint64_t now)",
                      "bool CreateControllerActions()", "bool ReadLeftThumbrestTouched()",
                      "void VR_GetPadState(VrPadState& out)"):
        body, line = extract_function(source, signature)
        output += [f'#line {line} "{source_path.as_posix()}"', body]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n\n".join(output) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
