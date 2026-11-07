# Project Reclaimer 0.10 engine evidence

Measured 2026-11-06 on the dev box, statically, from files the client wrote. Nothing here was read from a
running client yet: every runtime claim below is marked **unverified**.

| File | SHA-256 |
| --- | --- |
| `project-reclaimer-client-v0.9.23.exe` (version resource 0.10.0; updated itself in place) | `8246e8a4a6e90dcd2305ca402a5de3f6b2ab295fc86e3987ac100d8dcd38312e` |
| `%LOCALAPPDATA%\ProjectReclaimer\Cache\native\retail-engine\engine.code` | `2461fde9255041b198545a6160bce72f27855bd152f11f2bee9f48052ab6f36f` |
| MCC Steam `halo3\halo3.dll` (the build the client says it lifted: `b209d8454b12dc77`) | `b209d8454b12dc77e54ccd2c9924ec8d44b8619d21cf98e36ffaf601e67efb63` |

Pinned copies: `N:\MyP4_All\UE\TheScienceTeam\_reclaimer-0.10-re\pinned`.

## What changed from 0.9

0.9.x ran MCC's `halo3.dll` with `LoadLibrary` in a separate engine worker
(`probe-engine-worker ... --client-window`). 0.10.0 does neither:

- `client.log` (now `%LOCALAPPDATA%\ProjectReclaimer\Logs\client.log`): on first start
  `[host] the engine's code is 25% made` ... `100% made`, then `[host] linked recompiled engine, image at
  0x7ff692625000` and `[host] engine thread started`, in the process the player started. No worker.
- The 0.10.0 executable has no `probe-engine-worker` or `--client-window` strings (0.9.8 has both). It has
  `Halo 3 recompiled x86-64`, `unlifted instruction at`, `the guest DllMain failed`, `guest threads running`,
  and `the retail DLL runs only in a development build`.

So `halo3.dll`'s own machine code never runs, and `halo3.dll` is not a loaded module.

## `engine.code` (format version 2)

```
0x00  "RCLMCODE"
0x08  u32 2                format version
0x0C  u32 0x20             length of the next field
0x10  32 bytes             a hash (not the SHA-256 of halo3.dll)
0x30  u64 0x018CB8D7       length of the body that follows (file length - 0x40)
0x38  8 bytes              unknown (checksum?)
0x40  body
```

Body (offsets from 0x40):

```
+0x00  u64 0x180000000     the guest image's preferred base (halo3.dll's ImageBase)
+0x08  u32 0x20, u32 0x1B7307, u32 0     unknown
+0x14  LEB128 count = 22894, LEB128 first guest RVA = 0x1000 (.text), LEB128 0
       then `count` records of three LEB128 values:
         body_offset   offset of the function's body inside its host block (288 or 256)
         guest_delta   distance to the next record's guest RVA
         host_size     size of the host block
       guest RVAs and host offsets both accumulate from the first record.
+0x1F243  a second LEB128 stream (not decoded; relocations or labels?)
          then length-prefixed names (`first_person_weapon`, `interpolation_record`, ...)
+0xFB4C8  0x00, then LEB128 code length = 0x17D040A
+0xFB4CD  the code: host blocks back to back, unrelocated (call rel32 0, movabs 0)
```

Checked: every function start in `halo3.dll`'s `.pdata` (21768) is a record start, or lies inside a record
whose guest range covers that function's MSVC-split fragments (3703 such fragments, e.g. 0x2766 inside the
record at 0x26C0). The host sizes sum to the code length less 0xBF of trailing thunks. Records end at RVA
0x6F9110, the last function of `.text`.

## A host block

Each block is an **adapter**, then the **body** at `body_offset`.

The adapter is a Windows-x64 function of one argument: a pointer to the guest register file (`ctx`). It
saves rsi/rdi/r12/r13/xmm6-15, loads the body's register arguments from `ctx`, calls the body with
`rdi = ctx` and `rsi = ctx+0x20` (the guest rsp, which points at the return-address slot), stores `rax` to
`ctx+0x00` when the function returns a value, and sets `ctx+0x20 = guest rsp + 8`.

The body's convention:

- `rdi` = `ctx`, `rsi` = guest stack pointer (a separate stack, in guest memory).
- Integer register arguments arrive in host registers, a per-function subset chosen by the lifter (only
  what the body reads). The adapter's `mov reg, [ctx + off]` loads give the mapping. Guest rcx is
  `ctx+0x08`; guest rdx is `ctx+0x10`. Seen: guest rcx in host rdx, guest rdx in host rcx.
- **Floating-point arguments stay in `ctx`**: guest xmm0 is `ctx+0x90`, 16 bytes per register (RVA 0x1280,
  `movss [rcx+0xDC], xmm1`, becomes `vmovdqu xmm3, [rdi+0xA0]; vpextrd [rdx+0xDC], xmm3, 0`).
- Guest data is addressed at its original RVA from the guest image base, which a relocated `movabs` supplies
  (`lea r10, [rbx + 0x6FC690]` for `.rdata` RVA 0x6FC690). Data layout is halo3.dll's.
- `gs:[0x58]` (the TLS array) becomes a call to a host helper with `rcx = ctx` (RVA 0xF7DF0, the control
  update: `movabs r8, <helper>; mov rcx, rdi; call r8; mov rax, [rax + index*8]`).
- Calls between lifted functions are direct `call rel32` (unrelocated in the file).

## Unverified until a running client is read

- Where the code and the guest image sit in memory, and whether the guest image holds `.text`'s bytes.
- Whether relocated direct calls target bodies (expected) and how indirect calls and vtables dispatch.
- `ctx`'s full layout (guest gs base, rip, flags) and the TLS helper's address.
- Which process runs the engine when the launcher starts the client (expected: the child the first process
  starts).
- What Reclaimer itself patches in guest data and code (its log lists capacity changes, e.g. players
  0x10 -> 0x80).
