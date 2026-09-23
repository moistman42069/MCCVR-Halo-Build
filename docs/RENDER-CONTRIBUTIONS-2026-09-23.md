# Rendering contributions: implementation evidence and acceptance limits

This is an in-progress implementation record, not headset acceptance. The
accepted-build pointer in CURRENT-STATE.md is unchanged. No MCC installation,
game launch, publication, or contributor executable execution occurred.

## Optional DLSS (GP12)

Pancreations' preserved source at baseline 3f86346 supplies the NGX/NVOF starting
point. Martysl1's separate Reach delta refers to a missing depth helper; the
replacement admits only the current title's verified alternate depth resource.
The new helper has a 1,024-case admission fixture. Source archives and original
hashes remain under out/community-audit-20260923.

The integration is optional and default off. It uses per-eye camera reprojection
motion and depth, not frame generation. NGX availability precedes reduced-size
rendering. Missing native receipts retain ordinary resolve for that frame;
repeated admitted-size failures disable the optional path and restore full-size
rendering. Resize waits do not count as feature failures. Optional failures do
not disarm any title camera, stop OpenXR, or gate camera installation.

H3/ODST/Reach have their own camera publication and optional projection jitter.
H2 and H4 retain unjittered native projection. CE now has distinct Original and
Anniversary depth/camera adapters; ordinary fallback alone is no longer its
implementation. All title paths still require headset image-quality, timing,
title-switch, recenter, and long-session acceptance.

### CE native evidence

Pinned halo1.dll SHA-256:
0A12DC561780F449D3F4D0DF10BB8D3BC7BE7840A5BEB2B236F672EB6CD42E6C.

Anniversary reads the actual shader constants published by native camera upload:
backend +D8, block +8, shader data +240 (world origin) and +270 (transposed,
rotation-only view-projection). Optional independent contracts verify native
2351E0/235860, the constant-address witnesses, virtual +110 dispatch, and vtable
pointers 18153C0/1815488. This contract failure affects DLSS only. The production
decoder removes the actual Saber camera rotation and converts its translation
and projection depth term to CE units. It rejects mismatched origin and oblique
projection. Actual native camera and constant-writer instructions were executed
in bounded Unicorn emulation, never loaded into Windows: six translated/rotated
cases, nine points per case, 21 decoder checks per case, 12,632 instructions.

Original freezes the admitted raster frustum's valid byte +140 and projection
matrix +144 before BBCF30. Official HCEEK projection builder 427C90 matches
retail B8F690: ordinary perspective uses reversed depth, P10=n/(f-n), P11=-1,
P14=fn/(f-n). Oblique near-zero projection stays ordinary. The camera/fog far
plane is not substituted for the actual raster projection.

BBCF30 -> BBD172 -> AE06FC copies that raster camera/frustum and selects depth
through B51670/AEB00. The normal kind-1 root is (*2E3BDD8)+318, but the adapter
does not assume it is bound. At the existing native HUD entry (call B32106,
return B3210B) within the exact primary-eye scope, the existing native target
receipt must independently match that root, current writable DSV, selected
surface, context, descriptor, generation, frame and raster dimensions. The
snapshot occurs before HUD mutation. Postprocess can invalidate this receipt;
that frame then retains ordinary resolve.

The separate depth cache has four resources, two rendering and two completed,
under the existing bounded color-cache lease. Native callbacks only issue
whole-subresource GPU copies from verified live resources: no COM ownership,
allocation, locks, logging or scanning. Completed color may survive missing
depth; partial/stale depth cannot accompany a new color pair. The compositor
borrows SRVs under the same lease and resets temporal history for old/repeated
serials and changed generation, reference or tracking space. Classic's verified
full-raster depth is sampled at the corresponding scaled pixel when final color
has been reduced by the native final quad.

Validation: Release DLL; CE camera/depth fixtures; existing CE runtime, Classic
runtime, eye-cache and HUD-layout fixtures; DLSS contract fixture. The depth
fixture uses WARP, captures two distinct native depth values, overwrites the
source, reads both completed copies, and checks stale keys, leases and optional
MSAA failure. Native proof scripts are tools/re/verify_ce_dlss_native_receipts.py
and tools/re/verify_ce_dlss_camera.py.

### Runtime distribution for the private candidate

SDK pin: a291cc7d2cc642a51566f3dfd5376f635cd1b284.
Runtime: lib/Windows_x86_64/rel/nvngx_dlss.dll, version 310.7.0.0,
58,977,904 bytes, valid NVIDIA signature, SHA-256
BE6E434A94CA32499515EB62CA0E6C274526055D568D0426E4C652DCDFB6EE6E.
cmake/Dlss.cmake verifies this hash before staging it beside the mod. The full
SDK license and exact-pin notice are retained under third_party/dlss and staged
under licenses/NVIDIA-DLSS. Parent/user authorization is for a private test
candidate. Public distribution obligations, including the supplement's
notification/branding conditions, have not been represented as fulfilled.

## Native localized subtitles (GP07; five titles implemented)

Caption ingress copies a bounded UTF-16 string during the native invocation.
The shared host observer is a normal seven-argument ABI hook with compiler
unwind metadata. It forwards all native arguments and the Boolean result once;
native exceptions propagate. The observer publishes only after native acceptance
and current title/generation/caller/theatre arbitration. Copies occur before
the native callee can consume the incoming text. Rasterization and GDI work
occur on the worker. Native camera installation is independent.

MCC host SHA-256:
BE70D6DCD1A884F10CEB342A7A2DCB35EE0FA43181B66A1D19C3D830E9834691.
Unique host wrapper 1EBC10 calls production 3736B4 via 1EBC64; production calls
the final UTF-16 handoff BAC674 via 373809. The independent timed caller reaches
that same handoff through 373258. Wrapper vtable pointer is at 327F688.

Reach HREK SHA-256:
CBDD8448A87A433B0DFFC0DE47D06DB7A18B4BF868B96B057135DAA86790ABA8.
Named subtitle activation 2037D0/203920 resolves localized text, with TLS558
subtitle id/total/remaining/type and scenario localized-list +63C. Renderer
204470..204CD8 processes authored line breaks and emits the completed text once
through host +2C8 at 204C2E; it is not a per-line interface call. Current Reach
gameplay final return is 711363. Theatre renderer D5710 resolves through 9DF68
and returns from the final interface at D5D81. Five independent unique
signatures and the resolver call relationship establish source authority only
after the existing Reach load/preflight/camera gate. No Reach title image hook
or retained module reference is needed. Earlier experimental relay code remains
disabled, in accordance with the repository's preserve-dormant-code rule.

H3 theatre: official-kit renderer 4EDB80..4EE151 and native host +2C8 call
4EE0B0 use final UTF-16 plus remaining duration. Current retail homolog
18ACE8..18B24D calls at 18B1A6, returns 18B1AC. ODST own-kit renderer
545D80..5464A1 resolves from its scenario list +638 and remaining timer +28;
its retail homolog 1BC094..1BC69C returns from host +2C8 at 1BC5F7. These
sources are integrated alongside independently verified H2/H4 feeds.
H3 speech callers return at 5995EA/5BB097; ODST speech returns at 5E2BCB.
Each is located by a unique native signature in its own module, independent
of similar code in another title. H3 kit SHA-256 is
59A78F2C96034D7CEB5D710505B2B36813AA141FC81A083E3F952973DBCE4602;
ODST kit SHA-256 is
354EC94158AECCE3E9D0F6463023AD5FA6D2AFE49B390E6067EBC17465C63C2D.
Pinned retail H3/ODST SHA-256 values are respectively
B209D8454B12DC77E54CCD2C9924EC8D44B8619D21CF98E36FFAF601E67EFB63 and
5BB20976EFDFD9E1CE59C589339804725FEC239021027C8D65B2733EAB94829A.

H2/H4 own-kit derivation, exact caller signatures and native queue lifetime
proofs are in NATIVE-SUBTITLE-H2-H4-2026-09-23.md. Its read-only verifier passes
all five callers, pinned hashes, instructions and unwind ownership, including
H2's chained unwind records. H4's two simultaneous queue lines are composed in
a fixed two-line worker buffer with independent expiry; its single-line branch
replaces both. Every verified caption uses the actual VR theatre state, with
a second check after native acceptance. A script's style value or source caller
does not itself establish gameplay versus theatre. Renderer-fed captions renew
with their native remaining lifetime, bounded to 250 ms after the last verified
emission so skipped or cleared queues do not leave a long-lived stale caption.
One-shot speech retains the actual bounded native duration.

The previous broad final-handoff observer remains dormant; the exact native
interface caller table is active. The table is worker-published with atomic
revision/identity validation, and hooks never dereference title memory. Tests
cover all five source owners, both presentation modes, two queue channels,
same-call retirement/presentation changes, native refusal/exception, input
consumption, independent line expiry and two maximum-size UTF-16 payloads.

CE's localized subtitle handoff remains unproven and is fully withheld. The
latest user override asks for the current candidate after validation, so further
CE source discovery is deferred. Do not describe this as six-title subtitles.

The compositor uses an optional transparent caption quad. Successful native
theatre preparation suppresses the existing captured subtitle band for that
frame; failed preparation preserves the band. Existing theatre and camera
presentation remain independent. The UTF-16 normalizer supports authored |n,
validated surrogate pairs and the verified E900 speaker delimiter, and rejects
unknown markup/control data. Placement controls share native caption pixels;
their visible result still requires headset testing.

## Independent dual crosshairs (GP10)

H2 Original/Anniversary and H3 use two single-symbol quads during native dual
weapon presentation. Reusing the captured two-symbol native artwork would yield
four symbols, so this is a deliberate shared procedural presentation. Single
weapon presentation restores the existing authored reticle. Primary and
secondary poses follow their physical handedness roles; their smoothing and
lifecycle resets are independent. The same HUD/theatre/zoom ownership rules
apply to both.

When barrel aiming is enabled, committed native muzzle publication supplies
the actual per-weapon ray transformed through that title's established
world-to-tracking frame. Receipts include full unit/weapon identity, slot,
barrel, generation, tracking space, serial, timestamp and handedness. Only the
same prepared-frame receipt is consumed. No game-memory reads are added to the
compositor. Missing receipts use the existing calibrated-controller aim path.
Production math/ownership fixtures pass; actual dual-wield tracking, handedness
and visual placement still require headset acceptance.

## Other evidence and preserved limitations

- GP06 bloom adaptation is owned separately by the installer/render agent.
  The superseded donor producer hash is not proof of the final bloom consumer.
- GP13 sampler-bias behavior remains disabled: the previous enabled experiment
  was rejected. The new cold sampler cache and WARP fixture are dormant support,
  not an active performance fix. Do not re-enable it by implication.
- GP14 live resize restores the committed plan when UI admission fails; the
  native window notification occurs once after fitted dimensions, with the
  temporary size lie restored. These changes do not prove direction-dependent
  map performance fixed.
- ST08 Rainred's Reach allocation failure now reports the exact allocation
  stage, HRESULT, source descriptor and device-removal status from the worker,
  rate-limited to five seconds. The original log lacked this evidence; no
  unsupported root-cause conclusion is claimed.
- GP01-05, GP09, GP11 and GP16 retain their individual backlog scope. Neither
  general DLSS support nor passing offline tests proves those visual reports
  resolved. Existing resolved GP15 cases remain regression obligations.
