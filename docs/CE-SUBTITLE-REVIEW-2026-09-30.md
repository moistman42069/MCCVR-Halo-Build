# Halo CE subtitle handoff follow-up — 2026-09-30

This is a bounded read-only continuation of the September 23 CE subtitle
research. It does not add a hook or claim CE subtitle support. The current
native subtitle compositor can accept copied text events, but CE still needs a
title-specific producer with proven caller, payload, encoding and lifetime.

## What was checked

- Read `docs/NATIVE-SUBTITLE-H2-H4-2026-09-23.md` and the preserved CE
  subtitle Ghidra outputs under `out/coop-stability-20260923/`.
- Reopened the existing `CEKit` Ghidra project read-only, with analysis disabled,
  and checked references to kit strings `sound_subtitle_data` (`0x00B46884`)
  and `subtitle data` (`0x00B46A0C`). The project’s reference manager reports
  no XREF for either string. They establish tag/schema vocabulary, not a
  runtime text producer or render call.
- Queried the existing `CERetail` project read-only for `subtitle`, `subtitles`,
  `localized` and `failed to find string`. The only subtitle literal found is
  `GameOpt.Subtitles`; its references lead to the settings/config event path
  (`FUN_18047B180`, `FUN_18047ADE0`, `FUN_18006ACB0`). The other localization
  hit, `localized_projects`, is unrelated data. No literal failure diagnostic,
  localized caption payload or renderer handoff surfaced in that bounded
  string query.
- Followed the CE kit `+0x164` candidates already preserved in
  `ce-subtitle-kit-indirect.txt`. One is a `cseries_events.cpp` event-formatting
  path: its callback writes into a local `0x200` byte buffer, then the event
  logger formats that result. This buffer is a transient event-log value; the
  call graph does not identify subtitle ownership or a caption renderer. It is
  therefore not a safe shared text slot. The earlier review also rejected
  separate apparent slot candidates that resolve to rasterizer/render-target
  paths rather than subtitle consumers.
- Reviewed `root-ce-subtitle-manager.txt`. Its CE Retail functions register and
  forward `gsUserConfigChanged` and `gsCfgVarChanged` events. They provide
  configuration notification only; no localized caption pointer, text
  conversion, duration, expiry or draw operation crosses those functions.

## Result and blocker

No viable localized-text handoff was established. In particular, this pass did
not prove whether CE's final caption path receives narrow encoded text, UTF-16,
a string ID, or a tag-backed reference. The generic event buffer cannot answer
that, and retaining it would outlive its stack storage. No call site should be
derived from the `GameOpt.Subtitles` option or `sound_subtitle_data` name alone.

The next useful evidence is a source-backed CE subtitle consumer in the kit or
a runtime capture that identifies the final CE caption draw with its input
pointer, encoding and native expiry. Then retail matching can verify a unique
caller and an in-call copy point. Until those are available, CE gameplay
subtitle capture remains WIP and must not be described as implemented. The
theatre screen-band capture is a separate presentation path and this review
does not alter it.

No source code changed and no speculative hook was added. Existing native
subtitle queue and rendering work remains intact for the titles that have
verified producers; CE stays outside that producer list until evidence closes
this gap.
