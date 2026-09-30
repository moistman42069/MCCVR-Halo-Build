"""Extract shipping haptic functions for the offline XInput/OpenXR fixture.

No production source is edited. The fixture compiles the actual function bodies
against a deterministic clock and captured OpenXR calls, not a rewritten model.
"""

import argparse
from pathlib import Path
import re


def extract_function(source: str, signature: str) -> tuple[str, int]:
    matches = list(re.finditer(r"(?m)^\s*" + re.escape(signature) + r"\s*\{", source))
    if len(matches) != 1:
        raise ValueError(f"Expected one definition of {signature!r}, found {len(matches)}")
    start = matches[0].start()
    opening = source.index("{", start)
    depth, index = 1, opening + 1
    while index < len(source):
        if source.startswith("//", index):
            end = source.find("\n", index + 2)
            index = len(source) if end < 0 else end + 1
            continue
        if source.startswith("/*", index):
            end = source.find("*/", index + 2)
            if end < 0:
                raise ValueError("Unterminated block comment")
            index = end + 2
            continue
        char = source[index]
        if char in ('"', "'"):
            quote = char
            index += 1
            while index < len(source):
                if source[index] == "\\":
                    index += 2
                elif source[index] == quote:
                    index += 1
                    break
                else:
                    index += 1
            continue
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if not depth:
                return source[start:index + 1].strip(), source.count("\n", 0, start) + 1
        index += 1
    raise ValueError(f"Unclosed function {signature}")


def constant(source: str, name: str, emitted_name: str | None = None) -> str:
    matches = list(re.finditer(r"constexpr\s+uint32_t\s+" + re.escape(name) + r"\s*=([^;]+);", source))
    if len(matches) != 1:
        raise ValueError(f"Expected one uint32_t constant {name}, found {len(matches)}")
    return f"constexpr uint32_t {emitted_name or name} ={matches[0].group(1)};"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.source_root.resolve()
    paths = {name: root / "src" / "dll" / name for name in
             ("input.cpp", "vr.cpp", "game.cpp", "haloce_stereo_core.cpp")}
    sources = {name: path.read_text(encoding="utf-8-sig") for name, path in paths.items()}
    ce_poll, _ = extract_function(sources["haloce_stereo_core.cpp"],
                                 "bool HaloCE_Poll(uintptr_t base,size_t size,uint32_t gen,bool isActive,bool allowInitialInstall) noexcept")
    output = ["// Generated from current shipping source; do not edit.",
              constant(sources["game.cpp"], "kRuntimeCapabilitiesRequiringArm"),
              constant(sources["game.cpp"], "kHalo3RuntimeCapabilities"),
              constant(ce_poll, "capabilities", "kCePublishedCapabilities")]
    definitions = (
        ("game.cpp", "bool Game_HasTitleCapability(uint32_t requiredCapabilities)"),
        ("vr.cpp", "void VR_SetGameHaptics(float amplitude)"),
        ("vr.cpp", "void VR_PulseContactHaptics(bool left, float amplitude)"),
        ("vr.cpp", "uint64_t VR_WeaponHapticToken(GameTitle title,uint32_t generation,bool secondary) noexcept"),
        ("vr.cpp", "bool VR_PulseWeaponHaptics(GameTitle title,uint32_t generation,bool secondary,\n    bool supported,float amplitude,uint64_t sourceToken) noexcept"),
        ("vr.cpp", "void StopControllerHaptics()"),
        ("vr.cpp", "void ApplyControllerHaptics(bool primaryTrackingValid, bool supportTrackingValid)"),
        ("input.cpp", "DWORD ProcessSetState(DWORD result, DWORD user, XINPUT_VIBRATION* vibration)"),
    )
    for name, signature in definitions:
        body, line = extract_function(sources[name], signature)
        output += [f'#line {line} "{paths[name].as_posix()}"', body]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n\n".join(output) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
