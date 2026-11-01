; Halo 2 direct drive (halo2_direct_drive.inl): entry detour for Halo 2's per-user control accumulator,
; halo2.dll+0x6C3DF0 -- desired_angles += the look deltas, then wrap and clamp (Halo 1's
; player_control_modify_desired_angles lineage). Signature: ecx = user index, xmm1 = yaw delta, xmm2 = pitch
; delta, no stack arguments, nothing reads the return value; the record is reached through a global pointer.
; It has two callers: the per-user control update (+0x6C0A46), the only one the drive writes after, and a
; per-shot recoil kick (+0x6C47A0 tail-jumps in from weapon fire), which stays stock. So the callee is told
; the return address this detour was entered with.
; Runs the native update unchanged, then ALWAYS calls Halo2PcApplyForUser(user, returnAddress), which decides
; (armed / on foot / the view's own heading taken / a fresh hand aim) whether to overwrite desired_angles.
; Naked so the native ABI (ecx + xmm1/xmm2; the native uses only its own home slots at entry+08h and
; entry+18h, inside the 20h this frame reserves) is forwarded byte-exact. PROC FRAME + .allocstack give it
; an unwind entry, which an exception unwinding through it and the teardown's quiescence scan both need.
option casemap:none

EXTERN g_halo2PcOrig:QWORD
EXTERN Halo2PcApplyForUser:PROC           ; void Halo2PcApplyForUser(int user /*ecx*/, uint64 ret /*rdx*/)

.code
Halo2PcHook PROC FRAME
    sub  rsp, 38h
    .allocstack 38h
    .endprolog
    mov  QWORD PTR [rsp+30h], rcx         ; user index survives the native call
    call QWORD PTR [g_halo2PcOrig]
    mov  QWORD PTR [rsp+28h], rax
    mov  ecx, DWORD PTR [rsp+30h]         ; user -> arg1
    mov  rdx, QWORD PTR [rsp+38h]         ; the return address this detour was entered with -> arg2
    call Halo2PcApplyForUser
    mov  rax, QWORD PTR [rsp+28h]
    add  rsp, 38h
    ret
Halo2PcHook ENDP
END
