; Halo 3 / ODST / Halo 4 direct drive (legacy_direct_drive): entry detours for each title's per-user
; control update -- the ACCUMULATOR of player_control.desired_angles, Reach's 0x6375C in each sibling
; engine (located by structure: TLS player_control, a per-user stride, desired += stick delta
; then clamps; see LegacyDrive in game.cpp for the RVAs and the anchors that guard them).
; Signature, as Reach's: ecx = user index, xmm1..xmm3 = floats, no stack arguments; the record is reached
; through the calling (sim) thread's own TLS, so the C callee re-derives it from the same thread.
; Each detour runs the native update unchanged, then ALWAYS calls
; LegacyPcApplyForUser(slot, user, ret, entryRsp), which decides (which caller / armed / fresh setpoint /
; validated record / on foot or a written seat) whether to overwrite desired_angles. `ret` is this detour's
; own return address; `entryRsp` points at the slot holding it, i.e. the accumulator's ENTRY rsp. Halo 3 and
; ODST call the accumulator directly from the stick update, the no-unit path and a weapon's pitch kick, so
; `ret` names the caller. Halo 4 calls it from ONE site inside a dispatcher that one control pass runs up to
; four times (impulse, sway, stick, no unit), so there `ret` names nothing and the C callee reads the
; DISPATCHER's return address at entryRsp + its proven frame (0x100; aim_caller_logic.h,
; legacy_direct_drive.inl). Only the stick call is the update.
; Naked so the native ABI (rcx + xmm1..xmm3; the natives return nothing meaningful -- each ends by storing
; its clamped pitch) is forwarded byte-exact. PROC FRAME + .allocstack give each an unwind entry, which an
; exception unwinding through it and the teardown's quiescence scan both need. The frame is unchanged by
; the third and fourth arguments: [rsp+38h] is the caller's return address, read after the native call
; returns, and lea r9,[rsp+38h] is its address -- the accumulator's entry rsp, because MinHook enters this
; detour by a jmp from the accumulator's first instruction.
option casemap:none

EXTERN g_halo3PcOrig:QWORD
EXTERN g_odstPcOrig:QWORD
EXTERN g_halo4PcOrig:QWORD
EXTERN LegacyPcApplyForUser:PROC          ; void LegacyPcApplyForUser(int slot /*ecx*/, int user /*edx*/,
                                          ;     uintptr_t ret /*r8*/, const uint8_t* entryRsp /*r9*/)

.code
Halo3PcHook PROC FRAME
    sub  rsp, 38h
    .allocstack 38h
    .endprolog
    mov  QWORD PTR [rsp+30h], rcx         ; user index survives the native call
    call QWORD PTR [g_halo3PcOrig]
    mov  QWORD PTR [rsp+28h], rax
    mov  rdx, QWORD PTR [rsp+30h]         ; user -> arg2
    mov  r8, QWORD PTR [rsp+38h]          ; our return address (the caller) -> arg3
    lea  r9, QWORD PTR [rsp+38h]          ; the accumulator's entry rsp (where that return sits) -> arg4
    xor  ecx, ecx                         ; slot 0 = Halo 3
    call LegacyPcApplyForUser
    mov  rax, QWORD PTR [rsp+28h]
    add  rsp, 38h
    ret
Halo3PcHook ENDP

OdstPcHook PROC FRAME
    sub  rsp, 38h
    .allocstack 38h
    .endprolog
    mov  QWORD PTR [rsp+30h], rcx
    call QWORD PTR [g_odstPcOrig]
    mov  QWORD PTR [rsp+28h], rax
    mov  rdx, QWORD PTR [rsp+30h]
    mov  r8, QWORD PTR [rsp+38h]
    lea  r9, QWORD PTR [rsp+38h]
    mov  ecx, 1                           ; slot 1 = ODST
    call LegacyPcApplyForUser
    mov  rax, QWORD PTR [rsp+28h]
    add  rsp, 38h
    ret
OdstPcHook ENDP

Halo4PcHook PROC FRAME
    sub  rsp, 38h
    .allocstack 38h
    .endprolog
    mov  QWORD PTR [rsp+30h], rcx
    call QWORD PTR [g_halo4PcOrig]
    mov  QWORD PTR [rsp+28h], rax
    mov  rdx, QWORD PTR [rsp+30h]
    mov  r8, QWORD PTR [rsp+38h]
    lea  r9, QWORD PTR [rsp+38h]          ; the dispatcher's return is read through this (entry rsp + 0x100)
    mov  ecx, 2                           ; slot 2 = Halo 4
    call LegacyPcApplyForUser
    mov  rax, QWORD PTR [rsp+28h]
    add  rsp, 38h
    ret
Halo4PcHook ENDP
END
