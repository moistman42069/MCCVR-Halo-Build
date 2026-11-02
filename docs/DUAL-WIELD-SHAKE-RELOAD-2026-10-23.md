> Partial headset result: H2 Anniversary works; H2 Classic and H3 failed.
> The pose clock and H3 scheduling below are superseded by
> `DUAL-SHAKE-CLOCK-AND-IDLE-2026-10-23.md`. Native request ABI evidence remains valid.

# Dual wield shake reload — October 23, 2026

Source candidate for `modded-weapons-fix`; Windows build and headset acceptance
pending. User compiles. Preserve the previous ODST candidate and user-confirmed
Reach/H3 modded-weapon behavior. Do not advance the accepted release pointer.

## Behavior

Weapons → Reload and holsters now has `dual wield shake reload`, described as
`shake to reload a weapon when dual wielding`. Default off, saved as
`dual_wield_shake_reload`, independent of Manual Reload. H2 and H3 only.

Each logical weapon slot has the existing Needler `SingleNeedleShake` detector:
one rapid out-and-back stroke, then settle before another request. The stroke
threshold is shared with Needler shake. Existing physical-to-logical handedness
mapping determines the corresponding native weapon. Both hands may request on
the same frame. Firing inhibits only that hand's gesture. No gesture ammo writes,
button ID guesses, or changes to authored reload eligibility/animations.

The XR thread publishes requests bound to title generation, space epoch, local
unit, both salted weapon datums and handedness. A nonblocking try-once channel
expires owner receipts and each request after 150 ms. Invalid tracking, menus,
non-gameplay, claimed support grip, holster capture and identity changes cancel
gestures. The simulation consumer independently checks local on-foot dual
ownership and rechecks each requested weapon immediately before calling native.
Native rejection (full magazine/no reserve/ineligible state) is left native.

## Own-title native evidence

The four supplied binaries match the SHA-256 identities pinned in
`tools/re/verify_dual_shake_reload.py`. Discovery used each title's kit; retail
verification uses the existing native-reload binding, with unique signature and
expected RVA checked on the cold worker. No new engine binding is introduced.
All addresses below are RVAs.

| Title | Kit request | Retail request | Magazine-zero unit actions |
|---|---|---|---|
| Halo 2 | `4A3350` | `8EADF0` | primary `0`, secondary `A` |
| Halo 3 | `A8BCE0` | `35F040` | primary `3D`, secondary `3F` |

H2 kit resolves owned weapon flag `+130`, owner `+158`, then compares the weapon
against owner roles `+226/+227` through `489E50`. Magazine zero selects `0/A`;
magazine one selects `1/B`. It calls the native unit-action dispatcher `4C8F60`.
Retail performs the same comparisons and calls `93B750`. Retail ABI is
`uint8_t(uint32_t weapon, int16_t magazine)`.

H3 kit resolves owned flag `+15D`, owner `+168`, compares owner roles
`+262/+263` through `A62B90`, and selects `3D/3F` for magazine zero or `3E/40`
for magazine one. It calls `A7E8D0`; success reports through `1AB5B0`.
Retail calls `379C08`, then `1FFB0` on success. ABI is
`uint8_t(uint32_t weapon, int16_t magazine, uint8_t continuation)`.
The new request passes magazine zero and continuation zero, retaining native
prediction/eligibility handling. Magazine index is NOT a hand index.

The existing policy calls these functions `Auto`, but suppression is restricted
to the separately proved empty-trigger caller return. Explicit shake requests
enter the hooked function from a different caller and retain native behavior
and existing reload policies. Native access faults disable shake admission only.

H2's action-name table at `A447F8` identifies input `2E` as `reload` and `2F` as
`exit_vehicle`; incrementing the reload input ID would be wrong. H3 unit action
`3F` also must not be confused with an input action ID. This implementation
bypasses neither eligibility nor animation by fabricating input records.

## Scheduling and diagnostics

H2 uses the existing admitted `unit_update_aiming` detour and the existing owned
FP dual-pair reader. H3 uses the existing weapon-update detour at `35F828`, after
the original returns, processing only the current primary weapon's update.
Retail callback-table pointer `8B1D78` targets that update. The already-proved
H3 haptic hook supplies the scheduling point, independently of haptic settings
and voice/fault state. If that hook cannot install, shake receives no native
receipts; it does not install a second hook or affect VR ownership.

Cold-worker log `Dual wield shake reload` includes binding readiness, native
owned-pair ticks, requested slots, accepted native calls and shake fault state.
`bindingReady=1` alone does not prove that the scheduling/owner path is running;
`nativeTicks` must advance while holding two weapons in gameplay.

## Verification and remaining acceptance

- 1,335 portable gesture/channel checks passed with GCC C++20, warnings as
  errors, ASan and UBSan: both titles/hands, simultaneous requests, left-handed
  logical slots, settle/rearm, head translation, transition cancellation,
  expired owner/requests, busy-hand cancellation and independent request expiry.
- Read-only four-binary verifier passed: pinned identities, kit and retail slot
  comparisons/action selection, unique existing reload signatures, H2 input
  names and H3 update callback pointer.
- Preserved production-fixture regressions passed with ASan/UBSan: ODST 4,742,
  H3 828, Reach 2,793 checks.
- Windows production-detour tests now exercise per-slot native calls, magazine
  and continuation arguments, ownership/generation/option/fault guards and config
  persistence. The H3 backend fixture includes the new callback dependency.
  These Windows tests have NOT run in this Linux environment.
- Full DLL build, cumulative CTest and headset verification remain outstanding.
  No DLL or package was produced here. Existing CI is pinned to older source;
  compile the new branch head, not the old workflow artifact.

Reproduce the portable test:

```sh
g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/dual_wield_shake_tests.cpp -o /tmp/dual-shake-tests
/tmp/dual-shake-tests
# Python environment with pefile and capstone; directory contains all four files.
python tools/re/verify_dual_shake_reload.py --inputs /path/to/binaries
```

Headset checks for H2 and H3: enable the option with Manual Reload off; partially
empty both weapons; shake each hand separately and then both; verify only the
requested gun reloads. Repeat with left-handed mode and manual reload policies.
Check full/empty-reserve guns, firing with the other hand, dropping/swapping the
secondary, pause/resume and option off. Confirm ordinary Needler/manual reload
and existing H3 tracking remain unchanged. Native acceptance counters help
separate gesture/ownership failure from engine rejection. Neither co-op nor
Store coverage is claimed without user results.
