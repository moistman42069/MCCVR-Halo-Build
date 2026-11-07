#pragma once

// PROJECT RECLAIMER 0.10'S RECOMPILED HALO 3 (docs/RECLAIMER-0.10-ENGINE-EVIDENCE.md). From 0.10.0 the client does not
// run MCC's halo3.dll: it translates each of the DLL's functions into host code once, keeps the result in
// engine.code, and runs that. Data keeps halo3.dll's layout at its own RVAs from the guest image's base. These are the
// pure decisions the mod needs to put its Halo 3 hooks on the translated functions: reading engine.code's function
// table, and reading from each function's adapter which host registers carry its arguments. No Windows calls here;
// the unit test is tests/lifted_engine_logic_tests.cpp.
//
// A translated function is a HOST BLOCK: an adapter, then the body at `bodyOffset`.
//   - The adapter is a Windows-x64 function taking one pointer: the guest register file (`ctx`). It loads the body's
//     arguments from `ctx`, calls the body, and stores rax to ctx+0 when the function returns a value.
//   - The body takes rdi = ctx, rsi = the guest stack pointer (pointing at the return-address slot; the guest stack is
//     its own memory), and the guest's integer argument registers that it reads, in host registers the adapter shows.
//     Floating-point arguments and results stay in ctx (guest xmm0 at ctx+0x90, 16 bytes a register).
//   - Calls between translated functions call the body directly.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace lifted_engine
{
// The guest register file: x86 order, 8 bytes a general register, then the vector registers.
inline constexpr uint32_t kCtxRax = 0x00, kCtxRcx = 0x08, kCtxRdx = 0x10, kCtxRsp = 0x20, kCtxR8 = 0x40,
                          kCtxR9 = 0x48, kCtxXmm0 = 0x90, kCtxXmmStride = 0x10;
// The Windows x64 integer argument registers in order, as offsets in ctx.
inline constexpr uint32_t kGuestArgOffsets[4] = {kCtxRcx, kCtxRdx, kCtxR8, kCtxR9};

// Host registers by their x86 encoding number.
enum HostReg : int8_t
{
    kRax = 0, kRcx = 1, kRdx = 2, kRbx = 3, kRsp = 4, kRbp = 5, kRsi = 6, kRdi = 7,
    kR8 = 8, kR9 = 9, kR10 = 10, kR11 = 11, kR12 = 12, kR13 = 13, kR14 = 14, kR15 = 15, kNoReg = -1,
};

// ---- engine.code ----------------------------------------------------------------------------------------------------

struct Record
{
    uint32_t guestRva = 0;    // the translated function's start in halo3.dll
    uint32_t guestSize = 0;   // up to the next record (covers MSVC-split fragments and padding)
    uint32_t bodyOffset = 0;  // the body's offset in the host block (the adapter comes first)
    uint64_t hostOffset = 0;  // the host block's offset in the code
    uint32_t hostSize = 0;
};

struct Table
{
    uint64_t preferredBase = 0;   // halo3.dll's own ImageBase
    std::vector<Record> records;  // in guest order, contiguous
    size_t codeOffset = 0;        // where the code starts in the file
    size_t codeLength = 0;
};

enum class ParseResult
{
    Ok,
    NotEngineCode,  // no "RCLMCODE" header
    OtherVersion,   // a format this reading does not know
    Truncated,
    Inconsistent,   // the parts disagree with each other
};

inline const char* ParseResultName(ParseResult result)
{
    switch (result)
    {
    case ParseResult::Ok: return "read";
    case ParseResult::NotEngineCode: return "not an engine.code file";
    case ParseResult::OtherVersion: return "a format version other than 2";
    case ParseResult::Truncated: return "cut short";
    default: return "inconsistent";
    }
}

inline bool ReadLeb(const uint8_t* data, size_t size, size_t& at, uint64_t& value)
{
    value = 0;
    for (unsigned shift = 0; shift < 64; shift += 7)
    {
        if (at >= size)
            return false;
        const uint8_t byte = data[at++];
        value |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if (!(byte & 0x80))
            return true;
    }
    return false;
}

// The file as the client writes it (format 2):
//   0x00 "RCLMCODE", u32 version 2, u32 hash length, the hash, u64 body length (= file length - 0x40), 8 more bytes
//   0x40 body: u64 preferred base, 12 bytes, then LEB128 record count, first guest RVA, 0, and per record LEB128
//        body offset, guest size, host size; more tables and names; then 0x00, LEB128 code length, the code (to the
//        end of the file).
inline ParseResult Parse(const uint8_t* data, size_t size, Table& out)
{
    out = Table{};
    static constexpr char kMagic[8] = {'R', 'C', 'L', 'M', 'C', 'O', 'D', 'E'};
    if (!data || size < 0x40 + 0x14)
        return size >= 8 && data && std::memcmp(data, kMagic, 8) == 0 ? ParseResult::Truncated
                                                                       : ParseResult::NotEngineCode;
    if (std::memcmp(data, kMagic, 8) != 0)
        return ParseResult::NotEngineCode;
    uint32_t version = 0, hashLength = 0;
    std::memcpy(&version, data + 8, 4);
    std::memcpy(&hashLength, data + 12, 4);
    if (version != 2 || hashLength != 0x20)
        return ParseResult::OtherVersion;
    uint64_t bodyLength = 0;
    std::memcpy(&bodyLength, data + 0x30, 8);
    if (bodyLength != size - 0x40)
        return ParseResult::Inconsistent;
    const uint8_t* body = data + 0x40;
    const size_t bodySize = size - 0x40;
    std::memcpy(&out.preferredBase, body, 8);

    size_t at = 0x14;
    uint64_t count = 0, guest = 0, zero = 0;
    if (!ReadLeb(body, bodySize, at, count) || !ReadLeb(body, bodySize, at, guest) ||
        !ReadLeb(body, bodySize, at, zero))
        return ParseResult::Truncated;
    if (!count || count > 1000000 || guest > 0xFFFFFFFFull)
        return ParseResult::Inconsistent;
    out.records.reserve(static_cast<size_t>(count));
    uint64_t host = 0;
    for (uint64_t i = 0; i < count; ++i)
    {
        uint64_t bodyOffset = 0, guestSize = 0, hostSize = 0;
        if (!ReadLeb(body, bodySize, at, bodyOffset) || !ReadLeb(body, bodySize, at, guestSize) ||
            !ReadLeb(body, bodySize, at, hostSize))
            return ParseResult::Truncated;
        if (!guestSize || !hostSize || bodyOffset > 0xFFFF || guest + guestSize > 0xFFFFFFFFull ||
            hostSize > 0xFFFFFFFFull)
            return ParseResult::Inconsistent;
        Record record;
        record.guestRva = static_cast<uint32_t>(guest);
        record.guestSize = static_cast<uint32_t>(guestSize);
        record.bodyOffset = static_cast<uint32_t>(bodyOffset);
        record.hostOffset = host;
        record.hostSize = static_cast<uint32_t>(hostSize);
        out.records.push_back(record);
        guest += guestSize;
        host += hostSize;
    }
    // The code runs to the end of the file, and its length precedes it after a 0 that ends the names.
    for (size_t start = at + 1; start + 1 < bodySize; ++start)
    {
        if (body[start - 1] != 0)
            continue;
        size_t after = start;
        uint64_t length = 0;
        if (!ReadLeb(body, bodySize, after, length) || length != bodySize - after)
            continue;
        if (length < host || length - host > 0x10000)
            return ParseResult::Inconsistent;
        out.codeOffset = 0x40 + after;
        out.codeLength = static_cast<size_t>(length);
        return ParseResult::Ok;
    }
    return ParseResult::Inconsistent;
}

// Whether a record's block holds a body after its adapter. The table's last record (the end of .text) does not.
inline bool HasBody(const Record& record)
{
    return record.bodyOffset < record.hostSize;
}

// Where in `code` (a body) the first `window` bytes with no 0 byte start: bytes that are the same in the file and in
// memory. The client fills every relocation in when it loads the code, and in the file each one is 0 (a call's
// rel32, a movabs's imm64), so a window without a 0 holds none. False when the body has no such window.
inline bool RelocationFreeWindow(const uint8_t* code, size_t length, size_t window, size_t& at)
{
    size_t run = 0;
    for (size_t i = 0; i < length; ++i)
    {
        run = code[i] ? run + 1 : 0;
        if (run == window)
        {
            at = i + 1 - window;
            return true;
        }
    }
    return false;
}

// The record that starts exactly at `rva`, or null (a hook has to be on a whole function).
inline const Record* FindStart(const Table& table, uint32_t rva)
{
    size_t lo = 0, hi = table.records.size();
    while (lo < hi)
    {
        const size_t mid = lo + (hi - lo) / 2;
        if (table.records[mid].guestRva < rva)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < table.records.size() && table.records[lo].guestRva == rva ? &table.records[lo] : nullptr;
}

// ---- the adapter ----------------------------------------------------------------------------------------------------

struct Adapter
{
    // For guest rcx, rdx, r8, r9 (kGuestArgOffsets): the host register the body receives it in, or kNoReg when the
    // body does not read it.
    int8_t hostRegOfGuestArg[4] = {kNoReg, kNoReg, kNoReg, kNoReg};
    bool returnsValue = false;  // the adapter stores rax to ctx+0 after the call
    uint32_t length = 0;        // bytes up to and including the adapter's ret
};

// Reads the adapter at the start of a host block. Understands exactly the instructions the client's adapters are made
// of (push/pop rbp, ret, sub/add rsp, 64-bit mov and lea between registers and [base + disp], movdqu to and from the
// stack, one call); anything else, or a call whose rdi and rsi are not ctx and the guest stack pointer, is false and
// that function is not hooked.
namespace detail
{
enum Kind : uint8_t { Unknown, Ctx, Field };
struct Value
{
    Kind kind = Unknown;
    uint32_t offset = 0;
};
} // namespace detail

inline bool DecodeAdapter(const uint8_t* code, size_t size, Adapter& out)
{
    using detail::Ctx;
    using detail::Field;
    using detail::Value;
    out = Adapter{};
    Value regs[16]{};
    regs[kRcx].kind = Ctx;
    Value atCall[16]{};
    bool called = false;
    size_t i = 0;
    while (i < size)
    {
        const uint8_t op = code[i];
        if (op == 0x55 || op == 0x5D) // push rbp, pop rbp
        {
            ++i;
            continue;
        }
        if (op == 0xC3)
        {
            if (!called)
                return false;
            out.length = static_cast<uint32_t>(i + 1);
            const Value& ctxIn = atCall[kRdi];
            const Value& stackIn = atCall[kRsi];
            if (ctxIn.kind != Ctx || stackIn.kind != Field || stackIn.offset != kCtxRsp)
                return false;
            static constexpr int8_t kParams[4] = {kRdx, kRcx, kR8, kR9};
            for (const int8_t host : kParams)
            {
                const Value& value = atCall[host];
                if (value.kind != Field)
                    continue;
                int arg = -1;
                for (int a = 0; a < 4; ++a)
                    if (kGuestArgOffsets[a] == value.offset)
                        arg = a;
                if (arg < 0 || out.hostRegOfGuestArg[arg] != kNoReg)
                    return false;
                out.hostRegOfGuestArg[arg] = host;
            }
            return true;
        }
        if (op == 0xE8)
        {
            if (called || i + 5 > size)
                return false;
            std::memcpy(atCall, regs, sizeof(regs));
            called = true;
            i += 5;
            continue;
        }
        // movdqu xmm <-> [rsp + disp]: F3 [REX] 0F 6F/7F modrm SIB disp
        size_t at = i;
        uint8_t rex = 0;
        bool vector = false;
        if (op == 0xF3)
        {
            vector = true;
            ++at;
            if (at < size && (code[at] & 0xF0) == 0x40)
                rex = code[at++];
            if (at + 1 >= size || code[at] != 0x0F || (code[at + 1] != 0x6F && code[at + 1] != 0x7F))
                return false;
            at += 2;
        }
        else
        {
            if ((op & 0xF8) != 0x48) // REX.W
                return false;
            rex = op;
            ++at;
        }
        if (at + 1 >= size)
            return false;
        const uint8_t opcode = vector ? 0 : code[at++];
        const uint8_t modrm = code[at++];
        const uint8_t mod = modrm >> 6;
        const int reg = ((modrm >> 3) & 7) | ((rex & 4) ? 8 : 0);
        int rm = (modrm & 7) | ((rex & 1) ? 8 : 0);
        if (!vector && (opcode == 0x81 || opcode == 0x83))
        {
            // sub rsp / add rsp, imm
            if (modrm != 0xEC && modrm != 0xC4)
                return false;
            at += opcode == 0x81 ? 4 : 1;
            if (at > size)
                return false;
            i = at;
            continue;
        }
        if (!vector && opcode != 0x89 && opcode != 0x8B && opcode != 0x8D)
            return false;
        if (mod == 3)
        {
            if (vector || opcode == 0x8D)
                return false;
            const int dst = opcode == 0x89 ? rm : reg;
            const int src = opcode == 0x89 ? reg : rm;
            regs[dst] = regs[src];
            i = at;
            continue;
        }
        int base = rm;
        bool indexed = false;
        if ((modrm & 7) == 4)
        {
            if (at >= size)
                return false;
            const uint8_t sib = code[at++];
            base = (sib & 7) | ((rex & 1) ? 8 : 0);
            indexed = (((sib >> 3) & 7) | ((rex & 2) ? 8 : 0)) != 4;
            if (mod == 0 && (sib & 7) == 5)
                return false;
        }
        else if (mod == 0 && (modrm & 7) == 5)
            return false; // rip-relative: not in an adapter
        int32_t disp = 0;
        if (mod == 1)
        {
            if (at >= size)
                return false;
            disp = static_cast<int8_t>(code[at++]);
        }
        else if (mod == 2)
        {
            if (at + 4 > size)
                return false;
            std::memcpy(&disp, code + at, 4);
            at += 4;
        }
        i = at;
        if (vector)
            continue; // saving or restoring xmm6-15 on the host stack
        const bool fromCtx = !indexed && regs[base].kind == Ctx && disp >= 0;
        if (opcode == 0x8B)
        {
            regs[reg] = fromCtx ? Value{Field, static_cast<uint32_t>(disp)} : Value{};
        }
        else if (opcode == 0x8D)
        {
            regs[reg] = Value{};
        }
        else if (called && fromCtx && disp == 0 && reg == kRax)
        {
            out.returnsValue = true; // mov [ctx], rax
        }
    }
    return false;
}

// The host registers a body expects, filled from a guest register file: what an adapter would load. `ctx` is the
// file; `hostArgs` is indexed rdx, rcx, r8, r9 (HostArgSlot).
inline int HostArgSlot(int8_t hostReg)
{
    switch (hostReg)
    {
    case kRdx: return 0;
    case kRcx: return 1;
    case kR8: return 2;
    case kR9: return 3;
    default: return -1;
    }
}
inline void HostArgsFromCtx(const Adapter& adapter, const uint8_t* ctx, uint64_t hostArgs[4])
{
    for (int arg = 0; arg < 4; ++arg)
    {
        const int slot = HostArgSlot(adapter.hostRegOfGuestArg[arg]);
        if (slot >= 0)
            std::memcpy(&hostArgs[slot], ctx + kGuestArgOffsets[arg], 8);
    }
}
// The other way: what a body received in host registers, written back to the guest register file.
inline void CtxFromHostArgs(const Adapter& adapter, const uint64_t hostArgs[4], uint8_t* ctx)
{
    for (int arg = 0; arg < 4; ++arg)
    {
        const int slot = HostArgSlot(adapter.hostRegOfGuestArg[arg]);
        if (slot >= 0)
            std::memcpy(ctx + kGuestArgOffsets[arg], &hostArgs[slot], 8);
    }
}

// ---- the guest caller of a direct call ------------------------------------------------------------------------------

// A translated DIRECT call stores no guest return address (only an indirect call does, for the dispatcher), so a hook
// on a body knows its caller only by the host return address: inside the caller's translated block. The translation
// keeps a function's calls in order, so the n-th `call rel32` to the hooked body in the caller's host block is the
// n-th `call rel32` to the hooked function in the caller's guest code. Both sides are scanned as bytes: a false E8
// would have to name exactly the target.
inline constexpr size_t kMaxCallSites = 64;
inline size_t CallSitesTo(const uint8_t* code, size_t length, uint64_t base, uint64_t target, size_t sites[kMaxCallSites])
{
    size_t count = 0;
    for (size_t at = 0; at + 5 <= length; ++at)
    {
        if (code[at] != 0xE8)
            continue;
        int32_t rel = 0;
        std::memcpy(&rel, code + at + 1, 4);
        if (base + at + 5 + static_cast<int64_t>(rel) != target)
            continue;
        if (count == kMaxCallSites)
            return kMaxCallSites + 1; // too many to pair
        sites[count++] = at;
    }
    return count;
}

// The guest return address of the call at `hostCall` (the host return address - 5) in the caller's host block (`host`,
// `hostLength` bytes at `hostBase`) to the hooked body `hostTarget`, from the caller's guest code (`guest`,
// `guestLength` bytes at `guestBase`, halo3.dll's own bytes) and the hooked function's guest start `guestTarget`.
// 0 when that is no `call rel32` to the body, or the two sides do not hold the same number of such calls (then the
// order cannot be trusted).
inline uint64_t GuestReturnOfCall(const uint8_t* host, size_t hostLength, uint64_t hostBase, uint64_t hostCall,
                                  uint64_t hostTarget, const uint8_t* guest, size_t guestLength, uint64_t guestBase,
                                  uint64_t guestTarget)
{
    size_t hostSites[kMaxCallSites], guestSites[kMaxCallSites];
    const size_t hosts = CallSitesTo(host, hostLength, hostBase, hostTarget, hostSites);
    const size_t guests = CallSitesTo(guest, guestLength, guestBase, guestTarget, guestSites);
    if (!hosts || hosts != guests || hosts > kMaxCallSites)
        return 0;
    for (size_t i = 0; i < hosts; ++i)
        if (hostBase + hostSites[i] == hostCall)
            return guestBase + guestSites[i] + 5;
    return 0;
}

// ---- the client's own versions of guest functions -------------------------------------------------------------------

// The client replaces some guest functions with versions of its own, in its executable. Translated code reaches one
// through a stub past the end of the code (`jmp [rip+2]`, two int3, the target's address) with an ordinary Windows x64
// call: rcx = ctx, the guest's arguments in ctx, and its stack pointer at the guest return address, which the
// translated code formed just before as `lea r64, [r64 + disp32]` (disp32 = the guest return RVA; the base register
// holds the guest image's base) and stored. The client's version pops it (ctx rsp + 8) and leaves its result in ctx.
inline constexpr uint8_t kStubPrefix[8] = {0xFF, 0x25, 0x02, 0x00, 0x00, 0x00, 0xCC, 0xCC};
inline constexpr size_t kStubSize = 16;
inline constexpr size_t kStubLeaWindow = 0x90; // how far before the call the lea is looked for

// Whether `stub` (kStubSize bytes) is such a stub.
inline bool IsStub(const uint8_t* stub)
{
    return std::memcmp(stub, kStubPrefix, sizeof(kStubPrefix)) == 0;
}

// The guest return RVA the translated code formed before the call at `call` (an offset in `code`): the constant of the
// last `lea r64, [r64 + disp32]` (base register not rsp) or `add r64, imm32` in the window before it that lies in
// [lowRva, highRva); 0 if there is none. Both forms add the RVA to the guest image's base held in a register.
inline uint32_t GuestReturnBeforeCall(const uint8_t* code, size_t call, uint32_t lowRva, uint32_t highRva)
{
    const size_t from = call > kStubLeaWindow ? call - kStubLeaWindow : 0;
    uint32_t found = 0;
    for (size_t at = from; at + 7 <= call; ++at)
    {
        if ((code[at] & 0xF8) != 0x48) // REX.W
            continue;
        const uint8_t opcode = code[at + 1], modrm = code[at + 2];
        const bool lea = opcode == 0x8D && (modrm & 0xC0) == 0x80 && (modrm & 7) != 4;
        const bool add = opcode == 0x81 && (modrm & 0xF8) == 0xC0;
        if (!lea && !add)
            continue;
        int32_t value = 0;
        std::memcpy(&value, code + at + 3, 4);
        if (value >= static_cast<int64_t>(lowRva) && value < static_cast<int64_t>(highRva))
            found = static_cast<uint32_t>(value);
    }
    return found;
}

// Every `call rel32` in code[0, codeLength) to a stub in code[codeLength, size): `found(stubOffset, guestReturnRva)`,
// the RVA 0 when no lea names it.
template <typename Found>
inline void ForEachStubCall(const uint8_t* code, size_t codeLength, size_t size, uint32_t lowRva, uint32_t highRva,
                            Found found)
{
    for (size_t at = 0; at + 5 <= codeLength; ++at)
    {
        if (code[at] != 0xE8)
            continue;
        int32_t rel = 0;
        std::memcpy(&rel, code + at + 1, 4);
        const int64_t target = static_cast<int64_t>(at) + 5 + rel;
        if (target < static_cast<int64_t>(codeLength) || target + static_cast<int64_t>(kStubSize) > static_cast<int64_t>(size) ||
            !IsStub(code + target))
            continue;
        found(static_cast<size_t>(target), GuestReturnBeforeCall(code, at, lowRva, highRva));
    }
}

// Whether the client's version at `function` (its first `length` bytes) calls the translated function itself, through
// an address it loads from its own data (`mov r64, [rip + disp32]` or `call [rip + disp32]`) that `readQword` reads
// as `adapter` or `body`. Such a version wraps the translated function, so a hook on the body already runs inside it.
template <typename ReadQword>
inline bool CallsTranslatedFunction(const uint8_t* function, size_t length, uint64_t address, uint64_t adapter,
                                    uint64_t body, ReadQword readQword)
{
    for (size_t at = 0; at + 7 <= length; ++at)
    {
        size_t dispAt = 0, next = 0;
        if ((function[at] == 0x48 || function[at] == 0x4C) && function[at + 1] == 0x8B && (function[at + 2] & 0xC7) == 0x05)
            dispAt = at + 3, next = at + 7;
        else if (function[at] == 0xFF && function[at + 1] == 0x15)
            dispAt = at + 2, next = at + 6;
        else
            continue;
        int32_t disp = 0;
        std::memcpy(&disp, function + dispAt, 4);
        uint64_t value = 0;
        if (readQword(address + next + static_cast<int64_t>(disp), value) && (value == adapter || value == body))
            return true;
    }
    return false;
}
} // namespace lifted_engine
