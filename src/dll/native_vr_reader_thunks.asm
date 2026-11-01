; Native VR action bridge: entry thunks for the hooked native action READERS (Reach +0xD1070, Halo 3
; +0x187028, ODST +0x1B8420, Halo 4 +0x13F5CC; gesture_binding_reader.inl).
;
; Each reader is a LEAF, and LTCG let many of its native callers keep VOLATILE registers live across the
; call because the compiler knew the leaf leaves them alone (tools/audit_caller_area.py:
; Reach 28 of 114 callers, Halo 3 23 of 102, ODST 29 of 120, Halo 4 35 of 127). A C++ detour clobbers
; them. Halo 3's input step (+0xF8D3C) keeps its player-control bit array in r10/r11 across two reader
; calls and then writes through it: with a plain C++ detour it writes through r10 = 0 (+0xF8EBC / +0xF8EC9),
; crashing Halo 3 at every level load.
;
; So the hook is entered here: every volatile register except rax (the returned record) -- rcx, rdx,
; r8-r11, xmm0-xmm5 -- is saved, the C++ hook runs through its pointer (native_vr_actions.cpp), and all of
; them are restored. Flags are not preserved (no caller keeps flags across a call). PROC FRAME gives each
; thunk an unwind entry for exceptions unwinding through it and for the retirement quiescence scan.
;
; Frame: entry rsp = 8 (mod 16); six pushes and 88h leave rsp 16-aligned for the movdqa saves at +20h..+70h
; and for the call, with the callee's home area at +00h..+1Fh.
option casemap:none

EXTERN g_nativeVrReaderReach:QWORD
EXTERN g_nativeVrReaderHalo3:QWORD
EXTERN g_nativeVrH3ActionEdges:QWORD
EXTERN g_nativeVrReaderOdst:QWORD
EXTERN g_nativeVrReaderHalo4:QWORD

PRESERVING_READER MACRO thunkName, hookPointer
thunkName PROC FRAME
    push rcx
    .pushreg rcx
    push rdx
    .pushreg rdx
    push r8
    .pushreg r8
    push r9
    .pushreg r9
    push r10
    .pushreg r10
    push r11
    .pushreg r11
    sub rsp, 88h
    .allocstack 88h
    movdqa XMMWORD PTR [rsp+20h], xmm0
    .savexmm128 xmm0, 20h
    movdqa XMMWORD PTR [rsp+30h], xmm1
    .savexmm128 xmm1, 30h
    movdqa XMMWORD PTR [rsp+40h], xmm2
    .savexmm128 xmm2, 40h
    movdqa XMMWORD PTR [rsp+50h], xmm3
    .savexmm128 xmm3, 50h
    movdqa XMMWORD PTR [rsp+60h], xmm4
    .savexmm128 xmm4, 60h
    movdqa XMMWORD PTR [rsp+70h], xmm5
    .savexmm128 xmm5, 70h
    .endprolog
    ; rcx (state) and edx (action) still hold the native arguments.
    ; Six saved registers (30h) plus locals (88h): original native return address.
    ; Pass it explicitly; the C++ hook's own return address is inside this thunk.
    mov r8, QWORD PTR [rsp+0B8h]
    call QWORD PTR [hookPointer]
    movdqa xmm0, XMMWORD PTR [rsp+20h]
    movdqa xmm1, XMMWORD PTR [rsp+30h]
    movdqa xmm2, XMMWORD PTR [rsp+40h]
    movdqa xmm3, XMMWORD PTR [rsp+50h]
    movdqa xmm4, XMMWORD PTR [rsp+60h]
    movdqa xmm5, XMMWORD PTR [rsp+70h]
    add rsp, 88h
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    ret
thunkName ENDP
ENDM

.code
PRESERVING_READER NativeVrReaderThunkReach, g_nativeVrReaderReach
PRESERVING_READER NativeVrReaderThunkHalo3, g_nativeVrReaderHalo3
PRESERVING_READER NativeVrH3ActionEdgesThunk, g_nativeVrH3ActionEdges
PRESERVING_READER NativeVrReaderThunkOdst, g_nativeVrReaderOdst
PRESERVING_READER NativeVrReaderThunkHalo4, g_nativeVrReaderHalo4
END
