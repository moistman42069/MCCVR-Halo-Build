; CE Anniversary first_person_saber_bone_convert (retail halo1+0x77E70): the register-preserving entry
; MinHook jumps to, in front of the C++ hook (HaloCeSkinConvertEnter in haloce_first_person.cpp).
; The native writes only eax, xmm0-xmm5 and the flags (xmm6-xmm11 it saves), and halo1.dll is an LTCG
; build, so its callers may keep rcx, rdx and r8-r11 live across the call: 0xABCCA3 holds a global in r10
; and r10+0x138 in r11, passes r11 to the next call and stores through r10 afterwards
; (tools/audit_caller_area.py). A compiled detour may use any of those, so this thunk saves
; all six around it. It also passes the NATIVE return address, which the hook compares with the skin
; conversion's call site: _ReturnAddress() in the C++ function would see this thunk.
; PROC FRAME + .allocstack give it the .pdata entry that exception unwinding and the teardown's native
; stack verifier (WaitForNativeDetourQuiescence) both need.
option casemap:none

EXTERN HaloCeSkinConvertEnter:PROC   ; void (const NodeMatrix* /*rcx*/, SaberBoneMatrix* /*rdx*/, uintptr_t caller /*r8*/)

.code
HaloCeSkinConvertThunk PROC FRAME
    sub  rsp, 58h                         ; entry rsp%16==8 -> 0; 20h shadow, six saves, 8 spare
    .allocstack 58h
    .endprolog
    mov  QWORD PTR [rsp+20h], rcx
    mov  QWORD PTR [rsp+28h], rdx
    mov  QWORD PTR [rsp+30h], r8
    mov  QWORD PTR [rsp+38h], r9
    mov  QWORD PTR [rsp+40h], r10
    mov  QWORD PTR [rsp+48h], r11
    mov  r8, QWORD PTR [rsp+58h]          ; the native return address
    call HaloCeSkinConvertEnter           ; rcx = source, rdx = destination, untouched
    mov  rcx, QWORD PTR [rsp+20h]
    mov  rdx, QWORD PTR [rsp+28h]
    mov  r8,  QWORD PTR [rsp+30h]
    mov  r9,  QWORD PTR [rsp+38h]
    mov  r10, QWORD PTR [rsp+40h]
    mov  r11, QWORD PTR [rsp+48h]
    add  rsp, 58h
    ret
HaloCeSkinConvertThunk ENDP
END
