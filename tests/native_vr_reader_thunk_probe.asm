; Test-only probes for native_vr_reader_thunks.asm (tests/native_vr_reader_thunk_tests.cpp).
;
; ReaderThunkProbe(thunk, state, action) plays an LTCG caller: it loads a sentinel into every volatile
; register the thunk must preserve (r8-r11, xmm0-xmm5; rcx/rdx carry the arguments), calls the thunk, and
; returns a mask of the registers that did NOT survive: bit 0 rcx, 1 rdx, 2-5 r8-r11, 8-13 xmm0-xmm5.
; The thunk's return value is stored in g_probeReturn.
;
; ClobberingReader(state, action) is the worst-case hook: it records its arguments, then overwrites every
; volatile register and returns 0x1234.
option casemap:none

PUBLIC g_probeReturn
PUBLIC g_clobberState
PUBLIC g_clobberAction
PUBLIC g_clobberCaller
PUBLIC g_expectedCaller

.data
ALIGN 16
g_probeReturn   QWORD 0
g_clobberState  QWORD 0
g_clobberAction QWORD 0
g_clobberCaller QWORD 0
g_expectedCaller QWORD 0

.code
ReaderThunkProbe PROC FRAME
    push rbx
    .pushreg rbx
    push r12
    .pushreg r12
    push r13
    .pushreg r13
    push r14
    .pushreg r14
    sub rsp, 28h
    .allocstack 28h
    .endprolog
    mov r12, rcx                          ; the thunk
    mov r13, rdx                          ; state
    mov r14, r8                           ; action
    mov rcx, r13
    mov edx, r14d
    mov r8, 0A8A8A8A8A8A8A8A8h
    mov r9, 0A9A9A9A9A9A9A9A9h
    mov r10, 0AAAAAAAAAAAAAAAAh
    mov r11, 0ABABABABABABABABh
    mov rax, 0C0C0C0C0C0C0C0C0h
    movq xmm0, rax
    mov rax, 0C1C1C1C1C1C1C1C1h
    movq xmm1, rax
    mov rax, 0C2C2C2C2C2C2C2C2h
    movq xmm2, rax
    mov rax, 0C3C3C3C3C3C3C3C3h
    movq xmm3, rax
    mov rax, 0C4C4C4C4C4C4C4C4h
    movq xmm4, rax
    mov rax, 0C5C5C5C5C5C5C5C5h
    movq xmm5, rax
    lea rax, probeReturned
    mov QWORD PTR [g_expectedCaller], rax
    call r12
probeReturned:
    mov QWORD PTR [g_probeReturn], rax
    xor ebx, ebx
    cmp rcx, r13
    je @F
    or ebx, 1
@@: mov eax, r14d
    cmp rdx, rax
    je @F
    or ebx, 2
@@: mov rax, 0A8A8A8A8A8A8A8A8h
    cmp r8, rax
    je @F
    or ebx, 4
@@: mov rax, 0A9A9A9A9A9A9A9A9h
    cmp r9, rax
    je @F
    or ebx, 8
@@: mov rax, 0AAAAAAAAAAAAAAAAh
    cmp r10, rax
    je @F
    or ebx, 10h
@@: mov rax, 0ABABABABABABABABh
    cmp r11, rax
    je @F
    or ebx, 20h
@@: movq rax, xmm0
    mov rcx, 0C0C0C0C0C0C0C0C0h
    cmp rax, rcx
    je @F
    or ebx, 100h
@@: movq rax, xmm1
    mov rcx, 0C1C1C1C1C1C1C1C1h
    cmp rax, rcx
    je @F
    or ebx, 200h
@@: movq rax, xmm2
    mov rcx, 0C2C2C2C2C2C2C2C2h
    cmp rax, rcx
    je @F
    or ebx, 400h
@@: movq rax, xmm3
    mov rcx, 0C3C3C3C3C3C3C3C3h
    cmp rax, rcx
    je @F
    or ebx, 800h
@@: movq rax, xmm4
    mov rcx, 0C4C4C4C4C4C4C4C4h
    cmp rax, rcx
    je @F
    or ebx, 1000h
@@: movq rax, xmm5
    mov rcx, 0C5C5C5C5C5C5C5C5h
    cmp rax, rcx
    je @F
    or ebx, 2000h
@@: mov eax, ebx
    add rsp, 28h
    pop r14
    pop r13
    pop r12
    pop rbx
    ret
ReaderThunkProbe ENDP

ClobberingReader PROC
    mov QWORD PTR [g_clobberCaller], r8
    mov QWORD PTR [g_clobberState], rcx
    mov eax, edx
    mov QWORD PTR [g_clobberAction], rax
    mov rax, 0DEADDEADDEADDEADh
    mov rcx, rax
    mov rdx, rax
    mov r8, rax
    mov r9, rax
    mov r10, rax
    mov r11, rax
    movq xmm0, rax
    movq xmm1, rax
    movq xmm2, rax
    movq xmm3, rax
    movq xmm4, rax
    movq xmm5, rax
    mov eax, 1234h
    ret
ClobberingReader ENDP
END
