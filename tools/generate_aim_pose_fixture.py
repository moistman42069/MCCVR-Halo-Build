"""Extract the shipping aim-pose structs and solver for offline tests."""

import argparse
from pathlib import Path

from generate_haptics_runtime_fixture import extract_function


def extract_struct(source: str, name: str) -> tuple[str, int]:
    body, line = extract_function(source, f"struct {name}")
    return body + ";", line


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source_path = args.source_root.resolve() / "src" / "dll" / "vr.cpp"
    source = source_path.read_text(encoding="utf-8-sig")
    stock_path = args.source_root.resolve() / "src" / "dll" / "virtual_stock_aim.inl"
    stock = stock_path.read_text(encoding="utf-8-sig")

    # The recipient tree keeps the product aim solver and its settings plumbing
    # in the extracted virtual_stock_aim.inl; the shipped call sites (structs,
    # CurrentAimPoseInputs, the ComputeAimPose overloads) stay in vr.cpp. Search
    # both files so the same signatures are extracted from wherever the merged
    # source defines them - the extraction semantics do not change.
    candidates = ((source, source_path), (stock, stock_path))

    def find_function(signature: str) -> tuple[str, int, Path]:
        for text, path in candidates:
            try:
                body, line = extract_function(text, signature)
                return body, line, path
            except ValueError:
                continue
        raise SystemExit(f"aim_pose fixture: signature not found: {signature!r}")

    inputs, inputs_line, inputs_path = find_function("struct AimPoseInputs")
    inputs += ";"
    result, result_line, result_path = find_function("struct AimPoseResult")
    result += ";"
    apply_settings, apply_settings_line, apply_settings_path = find_function(
        "void ApplyVirtualStockAimSettings(\n"
        "        AimPoseInputs& inputs, const VirtualStockAimSettings& settings,\n"
        "        VirtualStockTestProfile profile) noexcept",
    )
    settings_from_inputs, settings_from_inputs_line, settings_from_inputs_path = \
        find_function(
            "VirtualStockAimSettings VirtualStockAimSettingsFromAimPoseInputs(\n"
            "        const AimPoseInputs& inputs) noexcept",
        )
    inputs_for_profile, inputs_for_profile_line, inputs_for_profile_path = \
        find_function(
            "AimPoseInputs AimPoseInputsForProfile(\n"
            "        const AimPoseInputs& base,\n"
            "        VirtualStockTestProfile profile) noexcept",
        )
    inputs_for_selected_profile, inputs_for_selected_profile_line, \
        inputs_for_selected_profile_path = find_function(
            "AimPoseInputs AimPoseInputsForSelectedProfile(\n"
            "        const AimPoseInputs& base,\n"
            "        const VirtualStockAimSettings& userSettings,\n"
            "        VirtualStockTestProfile selectedProfile) noexcept",
        )
    solver_impl, solver_impl_line, solver_impl_path = find_function(
        "template <bool CaptureTrace>\n"
        "    AimPoseResult ComputeAimPoseImpl(\n"
        "        const AimPoseInputs& inputs, AimPoseTrace* trace) noexcept",
    )
    solver, solver_line, solver_path = find_function(
        "AimPoseResult ComputeAimPose(const AimPoseInputs& inputs) noexcept",
    )
    traced_solver, traced_solver_line, traced_solver_path = find_function(
        "AimPoseResult ComputeAimPose(\n"
        "        const AimPoseInputs& inputs, AimPoseTrace* trace) noexcept",
    )
    output = [
        "// Generated from current shipping source; do not edit.",
        f'#line {inputs_line} "{inputs_path.as_posix()}"',
        inputs,
        f'#line {result_line} "{result_path.as_posix()}"',
        result,
        f'#line {apply_settings_line} "{apply_settings_path.as_posix()}"',
        apply_settings,
        f'#line {settings_from_inputs_line} "{settings_from_inputs_path.as_posix()}"',
        settings_from_inputs,
        f'#line {inputs_for_profile_line} "{inputs_for_profile_path.as_posix()}"',
        inputs_for_profile,
        f'#line {inputs_for_selected_profile_line} "{inputs_for_selected_profile_path.as_posix()}"',
        inputs_for_selected_profile,
        f'#line {solver_impl_line} "{solver_impl_path.as_posix()}"',
        solver_impl,
        f'#line {solver_line} "{solver_path.as_posix()}"',
        solver,
        f'#line {traced_solver_line} "{traced_solver_path.as_posix()}"',
        traced_solver,
    ]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n\n".join(output) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
