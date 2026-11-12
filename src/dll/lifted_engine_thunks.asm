; Project Reclaimer 0.10's recompiled Halo 3: the bridges between its translated bodies and this mod's C++
; (lifted_engine.cpp; the convention is in src/common/lifted_engine_logic.h and
; docs/RECLAIMER-0.10-ENGINE-EVIDENCE.md).
;
; A translated body is entered with rdi = ctx (the guest register file), rsi = the guest stack pointer, and the
; guest's integer arguments in rdx/rcx/r8/r9 as that function's adapter loads them. It keeps rbx, rbp and r12-r15
; and may change every other register, xmm6-xmm15 included.
;
;   LiftedHookEntry   a hooked body's first instruction jumps here (r10 = the hook's record): the host argument
;                     registers are handed to LiftedDispatch, which runs the mod's detour.
;   LiftedReplacementEntry  the same for a call to the client's own version of a hooked function (ctx in rcx).
;   LiftedOrigEntry   the detour's "original" pointer (r10 = the record): an ordinary Windows x64 call whose
;                     arguments LiftedCallOriginal writes into ctx before it runs the body.
;   LiftedCallEntry   a game function the mod calls (r10 = the record): the same, through LiftedCallGame.
;   LiftedCallNative  runs a Windows x64 function with its arguments taken from ctx and the guest stack.
;   LiftedCallBody    runs a translated body from Windows x64 code, keeping every register Windows x64 keeps.
option casemap:none

EXTERN LiftedDispatch:PROC
EXTERN LiftedReplacementDispatch:PROC
EXTERN LiftedCallOriginal:PROC
EXTERN LiftedCallGame:PROC

.code

; Entered from translated code: entry rsp = 8 (mod 16). rdi = ctx, rsi = guest rsp, r10 = record.
; LiftedDispatch(record, ctx, guestRsp, hostArgs[4] = rdx, rcx, r8, r9, hostReturnSlot) returns the value for rax.
; hostReturnSlot is where this call's return address sits on the host stack: in the caller's translated code for a
; direct call, which stores no guest return address (lifted_engine_logic.h, GuestReturnOfCall), or in the client's
; own code when it called the body for the caller, with the caller's return address further up.
; entry rsp = 8; push rbp and 50h leave it 16-aligned: home +00h, fifth argument +20h, the block +28h..+47h, and the
; return address at +58h.
LiftedHookEntry PROC FRAME
    push rbp
    .pushreg rbp
    sub rsp, 50h
    .allocstack 50h
    .endprolog
    mov [rsp+28h], rdx
    mov [rsp+30h], rcx
    mov [rsp+38h], r8
    mov [rsp+40h], r9
    lea rax, [rsp+58h]
    mov [rsp+20h], rax
    mov rcx, r10
    mov rdx, rdi
    mov r8, rsi
    lea r9, [rsp+28h]
    call LiftedDispatch
    add rsp, 50h
    pop rbp
    ret
LiftedHookEntry ENDP

; Entered from translated code through the stub of a function the client replaced with a version of its own, while
; that function's hook is enabled: an ordinary Windows x64 call, rcx = ctx (the guest arguments are in it and its
; stack pointer is at the guest return address), r10 = record.
; LiftedReplacementDispatch(record, ctx, hostReturnSlot) returns rax (and leaves it in ctx).
; entry rsp = 8; 28h leaves it 16-aligned with home space, and the return address at +28h.
LiftedReplacementEntry PROC FRAME
    sub rsp, 28h
    .allocstack 28h
    .endprolog
    mov rdx, rcx
    mov rcx, r10
    lea r8, [rsp+28h]
    call LiftedReplacementDispatch
    add rsp, 28h
    ret
LiftedReplacementEntry ENDP

; Shared by LiftedOrigEntry and LiftedCallEntry: an ordinary Windows x64 call, r10 = record. The four integer
; and four vector argument registers go to a block (+20h rcx, rdx, r8, r9; +40h xmm0-xmm3), the caller's stack
; arguments start at entry rsp + 28h. The C++ side returns rax and leaves the xmm0 result in the block's xmm0.
; entry rsp = 8; push rbp and 80h leave it 16-aligned.
LIFTED_WINDOWS_ENTRY MACRO entryName, target
entryName PROC FRAME
    push rbp
    .pushreg rbp
    sub rsp, 80h
    .allocstack 80h
    .endprolog
    mov [rsp+20h], rcx
    mov [rsp+28h], rdx
    mov [rsp+30h], r8
    mov [rsp+38h], r9
    movdqu [rsp+40h], xmm0
    movdqu [rsp+50h], xmm1
    movdqu [rsp+60h], xmm2
    movdqu [rsp+70h], xmm3
    mov rcx, r10
    lea rdx, [rsp+20h]
    lea r8, [rsp+0B0h]
    call target
    movdqu xmm0, [rsp+40h]
    add rsp, 80h
    pop rbp
    ret
entryName ENDP
ENDM

LIFTED_WINDOWS_ENTRY LiftedOrigEntry, LiftedCallOriginal
LIFTED_WINDOWS_ENTRY LiftedCallEntry, LiftedCallGame

; uint64_t LiftedCallNative(void* function, uint8_t* ctx, const uint64_t* stackArgs)
; Loads rcx, rdx, r8, r9 from ctx (+08h, +10h, +40h, +48h) and xmm0-xmm3 from ctx (+90h ...), passes eight stack
; arguments from stackArgs, calls the function, stores its xmm0 to ctx+90h and returns its rax.
; entry rsp = 8; two pushes and 68h leave it 16-aligned with 20h of home space and 40h of stack arguments.
LiftedCallNative PROC FRAME
    push rbp
    .pushreg rbp
    push rbx
    .pushreg rbx
    sub rsp, 68h
    .allocstack 68h
    .endprolog
    mov rbx, rdx
    mov r10, rcx
    mov rax, [r8]
    mov [rsp+20h], rax
    mov rax, [r8+8]
    mov [rsp+28h], rax
    mov rax, [r8+10h]
    mov [rsp+30h], rax
    mov rax, [r8+18h]
    mov [rsp+38h], rax
    mov rax, [r8+20h]
    mov [rsp+40h], rax
    mov rax, [r8+28h]
    mov [rsp+48h], rax
    mov rax, [r8+30h]
    mov [rsp+50h], rax
    mov rax, [r8+38h]
    mov [rsp+58h], rax
    movdqu xmm0, [rbx+90h]
    movdqu xmm1, [rbx+0A0h]
    movdqu xmm2, [rbx+0B0h]
    movdqu xmm3, [rbx+0C0h]
    mov rcx, [rbx+8]
    mov rdx, [rbx+10h]
    mov r8, [rbx+40h]
    mov r9, [rbx+48h]
    call r10
    movdqu [rbx+90h], xmm0
    add rsp, 68h
    pop rbx
    pop rbp
    ret
LiftedCallNative ENDP

; uint64_t LiftedCallBody(void* body, uint8_t* ctx, uint64_t guestRsp, const uint64_t hostArgs[4])
; hostArgs: the values for rdx, rcx, r8, r9. Every register Windows x64 keeps is kept here.
; entry rsp = 8; eight pushes leave it 8; 0A8h makes it 16-aligned for the xmm saves at +00h..+9Fh.
LiftedCallBody PROC FRAME
    push rbp
    .pushreg rbp
    push rbx
    .pushreg rbx
    push rsi
    .pushreg rsi
    push rdi
    .pushreg rdi
    push r12
    .pushreg r12
    push r13
    .pushreg r13
    push r14
    .pushreg r14
    push r15
    .pushreg r15
    sub rsp, 0A8h
    .allocstack 0A8h
    movdqa [rsp+00h], xmm6
    .savexmm128 xmm6, 00h
    movdqa [rsp+10h], xmm7
    .savexmm128 xmm7, 10h
    movdqa [rsp+20h], xmm8
    .savexmm128 xmm8, 20h
    movdqa [rsp+30h], xmm9
    .savexmm128 xmm9, 30h
    movdqa [rsp+40h], xmm10
    .savexmm128 xmm10, 40h
    movdqa [rsp+50h], xmm11
    .savexmm128 xmm11, 50h
    movdqa [rsp+60h], xmm12
    .savexmm128 xmm12, 60h
    movdqa [rsp+70h], xmm13
    .savexmm128 xmm13, 70h
    movdqa [rsp+80h], xmm14
    .savexmm128 xmm14, 80h
    movdqa [rsp+90h], xmm15
    .savexmm128 xmm15, 90h
    .endprolog
    mov rax, rcx
    mov rdi, rdx
    mov rsi, r8
    mov r10, r9
    mov rdx, [r10]
    mov rcx, [r10+8]
    mov r8, [r10+10h]
    mov r9, [r10+18h]
    call rax
    movdqa xmm6, [rsp+00h]
    movdqa xmm7, [rsp+10h]
    movdqa xmm8, [rsp+20h]
    movdqa xmm9, [rsp+30h]
    movdqa xmm10, [rsp+40h]
    movdqa xmm11, [rsp+50h]
    movdqa xmm12, [rsp+60h]
    movdqa xmm13, [rsp+70h]
    movdqa xmm14, [rsp+80h]
    movdqa xmm15, [rsp+90h]
    add rsp, 0A8h
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    pop rbp
    ret
LiftedCallBody ENDP

END
