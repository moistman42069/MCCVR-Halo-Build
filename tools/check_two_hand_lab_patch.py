"""Guard the Two-Handed Lab patch (hidden sidebar, presets, reset) wiring.

The Lab page is runtime-only experimental infrastructure hidden from normal
F1 navigation exactly like the Virtual Stock Lab, its quick presets are
complete deterministic states, and the global Reset ALL also resets the Lab.
That invariant lives in the wiring between several files rather than in any
state machine, so no behavioural test can catch its silent breakage (a
re-added sidebar entry or Advanced doorway, a read-modify-write preset, a
reset that forgets the Lab store, or the Lab page and its suites being
deleted along with its navigation entry all build and pass while corrupting
or losing the experiment).

The check is deliberately narrow. It asserts only:

  1. ``src/dll/menu.cpp`` hides ``Cat_VirtualStockLab`` from the sidebar
     (an ``if (i == ...)`` skip whose body is ``continue;``) and hides
     ``Cat_TwoHandLab`` the same way, so neither Lab has a normal sidebar
     entry;
  2. ``src/dll/menu.cpp`` still renders the Lab page behind the hidden
     category (``g_activeCategory == Cat_TwoHandLab`` and the
     ``two_hand_lab_menu.inl`` include): the hide removes the navigation
     entry only, never the page, its controls, or its state;
  3. ``src/dll/menu.cpp`` contains no ``Open Two-Handed Lab`` button
     (the old Advanced doorway stays retired);
  4. ``src/dll/two_hand_lab_menu.inl`` contains no ``< Back to Weapon & Aim``
     button (a hidden Lab has no other navigation target);
  5. ``src/dll/two_hand_lab_menu.inl`` reports
     ``Numerical two-hand steering`` and no longer reports ``Two-hand aim:``
     (the reading derives from the numerical aim output, not a latch);
  6. the global reset path in ``src/dll/menu.cpp`` calls
     ``two_hand_lab_runtime::ResetSettings(``, while
     ``src/dll/virtual_stock_menu.inl`` calls no
     ``two_hand_lab_runtime::`` function (the VS-specific reset must not
     touch the Lab);
  7. the preset region of ``src/dll/two_hand_lab_menu.inl`` (between the
     ``Quick presets`` and ``Telemetry`` markers) calls
     ``two_hand_lab::QuickPresetSettings(`` and never reads live state via
     ``two_hand_lab_runtime::GetSettings(``;
  8. no ``two_hand_lab_runtime::SetSettings(`` or ``ResetSettings(`` call
     occurs before the ``Enable Lab Overrides`` checkbox in
     ``src/dll/two_hand_lab_menu.inl`` (opening the page never mutates;
     comments naming the API do not count);
  9. ``src/common/two_hand_lab_logic.h`` no longer claims
     ``nothing outside tests includes this header``;
 10. ``CMakeLists.txt`` still registers the Lab's suites and this guard
     (``halomccvr_two_hand_lab_tests``, ``..._solver_tests``,
     ``..._temporal_tests``, ``..._patch_guard``): hiding the Lab from the
     sidebar must not delete its test, telemetry, or guard infrastructure.

It deliberately does not freeze the surrounding implementation text: labels,
layout, and solver semantics stay with human review. ``--self-test``
exercises this checking logic against synthetic sources and then checks the
real tree; ``--self-test-only`` runs the synthetic fixtures alone.
``--root`` points the real-source check at another tree (used to demonstrate
a failing tree without touching the real sources).
"""

import argparse
import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MENU = REPO_ROOT / "src" / "dll" / "menu.cpp"
DEFAULT_LAB_MENU = REPO_ROOT / "src" / "dll" / "two_hand_lab_menu.inl"
DEFAULT_LOGIC = REPO_ROOT / "src" / "common" / "two_hand_lab_logic.h"
DEFAULT_VS_MENU = REPO_ROOT / "src" / "dll" / "virtual_stock_menu.inl"
DEFAULT_CMAKE = REPO_ROOT / "CMakeLists.txt"

SKIP_IF = re.compile(r"^\s*if\s*\(\s*i\s*==(.*)\)\s*$", re.MULTILINE)
VS_SKIP = "Cat_VirtualStockLab"
LAB_SKIP = "Cat_TwoHandLab"
LAB_PAGE_INCLUDE = '#include "two_hand_lab_menu.inl"'
LAB_PAGE_DISPATCH = "g_activeCategory == Cat_TwoHandLab"
DOORWAY_BUTTON = "Open Two-Handed Lab"
BACK_BUTTON = "< Back to Weapon & Aim"
STEERING_LABEL = "Numerical two-hand steering"
LATCH_LABEL = "Two-hand aim:"
LAB_RESET = "two_hand_lab_runtime::ResetSettings("
LAB_SET = "two_hand_lab_runtime::SetSettings("
LAB_GET = "two_hand_lab_runtime::GetSettings("
LAB_ANY = "two_hand_lab_runtime::"
PRESET_BUILDER = "two_hand_lab::QuickPresetSettings("
PRESET_START = "Quick presets"
PRESET_END = "Telemetry"
ENABLE_CHECKBOX = "Enable Lab Overrides"
STALE_HEADER_CLAIM = "nothing outside tests includes this header"
LAB_TEST_REGISTRATIONS = (
    "halomccvr_two_hand_lab_tests",
    "halomccvr_two_hand_lab_solver_tests",
    "halomccvr_two_hand_lab_temporal_tests",
    "halomccvr_two_hand_lab_patch_guard",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def strip_line_comments(text: str) -> str:
    """Remove ``//`` comments so checks match code, not prose about code."""
    return re.sub(r"//[^\n]*", "", text)


def nav_skips(text: str) -> list[tuple[int, str]]:
    """Return the (line, condition) pairs of ``if (i == ...) continue;`` skips."""
    skips: list[tuple[int, str]] = []
    for match in SKIP_IF.finditer(text):
        rest = text[match.end():]
        for line in rest.splitlines():
            stripped = line.strip()
            if not stripped:
                continue
            if stripped == "continue;":
                skips.append((line_number(text, match.start()), match.group(1)))
            break
    return skips


def preset_region(text: str) -> str | None:
    """Return the quick-preset section between its two markers, if found."""
    start = text.find(PRESET_START)
    if start == -1:
        return None
    end = text.find(PRESET_END, start + len(PRESET_START))
    if end == -1:
        return None
    return text[start:end]


def check_tree(menu_text: str, lab_menu_text: str, logic_text: str,
               vs_menu_text: str, cmake_text: str) -> list[str]:
    """Return the violation messages for the five sources (empty when clean)."""
    violations: list[str] = []

    skips = nav_skips(menu_text)
    if not any(VS_SKIP in condition for _, condition in skips):
        violations.append(
            f"menu.cpp has no sidebar skip for {VS_SKIP}: the Virtual Stock "
            "Lab must stay hidden from normal navigation"
        )
    if not any(LAB_SKIP in condition for _, condition in skips):
        violations.append(
            f"menu.cpp has no sidebar skip for {LAB_SKIP}: the Two-Handed "
            "Lab is runtime-only experimental infrastructure and must stay "
            "out of normal navigation, exactly like the Virtual Stock Lab"
        )

    if LAB_PAGE_DISPATCH not in menu_text:
        violations.append(
            f"menu.cpp no longer renders the Lab page ({LAB_PAGE_DISPATCH}): "
            "hiding the Lab from navigation must not delete the page"
        )
    if LAB_PAGE_INCLUDE not in menu_text:
        violations.append(
            f"menu.cpp no longer includes the Lab page ({LAB_PAGE_INCLUDE}): "
            "hiding the Lab from navigation must not delete the page"
        )

    if DOORWAY_BUTTON in menu_text:
        violations.append(
            f"menu.cpp still contains {DOORWAY_BUTTON!r}: the Lab has no "
            "normal navigation entry, and the Advanced doorway stays retired"
        )

    if BACK_BUTTON in lab_menu_text:
        violations.append(
            f"two_hand_lab_menu.inl still contains {BACK_BUTTON!r}: a hidden "
            "Lab page has no other navigation target"
        )

    if STEERING_LABEL not in lab_menu_text:
        violations.append(
            "two_hand_lab_menu.inl does not report "
            f"{STEERING_LABEL!r}: the status line must use numerical wording"
        )
    if LATCH_LABEL in lab_menu_text:
        violations.append(
            f"two_hand_lab_menu.inl still contains {LATCH_LABEL!r}: the "
            "reading derives from the numerical aim output, not a latch"
        )

    if LAB_RESET not in menu_text:
        violations.append(
            "menu.cpp never calls two_hand_lab_runtime::ResetSettings(: the "
            "global Reset ALL must also reset and disable the Lab"
        )
    if LAB_ANY in vs_menu_text:
        violations.append(
            "virtual_stock_menu.inl calls a two_hand_lab_runtime:: function: "
            "the Virtual Stock reset must never touch the Lab"
        )

    region = preset_region(lab_menu_text)
    if region is None:
        violations.append(
            "two_hand_lab_menu.inl has no Quick presets/Telemetry region: "
            "the preset wiring cannot be located"
        )
    else:
        if PRESET_BUILDER not in region:
            violations.append(
                "the preset region never calls "
                "two_hand_lab::QuickPresetSettings(: presets must be "
                "complete deterministic states, not read-modify-write"
            )
        if LAB_GET in region:
            violations.append(
                "the preset region reads "
                "two_hand_lab_runtime::GetSettings(: presets must not depend "
                "on prior state"
            )

    enable_at = lab_menu_text.find(ENABLE_CHECKBOX)
    if enable_at == -1:
        violations.append(
            "two_hand_lab_menu.inl has no Enable Lab Overrides checkbox: "
            "the page-open non-mutation boundary cannot be located"
        )
    else:
        # The file header documents the Set/Reset surface in a comment;
        # only real calls before the checkbox would mutate on page open.
        prefix = strip_line_comments(lab_menu_text[:enable_at])
        for call in (LAB_SET, LAB_RESET):
            if call in prefix:
                violations.append(
                    f"{call} occurs before the Enable Lab Overrides "
                    "checkbox: opening the page must never mutate the Lab"
                )

    if STALE_HEADER_CLAIM in logic_text:
        violations.append(
            f"two_hand_lab_logic.h still claims {STALE_HEADER_CLAIM!r}: the "
            "pure module is shared by the runtime, the menu, and the tests"
        )

    for registration in LAB_TEST_REGISTRATIONS:
        if registration not in cmake_text:
            violations.append(
                f"CMakeLists.txt no longer registers {registration}: hiding "
                "the Lab from navigation must not delete its test, telemetry "
                "or guard infrastructure"
            )

    return violations


# ---------------------------------------------------------------------------
# Self-test: the checking logic is exercised on synthetic sources only.
# ---------------------------------------------------------------------------

GOOD_MENU = """\
// Synthetic menu.cpp for the two-hand-lab patch guard self-test.
        for (int i = 0; i < Cat_Count; ++i)
        {
            // Virtual Stock Lab is intentionally excluded from normal navigation;
            // product controls live in Weapon & Aim.
            if (i == Cat_VirtualStockLab)
                continue;
            // Two-Handed Lab is runtime-only experimental infrastructure hidden
            // the same way: no normal navigation entry. Its page block stays.
            if (i == Cat_TwoHandLab)
                continue;
        }

        if (g_activeCategory == Cat_TwoHandLab)
        {
#include "two_hand_lab_menu.inl"
        }
"""

GOOD_MENU_RESET = """\
            if (g_resetArmed)
            {
                g_config = Config{};
                ConfigSave();
                two_hand_lab_runtime::ResetSettings();
                g_resetArmed = false;
            }
"""

GOOD_LAB_MENU = """\
// Synthetic two_hand_lab_menu.inl for the patch guard self-test.
// Every write goes through two_hand_lab_runtime::SetSettings()/
// two_hand_lab_runtime::ResetSettings(); opening the page never enables.
        ImGui::Text("Two-Handed Lab (experimental)");
        two_hand_lab::Settings labSettings = two_hand_lab_runtime::GetSettings();
        ImGui::Text("Numerical two-hand steering: %s", "Active");
        if (ImGui::Checkbox("Enable Lab Overrides", &labEnabled))
        {
            two_hand_lab_runtime::SetSettings(labSettings);
        }
        ImGui::Text("Quick presets (runtime state only)");
        if (ImGui::Button("GG 50"))
            two_hand_lab_runtime::SetSettings(
                two_hand_lab::QuickPresetSettings(two_hand_lab::QuickPreset::GG50));
        ImGui::Text("Telemetry");
"""

GOOD_LOGIC = """\
// Synthetic two_hand_lab_logic.h for the patch guard self-test.
// Landed architecture: this pure module is shared by the runtime solver
// integration, the temporal fragment, the menu, and the tests.
"""

GOOD_VS_MENU = """\
// Synthetic virtual_stock_menu.inl for the patch guard self-test.
            if (ImGui::Button("Reset##virtualstockreset"))
            {
                ResetVirtualStockSettings(g_config);
            }
"""

GOOD_CMAKE = """\
# Synthetic CMakeLists.txt for the patch guard self-test.
    add_executable(halomccvr_two_hand_lab_tests tests/two_hand_lab_logic_tests.cpp)
    target_include_directories(halomccvr_two_hand_lab_tests PRIVATE src/common)
    add_test(NAME halomccvr_two_hand_lab_tests COMMAND halomccvr_two_hand_lab_tests)
    add_executable(halomccvr_two_hand_lab_solver_tests
        tests/two_hand_lab_solver_tests.cpp)
    add_test(NAME halomccvr_two_hand_lab_solver_tests COMMAND halomccvr_two_hand_lab_solver_tests)
    add_executable(halomccvr_two_hand_lab_temporal_tests
        tests/two_hand_lab_temporal_tests.cpp)
    add_test(NAME halomccvr_two_hand_lab_temporal_tests COMMAND halomccvr_two_hand_lab_temporal_tests)
    add_test(NAME halomccvr_two_hand_lab_patch_guard
        COMMAND "${Python3_EXECUTABLE}" -O
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_two_hand_lab_patch.py"
            --self-test)
"""


def good_texts() -> tuple[str, str, str, str, str]:
    return (
        GOOD_MENU + GOOD_MENU_RESET,
        GOOD_LAB_MENU,
        GOOD_LOGIC,
        GOOD_VS_MENU,
        GOOD_CMAKE,
    )


def require_violation(sources: dict[str, str], needle: str, message: str) -> None:
    violations = check_tree(
        sources["menu"], sources["lab_menu"], sources["logic"],
        sources["vs_menu"], sources["cmake"])
    require(bool(violations), f"{message}: expected a violation, got none")
    require(
        any(needle in violation for violation in violations),
        f"{message}: expected a violation mentioning {needle!r}, got "
        f"{violations!r}",
    )


def with_menu(fragment: str) -> dict[str, str]:
    menu, lab_menu, logic, vs_menu, cmake = good_texts()
    return {"menu": fragment, "lab_menu": lab_menu, "logic": logic,
            "vs_menu": vs_menu, "cmake": cmake}


def with_lab_menu(fragment: str) -> dict[str, str]:
    menu, _, logic, vs_menu, cmake = good_texts()
    return {"menu": menu, "lab_menu": fragment, "logic": logic,
            "vs_menu": vs_menu, "cmake": cmake}


def with_cmake(fragment: str) -> dict[str, str]:
    menu, lab_menu, logic, vs_menu, _ = good_texts()
    return {"menu": menu, "lab_menu": lab_menu, "logic": logic,
            "vs_menu": vs_menu, "cmake": fragment}


LAB_SKIP_BLOCK = "            if (i == Cat_TwoHandLab)\n                continue;\n"


def run_self_test() -> None:
    menu, lab_menu, logic, vs_menu, cmake = good_texts()
    clean = check_tree(menu, lab_menu, logic, vs_menu, cmake)
    require(clean == [], f"the clean synthetic tree was rejected: {clean!r}")

    # The hide is accepted as its own skip statement (the clean fixture above)
    # or as an extra ``||`` term on the Virtual Stock skip.
    combined_hide = (
        GOOD_MENU.replace(LAB_SKIP_BLOCK, "").replace(
            "if (i == Cat_VirtualStockLab)",
            "if (i == Cat_VirtualStockLab || i == Cat_TwoHandLab)")
        + GOOD_MENU_RESET
    )
    require(
        check_tree(combined_hide, lab_menu, logic, vs_menu, cmake) == [],
        "a combined Virtual Stock/Two-Handed Lab sidebar skip must be accepted")

    missing_lab_skip = (
        GOOD_MENU.replace(LAB_SKIP_BLOCK, "") + GOOD_MENU_RESET)
    require_violation(
        with_menu(missing_lab_skip), "Cat_TwoHandLab",
        "a sidebar that still lists the Lab must be rejected")

    deleted_dispatch = with_menu(GOOD_MENU.replace(
        "g_activeCategory == Cat_TwoHandLab",
        "g_activeCategory == Cat_Crosshair") + GOOD_MENU_RESET)
    require_violation(
        deleted_dispatch, "no longer renders the Lab page",
        "deleting the Lab page dispatch along with the hide must be rejected")

    deleted_include = with_menu(GOOD_MENU.replace(
        '#include "two_hand_lab_menu.inl"', "// page removed")
        + GOOD_MENU_RESET)
    require_violation(
        deleted_include, "no longer includes the Lab page",
        "deleting the Lab page include along with the hide must be rejected")

    missing_vs_skip = GOOD_MENU.replace(
        "if (i == Cat_VirtualStockLab)", "if (i == Cat_Crosshair)")
    require_violation(
        with_menu(missing_vs_skip + GOOD_MENU_RESET), "Cat_VirtualStockLab",
        "a missing Virtual Stock sidebar skip must be rejected")

    doorway = GOOD_MENU + GOOD_MENU_RESET + \
        '        if (ImGui::Button("Open Two-Handed Lab"))\n'
    require_violation(
        with_menu(doorway), "Open Two-Handed Lab",
        "the Advanced doorway button must be rejected")

    no_reset = GOOD_MENU
    require_violation(
        with_menu(no_reset), "ResetSettings(",
        "a global reset that forgets the Lab must be rejected")

    vs_touches_lab = GOOD_VS_MENU + \
        "                two_hand_lab_runtime::ResetSettings();\n"
    texts = with_menu(GOOD_MENU + GOOD_MENU_RESET)
    texts["vs_menu"] = vs_touches_lab
    require_violation(
        texts, "virtual_stock_menu.inl",
        "a Virtual Stock reset touching the Lab must be rejected")

    back_button = GOOD_LAB_MENU.replace(
        'ImGui::Text("Two-Handed Lab (experimental)");',
        'if (ImGui::Button("< Back to Weapon & Aim"))\n'
        '        ImGui::Text("Two-Handed Lab (experimental)");')
    require_violation(
        with_lab_menu(back_button), "Back to Weapon",
        "the Back button must be rejected")

    latch_wording = GOOD_LAB_MENU.replace(
        "Numerical two-hand steering", "Two-hand aim:")
    require_violation(
        with_lab_menu(latch_wording), "Two-hand aim:",
        "latch-style status wording must be rejected")

    missing_steering = GOOD_LAB_MENU.replace(
        "Numerical two-hand steering: %s", "Steering: %s")
    require_violation(
        with_lab_menu(missing_steering), "Numerical two-hand steering",
        "a missing numerical status label must be rejected")

    live_read_preset = GOOD_LAB_MENU.replace(
        "two_hand_lab::QuickPresetSettings(two_hand_lab::QuickPreset::GG50)",
        "labApplyPreset(two_hand_lab::AnchorMode::GG, 0.50f)")
    live_read_preset = live_read_preset.replace(
        "two_hand_lab::Settings labSettings = "
        "two_hand_lab_runtime::GetSettings();",
        "two_hand_lab::Settings labSettings = "
        "two_hand_lab_runtime::GetSettings();\n"
        "        two_hand_lab::Settings preset = "
        "two_hand_lab_runtime::GetSettings();")
    live_read_region = live_read_preset.replace(
        'ImGui::Text("Telemetry");',
        'two_hand_lab::Settings probe = two_hand_lab_runtime::GetSettings();\n'
        '        ImGui::Text("Telemetry");')
    require_violation(
        with_lab_menu(live_read_region), "GetSettings(",
        "a preset region reading live settings must be rejected")

    no_builder = GOOD_LAB_MENU.replace(
        "two_hand_lab::QuickPresetSettings(",
        "two_hand_lab::DefaultSettings(")
    require_violation(
        with_lab_menu(no_builder), "QuickPresetSettings(",
        "a preset region without the builder must be rejected")

    early_set = GOOD_LAB_MENU.replace(
        'ImGui::Text("Two-Handed Lab (experimental)");',
        'two_hand_lab_runtime::SetSettings(labSettings);\n'
        '        ImGui::Text("Two-Handed Lab (experimental)");')
    require_violation(
        with_lab_menu(early_set), "SetSettings(",
        "a page-open mutation must be rejected")

    stale_logic = GOOD_LOGIC + \
        "// No runtime integration yet: nothing outside tests includes " \
        "this header yet.\n"
    texts = with_menu(GOOD_MENU + GOOD_MENU_RESET)
    texts["logic"] = stale_logic
    require_violation(
        texts, "nothing outside tests includes this header",
        "the stale header claim must be rejected")

    retired_suite = GOOD_CMAKE.replace(
        "halomccvr_two_hand_lab_solver_tests",
        "halomccvr_lab_solver_tests")
    require_violation(
        with_cmake(retired_suite), "halomccvr_two_hand_lab_solver_tests",
        "a retired Lab solver suite must be rejected")

    retired_guard = GOOD_CMAKE.replace(
        "halomccvr_two_hand_lab_patch_guard",
        "halomccvr_lab_patch_guard")
    require_violation(
        with_cmake(retired_guard), "halomccvr_two_hand_lab_patch_guard",
        "a retired Lab patch guard must be rejected")


def read_tree(root: Path) -> tuple[str, str, str, str, str]:
    paths = {
        "menu.cpp": root / "src" / "dll" / "menu.cpp",
        "two_hand_lab_menu.inl": root / "src" / "dll" / "two_hand_lab_menu.inl",
        "two_hand_lab_logic.h":
            root / "src" / "common" / "two_hand_lab_logic.h",
        "virtual_stock_menu.inl":
            root / "src" / "dll" / "virtual_stock_menu.inl",
        "CMakeLists.txt": root / "CMakeLists.txt",
    }
    texts: dict[str, str] = {}
    for name, path in paths.items():
        require(path.is_file(), f"expected source file {path.as_posix()}")
        texts[name] = path.read_text(encoding="utf-8-sig")
    return (texts["menu.cpp"], texts["two_hand_lab_menu.inl"],
            texts["two_hand_lab_logic.h"],
            texts["virtual_stock_menu.inl"], texts["CMakeLists.txt"])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        type=Path,
        default=REPO_ROOT,
        help="repository root holding the five checked sources",
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
            print("two-hand-lab patch guard self-test passed")
            return 0

    menu_text, lab_menu_text, logic_text, vs_menu_text, cmake_text = \
        read_tree(args.root)
    violations = check_tree(menu_text, lab_menu_text, logic_text,
                            vs_menu_text, cmake_text)
    if violations:
        for violation in violations:
            print(f"FAIL: {violation}", file=sys.stderr)
        return 1
    print(
        "two-hand-lab patch guard passed: sidebar hide, no doorway, presets, "
        "reset, page wiring, header wiring, and suite registration clean "
        f"under {args.root.as_posix()}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
