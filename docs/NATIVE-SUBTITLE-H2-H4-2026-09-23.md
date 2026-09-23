# Halo 2 and Halo 4 native subtitle handoff evidence

Read-only official-kit discovery and pinned-retail matching. No headset result,
game launch, game-file write or new hook is implied by this evidence alone.
The renderer integration is independent of native camera ownership and must
retain the original native call and return value.

## Inputs

| Input | SHA-256 |
| --- | --- |
| H2EK `halo2_tag_test.exe` | `D0B71186D3948C48DDD02E2CCB88FA13E77E25A3D8F7FA60922F23A2A0073E36` |
| `halo2.dll` | `DE65B4F4FDBF3F0A5EAB7431FE530DA17DD815599182DFD6AE9B7E21CF171946` |
| H4EK `halo4_tag_test.exe` | `B7468DB9FD160B035C329540EE0B0D47BCF609E1BA6E85AE4F204B70661113A6` |
| `halo4.dll` | `7C53E7D5BC9848545A1B70E2768242479336FBA1B7630D7AB955F7FD0C34FA84` |

All addresses below are RVAs. The H2 kit image base is `400000`; e.g. kit
RVA `D5EE0` is absolute address `004D5EE0`. H4 kit and retail are x64.

## Halo 2 speech and named subtitle paths

Official-kit speech function `564B0` names the `subtitles\\%S` lookup,
obtains localized UTF-16 through the host interface, derives sound lifetime
and colors, then invokes interface slot `+164`. Its retail match `6B7ED0`
preserves these operations; the 64-bit interface localizer is `+1D0` and the
final subtitle handoff is `+2C8`. This is an independently matched pointer-size
change, not an assumed offset copied from another title.

The final retail call at `6B825A`, returning to `6B8260`, passes:

- RCX: host interface object.
- RDX/R8: native color vectors.
- R9: null rectangle for ordinary speech.
- Stack `+20`: localized UTF-16 pointer, the native stack buffer at `RBP+210`.
- Stack `+28`: sound lifetime from XMM6.
- Stack `+30`: native style zero.

Official-kit named-subtitle resolver `D5980` explicitly reports a missing
subtitle string, localizes the scenario string, and calls renderer `D5EE0`.
That renderer constructs colors/fade/rectangle and invokes the same host
subtitle interface with supplied UTF-16, remaining duration and style two.
Retail equivalents are resolver `6F62D0` and renderer `6F6750`.
Its final call is `6F68A1`, returning to `6F68A4`; R10 is loaded from the host
vtable `+2C8` at `6F6897`, then called indirectly. This is a three-byte call,
not the six-byte memory-call shape used by the speech source.

The source label does not establish current VR presentation mode. Classify
gameplay versus theatre using current verified title/cinematic/presentation
state, and reject unrelated return addresses. Copy text during the call;
never retain its native stack pointer.

## Halo 4 queue and shared sound/script consumers

Official-kit named-subtitle activation `23BA00` contains the missing-string
diagnostic and localizes through scenario `+674`. It stores two native queue
records through this title's TLS subtitle state. Renderer `23BBF0` consumes
those records, validates character identity where applicable, localizes into
bounded UTF-16 buffers, and emits up to two native host-interface `+2C8` calls.
Retail renderer `12FC38` matches that control flow, scenario field, two-record
queue, localizer calls, remaining-duration reads, host call ABI, and fallback
native font renderer. BSim suggested the candidate; the structural comparison
establishes the match.

| Retail return | Meaning | UTF-16 source | Remaining seconds | Native style |
| --- | --- | --- | --- | --- |
| `1301DA` | First of two queued lines | first localizer output | queue 0 `+8` | 0 |
| `130253` | Second queued line | second localizer output | queue state `+1C` | 0 |
| `1302A2` | Single line, second record absent | first localizer output | queue 0 `+8` | 2 |

The corresponding memory calls are at `1301D4`, `13024D`, `13029C`.
All use RCX self, RDX/R8 colors, R9 rectangle, and stack text/duration/style
at `+20/+28/+30` before call. A bounded overlay can preserve both line slots
and each remaining expiry. The second line must not replace the first merely
because it arrived later in the same native render.

Do not mark these callers theatre-only. H4EK's script registration explicitly
names `play_sound_subtitle` at `2B6D3B`, registering function `204710`, and
`sound_impulse_start_with_subtitle` at `2B68C7`, registering `210230`.
Both functions call `23B000`, whose native tail call supplies `R8D=39` to the
same `23BA00` queue. The queue therefore supports sound/scripted subtitles;
its source file name and single-line style do not prove current theatre mode.
The sound producers were not independently matched to retail before the user
requested discovery stop; the retail final renderer/queue match is complete.

## Verification and preserved output

`python tools/re/verify_h2_h4_subtitle_callers.py` passes five unique caller
patterns, exact indirect-call bytes, pinned module hashes, and unwind ownership.
It prints the exact pattern/call/return triplets for cold adapter validation.
Halo 2 speech has chained unwind fragments:
`6B8045 -> 6B7F1A -> 6B7ED0`. Requiring its containing unwind fragment to
start at the whole function would falsely reject a valid binding.

Official-kit and retail decompilations and candidate searches are preserved
under `out/coop-stability-20260923/{h2,h4}-subtitle-*`. Relevant files include
`h2-subtitle-kit-render.txt`, `h2-subtitle-kit-handoff.txt`,
`h2-subtitle-retail-gameplay.txt`, `h2-subtitle-retail-theatre.txt`,
`h4-subtitle-kit-draw.txt`, `h4-subtitle-retail-draw.txt`, and
`h4-subtitle-kit-sound.txt`.

## CE result and limits

The bounded CE kit search found subtitle tag-data names but did not establish
a final localized-text handoff. Apparent direct interface-slot `+164` sites
were native D3D target queries; they are not subtitle bindings. CE remains
unproven for this optional text observer. No guessed CE hook was added.
The user requested stopping further discovery and packaging the completed
integrations; unfinished CE subtitle discovery is preserved for explicit resume.

The runtime integration must still validate current title/generation, exact
caller, text bounds, finite duration, presentation mode and source lifetime.
Offline call proof does not demonstrate native localization coverage, headset
placement, suppression of duplicate stock captions or successful runtime hooks.
