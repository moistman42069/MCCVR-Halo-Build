; Reach direct-drive: entry detour for the per-user control update (retail haloreach+0x6375C).
; The dev value-scan + write watchpoint (reach_dd_watchpoint=3) locates it and the dev one-shot
; write (reach_direct_drive=4) proves it: it is the ACCUMULATOR of player_control.desired_angles
; (desired += stick delta, then clamps). Signature (from its caller 0x6350C): ecx = user index,
; xmm1..xmm3 = floats, no stack arguments. It reaches player_control through its own TLS
; (*(TLS + 0xD8) + user*0x180), so the pointer is NOT an argument: the detour saves the user index
; and the C callee re-derives the pointer from this (sim) thread's TLS.
; This detour runs native unchanged, then ALWAYS calls ReachPcApplyForUser(user, entryRsp), which
; decides (which caller / armed / fresh aim / validated pointer) whether to overwrite desired_angles with
; the VR aim, and logs its state. `entryRsp` = lea [rsp+38h], the slot holding this detour's return
; address: the accumulator's ENTRY rsp (MinHook enters by a jmp). The accumulator has one call site, in
; the dispatcher 0x6350C, which one control pass calls up to three times (an effect camera impulse, the
; stick update or the no-unit path); the C callee names the caller by the DISPATCHER's return address,
; read at entryRsp + its proven frame (0x100; aim_caller_logic.h). Only the stick call is the update.
; Naked so the native ABI (rcx + xmm1..xmm3, integer return in rax) is forwarded byte-exact. No stack
; arguments exist, so shifting rsp here cannot misplace any.
; No allocation, logging, locks, file I/O or scans here (those live in the C callee).
; PROC FRAME + .allocstack give it a .pdata/unwind entry. Two things depend on that: an exception
; unwinding through this frame (it moves rsp, so without unwind info the unwinder reads a garbage
; return address), and the Reach teardown's ingress scan, which finds this detour's code range with
; RtlLookupFunctionEntry before it may remove the hook (ScanForReachDetourIngress in game.cpp).
option casemap:none

EXTERN g_reachPcOrig:QWORD                ; MinHook trampoline to the native control update
EXTERN ReachPcApplyForUser:PROC           ; void ReachPcApplyForUser(int user /*ecx*/, const uint8_t* entryRsp /*rdx*/)

.code
ReachPlayerControlHook PROC FRAME
    sub  rsp, 38h                         ; 20h shadow + saves + align: entry rsp%16==8 -> here %16==0
    .allocstack 38h
    .endprolog
    mov  QWORD PTR [rsp+30h], rcx         ; save the user index across the native call (rcx is volatile)
    call QWORD PTR [g_reachPcOrig]        ; native runs (rcx/xmm1..xmm3 still original); ret in rax
    mov  QWORD PTR [rsp+28h], rax         ; preserve native's return value across the C call
    mov  rcx, QWORD PTR [rsp+30h]         ; user index -> arg1
    lea  rdx, QWORD PTR [rsp+38h]         ; the accumulator's entry rsp (our return address's slot) -> arg2
    call ReachPcApplyForUser              ; always: it gates + logs internally (SEH inside)
    mov  rax, QWORD PTR [rsp+28h]         ; restore native's return value
    add  rsp, 38h
    ret
ReachPlayerControlHook ENDP
END
