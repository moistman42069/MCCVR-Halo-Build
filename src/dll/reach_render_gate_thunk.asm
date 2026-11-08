; Reach first-person render gate (retail haloreach+0x6527C): the register-preserving entry MinHook jumps
; to, in front of the C++ hook (ReachFirstPersonRenderGateEnter in game.cpp).
; The native is a LEAF that writes only rax, rcx, rdx, r8 and r9, and haloreach.dll is an LTCG build, so
; its callers keep other volatile registers live across the call: 0x2B349F holds the TLS block in r10 and
; a float in xmm1 and uses both afterwards; 0x2C3B1D holds the user index in r10 and indexes a per-player
; array with it (tools/audit_caller_area.py). A compiled detour may use any volatile register,
; so this thunk saves r10, r11 and xmm0-xmm5 around it. It also passes the NATIVE return address, which
; the hook compares with the render call site: _ReturnAddress() in the C++ function would see this thunk.
; PROC FRAME + .pushreg/.allocstack give it the .pdata entry that exception unwinding and the Reach
; teardown's ingress scan (ResolveReachDetourCodeRange) both need.
option casemap:none

EXTERN ReachFirstPersonRenderGateEnter:PROC   ; int32_t (int32_t outputUserIndex /*ecx*/, uintptr_t caller /*rdx*/)

.code
ReachFirstPersonRenderGateThunk PROC FRAME
    push r10
    .pushreg r10
    push r11
    .pushreg r11
    sub  rsp, 88h                         ; entry rsp%16==8, two pushes keep it 8, 88h makes it 0
    .allocstack 88h
    .endprolog
    movdqa XMMWORD PTR [rsp+20h], xmm0    ; 20h shadow space below the saves
    movdqa XMMWORD PTR [rsp+30h], xmm1
    movdqa XMMWORD PTR [rsp+40h], xmm2
    movdqa XMMWORD PTR [rsp+50h], xmm3
    movdqa XMMWORD PTR [rsp+60h], xmm4
    movdqa XMMWORD PTR [rsp+70h], xmm5
    mov  rdx, QWORD PTR [rsp+98h]         ; the native return address (88h + the two pushes)
    call ReachFirstPersonRenderGateEnter  ; ecx = outputUserIndex, untouched; result in eax
    movdqa xmm0, XMMWORD PTR [rsp+20h]
    movdqa xmm1, XMMWORD PTR [rsp+30h]
    movdqa xmm2, XMMWORD PTR [rsp+40h]
    movdqa xmm3, XMMWORD PTR [rsp+50h]
    movdqa xmm4, XMMWORD PTR [rsp+60h]
    movdqa xmm5, XMMWORD PTR [rsp+70h]
    add  rsp, 88h
    pop  r11
    pop  r10
    ret
ReachFirstPersonRenderGateThunk ENDP
END
