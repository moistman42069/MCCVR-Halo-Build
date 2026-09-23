"""Extract the shipping stock/controller aim solvers for geometry regression tests."""
import argparse
from pathlib import Path
from generate_haptics_runtime_fixture import extract_function


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.source_root.resolve()
    source_path = root / "src/dll/vr.cpp"
    source = source_path.read_text(encoding="utf-8-sig")
    stock_path = root / "src/dll/virtual_stock_aim.inl"
    stock = stock_path.read_text(encoding="utf-8-sig")
    output = ["// Generated from current production source. Do not edit."]
    for signature in (
        "struct AimPoseInputs", "struct AimPoseResult",
        "XrVector3f Rotate(const XrQuaternionf& q, const XrVector3f& v)",
        "XrVector3f LeftHandPointWithOffsets(\n        const XrPosef& lpose, float handForwardM, float gripForwardM)",
    ):
        body, line = extract_function(source, signature)
        output += [f'#line {line} "{source_path.as_posix()}"', body + (";" if signature.startswith("struct ") else "")]
    for text, path, signature in (
        (stock, stock_path, "AimPoseResult ComputeStockAimPose(const AimPoseInputs& inputs) noexcept"),
        (source, source_path, "AimPoseResult ComputeAimPose(const AimPoseInputs& inputs) noexcept"),
    ):
        body, line = extract_function(text, signature)
        output += [f'#line {line} "{path.as_posix()}"', body]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n\n".join(output) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
