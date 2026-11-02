# Selected-record HUD candidate (supersedes alternate-anchor experiment)

Two user logs from source0497eea9, Steam: vanilla accepts three stock records
and logs slider writes; Ultimate Firefight finds only one stock-shaped record
with safe frames0/0, rejects it, accepts zero, and retries. Height/hiding use
separate hooks. The prior alternate-anchor experiment is rejected and remains
dormant: successful writes to alternate records did not prove consumption.

Read-only reinspection of the supplied user.map confirms the three records
listed below, including the 1280x720 record with zero scales. Retail halo3.dll
SHA256 b209d8454b12dc77e54ccd2c9924ec8d44b8619d21cf98e36ffaf601e67efb63:
- +2ECF38 selects a skin via +2D2D2C, walks flagged 0x64-byte resolution records,
  and returns the chosen record (fallback index0 when no flag matches).
- +2ED1E5 calls that selector with local user/resolution class. Frame setup
  publishes its result to +AD3158 and consumes that same pointer in RBX.
- +2ED894/+2ED8A8 load +28/+2C safe frames. Either equals0 (constant+840100)
  branches to+2EDA16, skipping the safe-frame calculation. Zero is legal native
  authored data, not proof of a corrupt tag; the old locator rejects it anyway.
- +2EC6F8 reads the published record's +04 FOV, +08 aspect and +0C destination
  depth. Frame setup calls this projection builder at+2EDA3F.
- Record +10/+14 are canvas dimensions; +18..+24 sensor fields. These match the
  provided map. Selector uses only its two register arguments; disassembly
  shows ordinary home-slot saves/restores, no hidden caller-area inputs.

New optional selector hook verifies unique entry/tail/caller/consumer patterns,
the relative call destination and relative site bounds. Only the proven frame
setup caller receives an adjusted copy; other callers receive the native pointer.
No map writes or widened memory scans. Fixed per-user/resolution copies live
inside our DLL image, excluded from the old scanner, and survive the HUD pass.
Native frame setup already uses a shared published pointer. Each call recopies
native bytes, avoiding accumulated curvature and retained pointers across maps.
Only +0C/+28/+2C change, using the existing FOV-corrected size/width formula and
curvature delta. Finite/range checks leave unsupported records native.

Stock-shaped records with valid scales stay exclusively on the accepted stock
scanner; zero-scale or differently shaped selected records use the new path.
This prevents double scaling and keeps stock behavior. It is broader than a
map-name or resolution whitelist, but not a promise for all future custom HUDs.
The stock scan can still log rejected zero records in mods; the new diagnostic
H3 live HUD layout adjusted/native counters identify the separate live path.
Hook misses leave this feature native, not VR disabled. Core callback draining
includes the new hook/trampoline. No logging/allocation/COM/locks in its callback.

Validation: read-only verifier matches all four signatures uniquely in supplied
retail and checks caller/stride. Portable transform tests pass: zero-scale,
stock isolation, alternative canvases, exact unchanged bytes, no accumulation,
restored curvature, malformed/nonfinite inputs. Windows build/SEH and headset
behavior not yet tested. Commit-only delivery; no build trigger. Test vanilla
and Ultimate Firefight size, width and curvature, then switch maps in-session.
Do not advance the accepted release pointer before headset confirmation.

---

Historical rejected candidate follows:

# Ultimate Firefight HUD layout candidate

User log source 673c120a: Halo3 scan repeatedly sees a 1280x720 anchor with
safe-frame pair 0/0, rejects it, then reports zero accepted blocks. Size,
aspect and curvature share this locator; hide and height use other paths.

Read-only HTTP ranges from supplied firefight_shrine.map: v13 header,
metadata file offset 2087653376, length123863040, virtual base7660765184.
chgd tag index20810 has three skins. Metadata offsets (relative to section):
- 0x6de21b8: canvas1280/720, sensor55/661/58/4, safe frames0/0.
- 0x6de221c: canvas1600/580, sensor55/520/58/4, safe frames.87/.87.
- 0x6de2280: canvas960/580, sensor55/520/58/4, safe frames.87/.87.
All three have destination Z -.95 at anchor-4. Other canvas records in this
block have zero safe frames and remain rejected. These records are 0x64 apart.

Candidate adds two exact 24-byte anchors using existing alternate-resolution
support, accepts 2..3 validated blocks, retains private-RW scan, finite/payload
checks, generation ownership and per-write identity checks. Stock prefix and
field offsets unchanged. No broad wildcard matching. This supports these
layouts in other mods, not arbitrary future CHUD definitions. Runtime selection
and visible response require headset confirmation; map evidence alone does
not prove the candidate fixes rendering. Universal tag-owned discovery remains
future work requiring stronger native ownership evidence.
