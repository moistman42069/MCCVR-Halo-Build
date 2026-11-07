#include "../src/common/lifted_engine_logic.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace lifted_engine;

#define CHECK(condition) \
    do { if (!(condition)) { std::printf("FAILED line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (0)

// Real adapters from the client's engine.code (0.10.0, docs/RECLAIMER-0.10-ENGINE-EVIDENCE.md).
// RVA 0x1280 (`movss [rcx+0xDC], xmm1; ret`): one integer argument, no value returned.
static const uint8_t kAdapter1280[] = {
    0x55, 0x48, 0x89, 0xE5, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00, 0x48, 0x89, 0x34, 0x24, 0x48, 0x89, 0x7C, 0x24,
    0x08, 0x4C, 0x89, 0x64, 0x24, 0x10, 0x4C, 0x89, 0x6C, 0x24, 0x18, 0xF3, 0x0F, 0x7F, 0x74, 0x24, 0x20, 0xF3, 0x0F,
    0x7F, 0x7C, 0x24, 0x30, 0xF3, 0x44, 0x0F, 0x7F, 0x44, 0x24, 0x40, 0xF3, 0x44, 0x0F, 0x7F, 0x4C, 0x24, 0x50, 0xF3,
    0x44, 0x0F, 0x7F, 0x54, 0x24, 0x60, 0xF3, 0x44, 0x0F, 0x7F, 0x5C, 0x24, 0x70, 0xF3, 0x44, 0x0F, 0x7F, 0xA4, 0x24,
    0x80, 0x00, 0x00, 0x00, 0xF3, 0x44, 0x0F, 0x7F, 0xAC, 0x24, 0x90, 0x00, 0x00, 0x00, 0xF3, 0x44, 0x0F, 0x7F, 0xB4,
    0x24, 0xA0, 0x00, 0x00, 0x00, 0xF3, 0x44, 0x0F, 0x7F, 0xBC, 0x24, 0xB0, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0x61, 0x20,
    0x48, 0x8B, 0x51, 0x08, 0x49, 0x89, 0xCD, 0x4C, 0x89, 0xE6, 0x4C, 0x89, 0xEF, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x8D, 0x74, 0x24, 0x08, 0x4C, 0x89, 0xE9, 0x48, 0x89, 0x71, 0x20, 0x48, 0x8B, 0x34, 0x24, 0x48, 0x8B, 0x7C, 0x24,
    0x08, 0x4C, 0x8B, 0x64, 0x24, 0x10, 0x4C, 0x8B, 0x6C, 0x24, 0x18, 0xF3, 0x0F, 0x6F, 0x74, 0x24, 0x20, 0xF3, 0x0F,
    0x6F, 0x7C, 0x24, 0x30, 0xF3, 0x44, 0x0F, 0x6F, 0x44, 0x24, 0x40, 0xF3, 0x44, 0x0F, 0x6F, 0x4C, 0x24, 0x50, 0xF3,
    0x44, 0x0F, 0x6F, 0x54, 0x24, 0x60, 0xF3, 0x44, 0x0F, 0x6F, 0x5C, 0x24, 0x70, 0xF3, 0x44, 0x0F, 0x6F, 0xA4, 0x24,
    0x80, 0x00, 0x00, 0x00, 0xF3, 0x44, 0x0F, 0x6F, 0xAC, 0x24, 0x90, 0x00, 0x00, 0x00, 0xF3, 0x44, 0x0F, 0x6F, 0xB4,
    0x24, 0xA0, 0x00, 0x00, 0x00, 0xF3, 0x44, 0x0F, 0x6F, 0xBC, 0x24, 0xB0, 0x00, 0x00, 0x00, 0x48, 0x81, 0xC4, 0xC0,
    0x00, 0x00, 0x00, 0x48, 0x89, 0xEC, 0x5D, 0xC3};
// RVA 0xF7DF0 (the control update): guest rcx, r8 and r9, a value returned.
static const uint8_t kAdapterF7DF0[] = {
    0x55, 0x48, 0x89, 0xE5, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00, 0x48, 0x89, 0x34, 0x24, 0x48, 0x89, 0x7C, 0x24,
    0x08, 0x4C, 0x89, 0x64, 0x24, 0x10, 0x4C, 0x89, 0x6C, 0x24, 0x18, 0xF3, 0x0F, 0x7F, 0x74, 0x24, 0x20, 0xF3, 0x0F,
    0x7F, 0x7C, 0x24, 0x30, 0xF3, 0x44, 0x0F, 0x7F, 0x44, 0x24, 0x40, 0xF3, 0x44, 0x0F, 0x7F, 0x4C, 0x24, 0x50, 0xF3,
    0x44, 0x0F, 0x7F, 0x54, 0x24, 0x60, 0xF3, 0x44, 0x0F, 0x7F, 0x5C, 0x24, 0x70, 0xF3, 0x44, 0x0F, 0x7F, 0xA4, 0x24,
    0x80, 0x00, 0x00, 0x00, 0xF3, 0x44, 0x0F, 0x7F, 0xAC, 0x24, 0x90, 0x00, 0x00, 0x00, 0xF3, 0x44, 0x0F, 0x7F, 0xB4,
    0x24, 0xA0, 0x00, 0x00, 0x00, 0xF3, 0x44, 0x0F, 0x7F, 0xBC, 0x24, 0xB0, 0x00, 0x00, 0x00, 0x49, 0x89, 0xCD, 0x4C,
    0x8B, 0x61, 0x20, 0x48, 0x8B, 0x51, 0x08, 0x48, 0x8B, 0x49, 0x40, 0x4C, 0x89, 0xE8, 0x4C, 0x8B, 0x40, 0x48, 0x4C,
    0x89, 0xE6, 0x4C, 0x89, 0xEF, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x4C, 0x89, 0xE9, 0x48, 0x89, 0x01, 0x4D, 0x8D, 0x44,
    0x24, 0x08, 0x4C, 0x89, 0x41, 0x20, 0x48, 0x8B, 0x34, 0x24, 0x48, 0x8B, 0x7C, 0x24, 0x08, 0x4C, 0x8B, 0x64, 0x24,
    0x10, 0x4C, 0x8B, 0x6C, 0x24, 0x18, 0xF3, 0x0F, 0x6F, 0x74, 0x24, 0x20, 0xF3, 0x0F, 0x6F, 0x7C, 0x24, 0x30, 0xF3,
    0x44, 0x0F, 0x6F, 0x44, 0x24, 0x40, 0xF3, 0x44, 0x0F, 0x6F, 0x4C, 0x24, 0x50, 0xF3, 0x44, 0x0F, 0x6F, 0x54, 0x24,
    0x60, 0xF3, 0x44, 0x0F, 0x6F, 0x5C, 0x24, 0x70, 0xF3, 0x44, 0x0F, 0x6F, 0xA4, 0x24, 0x80, 0x00, 0x00, 0x00, 0xF3,
    0x44, 0x0F, 0x6F, 0xAC, 0x24, 0x90, 0x00, 0x00, 0x00, 0xF3, 0x44, 0x0F, 0x6F, 0xB4, 0x24, 0xA0, 0x00, 0x00, 0x00,
    0xF3, 0x44, 0x0F, 0x6F, 0xBC, 0x24, 0xB0, 0x00, 0x00, 0x00, 0x48, 0x81, 0xC4, 0xC0, 0x00, 0x00, 0x00, 0x48, 0x89,
    0xEC, 0x5D, 0xC3};

static void Leb(std::vector<uint8_t>& out, uint64_t value)
{
    do
    {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        out.push_back(byte | (value ? 0x80 : 0));
    } while (value);
}

// A small engine.code: three records, one name, code of `codeLength` bytes.
static std::vector<uint8_t> MakeFile(size_t codeLength, uint32_t version = 2)
{
    std::vector<uint8_t> body(0x14, 0);
    const uint64_t base = 0x180000000ull;
    std::memcpy(body.data(), &base, 8);
    Leb(body, 3);
    Leb(body, 0x1000);
    Leb(body, 0);
    const uint32_t records[3][3] = {{288, 0x250, 0x520}, {256, 0x10, 0x120}, {288, 0x60, 0x200}};
    for (const auto& r : records)
        for (const uint32_t v : r)
            Leb(body, v);
    const char name[] = "\x0b" "first_names";
    body.insert(body.end(), name, name + sizeof(name) - 1);
    body.push_back(0);
    Leb(body, codeLength);
    body.resize(body.size() + codeLength, 0xCC);
    std::vector<uint8_t> file(0x40, 0);
    std::memcpy(file.data(), "RCLMCODE", 8);
    const uint32_t hashLength = 0x20;
    std::memcpy(file.data() + 8, &version, 4);
    std::memcpy(file.data() + 12, &hashLength, 4);
    const uint64_t bodyLength = body.size();
    std::memcpy(file.data() + 0x30, &bodyLength, 8);
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

int main()
{
    // LEB128
    {
        const uint8_t bytes[] = {0xEE, 0xB2, 0x01, 0x80, 0x20};
        size_t at = 0;
        uint64_t v = 0;
        CHECK(ReadLeb(bytes, sizeof(bytes), at, v) && v == 22894 && at == 3);
        CHECK(ReadLeb(bytes, sizeof(bytes), at, v) && v == 0x1000 && at == 5);
        CHECK(!ReadLeb(bytes, sizeof(bytes), at, v));
        const uint8_t open[] = {0x80, 0x80};
        at = 0;
        CHECK(!ReadLeb(open, sizeof(open), at, v));
    }

    // engine.code
    {
        const size_t code = 0x520 + 0x120 + 0x200 + 0xBF;
        const std::vector<uint8_t> file = MakeFile(code);
        Table table;
        CHECK(Parse(file.data(), file.size(), table) == ParseResult::Ok);
        CHECK(table.preferredBase == 0x180000000ull);
        CHECK(table.records.size() == 3);
        CHECK(table.records[0].guestRva == 0x1000 && table.records[0].bodyOffset == 288 &&
              table.records[0].hostOffset == 0 && table.records[0].hostSize == 0x520);
        CHECK(table.records[1].guestRva == 0x1250 && table.records[1].hostOffset == 0x520 &&
              table.records[1].bodyOffset == 256);
        CHECK(table.records[2].guestRva == 0x1260 && table.records[2].hostOffset == 0x640);
        CHECK(table.codeLength == code && table.codeOffset + code == file.size());
        CHECK(FindStart(table, 0x1250) == &table.records[1]);
        CHECK(FindStart(table, 0x1000) == &table.records[0]);
        CHECK(!FindStart(table, 0x1254));   // inside a function: never hooked
        CHECK(!FindStart(table, 0x900));
        CHECK(!FindStart(table, 0x2000));

        // Code shorter than the blocks, or far longer: the parts disagree.
        CHECK(Parse(MakeFile(0x500).data(), MakeFile(0x500).size(), table) == ParseResult::Inconsistent);
        CHECK(Parse(MakeFile(code + 0x20000).data(), MakeFile(code + 0x20000).size(), table) ==
              ParseResult::Inconsistent);
        const std::vector<uint8_t> v3 = MakeFile(code, 3);
        CHECK(Parse(v3.data(), v3.size(), table) == ParseResult::OtherVersion);
        std::vector<uint8_t> cut = file;
        cut.resize(cut.size() - 1);
        CHECK(Parse(cut.data(), cut.size(), table) == ParseResult::Inconsistent);   // the header's length disagrees
        std::vector<uint8_t> notCode = file;
        notCode[0] = 'X';
        CHECK(Parse(notCode.data(), notCode.size(), table) == ParseResult::NotEngineCode);
        CHECK(Parse(file.data(), 20, table) == ParseResult::Truncated);
        CHECK(Parse(nullptr, 0, table) == ParseResult::NotEngineCode);
    }

    // A window with no relocation in it: no 0 byte.
    {
        const uint8_t body[] = {0x55, 0x48, 0x89, 0xE5, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x83, 0xEC, 0x20, 0x49,
                                0x89, 0xCD, 0x4C, 0x89, 0xE6, 0x00, 0x48, 0x8B, 0x51, 0x08};
        size_t at = 99;
        CHECK(RelocationFreeWindow(body, sizeof(body), 4, at) && at == 0);   // push rbp; mov rbp, rsp
        CHECK(RelocationFreeWindow(body, sizeof(body), 8, at) && at == 9);   // after the call's rel32
        CHECK(!RelocationFreeWindow(body, sizeof(body), 11, at));
        CHECK(RelocationFreeWindow(body, sizeof(body), 10, at) && at == 9);
        CHECK(!RelocationFreeWindow(body, 0, 4, at));
    }

    // Adapters
    {
        Adapter a;
        CHECK(DecodeAdapter(kAdapter1280, sizeof(kAdapter1280), a));
        CHECK(a.length == sizeof(kAdapter1280));
        CHECK(!a.returnsValue);
        CHECK(a.hostRegOfGuestArg[0] == kRdx);   // guest rcx
        CHECK(a.hostRegOfGuestArg[1] == kNoReg && a.hostRegOfGuestArg[2] == kNoReg && a.hostRegOfGuestArg[3] == kNoReg);

        CHECK(DecodeAdapter(kAdapterF7DF0, sizeof(kAdapterF7DF0), a));
        CHECK(a.returnsValue);
        CHECK(a.hostRegOfGuestArg[0] == kRdx);   // guest rcx
        CHECK(a.hostRegOfGuestArg[1] == kNoReg); // guest rdx unread
        CHECK(a.hostRegOfGuestArg[2] == kRcx);   // guest r8
        CHECK(a.hostRegOfGuestArg[3] == kR8);    // guest r9

        // Cut short, or an instruction no adapter has: not decoded.
        CHECK(!DecodeAdapter(kAdapterF7DF0, 140, a));
        std::vector<uint8_t> odd(kAdapterF7DF0, kAdapterF7DF0 + sizeof(kAdapterF7DF0));
        odd[0] = 0x90;
        CHECK(!DecodeAdapter(odd.data(), odd.size(), a));
        // rdi not ctx at the call (the `mov rdi, r13` replaced by `mov rdi, r12`): not decoded.
        std::vector<uint8_t> wrongCtx(kAdapterF7DF0, kAdapterF7DF0 + sizeof(kAdapterF7DF0));
        for (size_t i = 0; i + 2 < wrongCtx.size(); ++i)
            if (wrongCtx[i] == 0x4C && wrongCtx[i + 1] == 0x89 && wrongCtx[i + 2] == 0xEF)
            {
                wrongCtx[i + 2] = 0xE7;
                break;
            }
        CHECK(!DecodeAdapter(wrongCtx.data(), wrongCtx.size(), a));

        // The host registers to and from the guest register file.
        CHECK(DecodeAdapter(kAdapterF7DF0, sizeof(kAdapterF7DF0), a));
        uint8_t ctx[0x200]{};
        const uint64_t rcx = 0x1111, r8 = 0x8888, r9 = 0x9999, rdx = 0x2222;
        std::memcpy(ctx + kCtxRcx, &rcx, 8);
        std::memcpy(ctx + kCtxRdx, &rdx, 8);
        std::memcpy(ctx + kCtxR8, &r8, 8);
        std::memcpy(ctx + kCtxR9, &r9, 8);
        uint64_t host[4]{};
        HostArgsFromCtx(a, ctx, host);
        CHECK(host[HostArgSlot(kRdx)] == rcx && host[HostArgSlot(kRcx)] == r8 && host[HostArgSlot(kR8)] == r9 &&
              host[HostArgSlot(kR9)] == 0);
        uint8_t back[0x200]{};
        CtxFromHostArgs(a, host, back);
        uint64_t v = 0;
        std::memcpy(&v, back + kCtxRcx, 8);
        CHECK(v == rcx);
        std::memcpy(&v, back + kCtxR8, 8);
        CHECK(v == r8);
        std::memcpy(&v, back + kCtxR9, 8);
        CHECK(v == r9);
        std::memcpy(&v, back + kCtxRdx, 8);
        CHECK(v == 0);   // not an argument of this body: left alone
        CHECK(HostArgSlot(kR10) == -1);
    }
    // The guest call site of a direct call: the n-th host call to the body is the n-th guest call to the function.
    {
        // Host block at 0x10000: calls to the body 0x50000 at +0x10 and +0x40, one to another body at +0x28.
        uint8_t host[0x60]{};
        const auto putCall = [](uint8_t* code, uint64_t base, size_t at, uint64_t target) {
            code[at] = 0xE8;
            const int32_t rel = static_cast<int32_t>(static_cast<int64_t>(target) - static_cast<int64_t>(base + at + 5));
            std::memcpy(code + at + 1, &rel, 4);
        };
        putCall(host, 0x10000, 0x10, 0x50000);
        putCall(host, 0x10000, 0x28, 0x60000);
        putCall(host, 0x10000, 0x40, 0x50000);
        // Guest function at 0x180001000: calls to the hooked function 0x180009000 at +0x08 and +0x30.
        uint8_t guest[0x50]{};
        putCall(guest, 0x180001000, 0x08, 0x180009000);
        putCall(guest, 0x180001000, 0x30, 0x180009000);
        size_t sites[kMaxCallSites];
        CHECK(CallSitesTo(host, sizeof(host), 0x10000, 0x50000, sites) == 2 && sites[0] == 0x10 && sites[1] == 0x40);
        CHECK(GuestReturnOfCall(host, sizeof(host), 0x10000, 0x10010, 0x50000, guest, sizeof(guest), 0x180001000,
                                0x180009000) == 0x180001000 + 0x08 + 5);
        CHECK(GuestReturnOfCall(host, sizeof(host), 0x10000, 0x10040, 0x50000, guest, sizeof(guest), 0x180001000,
                                0x180009000) == 0x180001000 + 0x30 + 5);
        // Not a call to the body, or a count that does not pair: no answer.
        CHECK(GuestReturnOfCall(host, sizeof(host), 0x10000, 0x10028, 0x50000, guest, sizeof(guest), 0x180001000,
                                0x180009000) == 0);
        putCall(guest, 0x180001000, 0x40, 0x180009000);
        CHECK(GuestReturnOfCall(host, sizeof(host), 0x10000, 0x10010, 0x50000, guest, sizeof(guest), 0x180001000,
                                0x180009000) == 0);
        CHECK(GuestReturnOfCall(host, 0, 0x10000, 0x10010, 0x50000, guest, sizeof(guest), 0x180001000,
                                0x180009000) == 0);
    }
    // Calls to the client's own versions of guest functions, through stubs past the code.
    {
        // 0x200 bytes of translated code, a stub at 0x200 (the code's end) and one at 0x210.
        uint8_t code[0x220]{};
        const auto putBytes = [&](size_t at, std::initializer_list<uint8_t> bytes) {
            for (const uint8_t b : bytes)
                code[at++] = b;
        };
        const auto putCall = [&](size_t at, size_t target) {
            code[at] = 0xE8;
            const int32_t rel = static_cast<int32_t>(static_cast<int64_t>(target) - static_cast<int64_t>(at + 5));
            std::memcpy(code + at + 1, &rel, 4);
        };
        putBytes(0x200, {0xFF, 0x25, 0x02, 0x00, 0x00, 0x00, 0xCC, 0xCC, 1, 2, 3, 4, 5, 6, 7, 8});
        putBytes(0x210, {0xFF, 0x25, 0x02, 0x00, 0x00, 0x00, 0xCC, 0xCC, 8, 7, 6, 5, 4, 3, 2, 1});
        CHECK(IsStub(code + 0x200) && IsStub(code + 0x210) && !IsStub(code));
        // As the client's code calls the first-person camera rebuild (0.10.0, its caller at RVA 0x2835D4): the guest
        // return address formed (lea rsi, [rax + 0x283786]), a lea of another kind (lea rsi, [r15 + 0x6C8]) and one
        // on rsp, then `mov rcx, rbx; call stub`.
        putBytes(0x40, {0x48, 0x8D, 0xB0, 0x86, 0x37, 0x28, 0x00});
        putBytes(0x50, {0x49, 0x8D, 0xB7, 0xC8, 0x06, 0x00, 0x00});
        putBytes(0x58, {0x48, 0x8D, 0x84, 0x24, 0x00, 0x10, 0x00, 0x00});
        putBytes(0x60, {0x48, 0x89, 0xD9});
        putCall(0x63, 0x200);
        putCall(0x80, 0x20);   // a call inside the code: not a stub call
        putCall(0x1F0, 0x210); // a stub call with no lea in reach
        CHECK(GuestReturnBeforeCall(code, 0x63, 0x1000, 0x700000) == 0x283786);
        CHECK(GuestReturnBeforeCall(code, 0x63, 0x300000, 0x700000) == 0); // outside the guest code
        // The other form, as the client's code calls the HUD widget draw (its caller at RVA 0x2D289C): the image base
        // loaded, then `add rdx, 0x2D2A3B` (mov rdx, [rsp + 0x90]; add rdx, imm32; mov [rbx - 8], rdx).
        {
            uint8_t added[0x40]{};
            const uint8_t bytes[] = {0x48, 0x8B, 0x94, 0x24, 0x90, 0x00, 0x00, 0x00, 0x48, 0x81, 0xC2, 0x3B,
                                     0x2A, 0x2D, 0x00, 0x48, 0x89, 0x53, 0xF8};
            std::memcpy(added + 0x10, bytes, sizeof(bytes));
            CHECK(GuestReturnBeforeCall(added, 0x30, 0x1000, 0x700000) == 0x2D2A3B);
        }
        struct Found { size_t stub; uint32_t returnRva; };
        std::vector<Found> found;
        ForEachStubCall(code, 0x200, sizeof(code), 0x1000, 0x700000,
                        [&](size_t stub, uint32_t returnRva) { found.push_back({stub, returnRva}); });
        CHECK(found.size() == 2);
        CHECK(found[0].stub == 0x200 && found[0].returnRva == 0x283786);
        CHECK(found[1].stub == 0x210 && found[1].returnRva == 0);
        // A stub cut off by the end of what was read is not one.
        found.clear();
        ForEachStubCall(code, 0x200, 0x218, 0x1000, 0x700000,
                        [&](size_t stub, uint32_t returnRva) { found.push_back({stub, returnRva}); });
        CHECK(found.size() == 1 && found[0].stub == 0x200);
    }
    // A version of the client's own that wraps the translated function loads its adapter from its data.
    {
        uint8_t function[0x40]{};
        const uint64_t at = 0x140001000, adapter = 0x24000010000, body = 0x24000010120;
        // mov rax, [rip + 0x100] at +0x10 (the address 0x140001117), and call [rip + 0x200] at +0x20 (0x140001226).
        const uint8_t load[] = {0x48, 0x8B, 0x05, 0x00, 0x01, 0x00, 0x00};
        const uint8_t call[] = {0xFF, 0x15, 0x00, 0x02, 0x00, 0x00};
        std::memcpy(function + 0x10, load, sizeof(load));
        std::memcpy(function + 0x20, call, sizeof(call));
        uint64_t atLoad = 0, atCall = 0;
        const auto read = [&](uint64_t address, uint64_t& value) {
            if (address == 0x140001117) { value = atLoad; return true; }
            if (address == 0x140001226) { value = atCall; return true; }
            return false;
        };
        CHECK(!CallsTranslatedFunction(function, sizeof(function), at, adapter, body, read));
        atLoad = adapter;
        CHECK(CallsTranslatedFunction(function, sizeof(function), at, adapter, body, read));
        atLoad = 0x12345;
        atCall = body;
        CHECK(CallsTranslatedFunction(function, sizeof(function), at, adapter, body, read));
        CHECK(!CallsTranslatedFunction(function, 0x20, at, adapter, body, read)); // the call lies past what is read
    }
    std::printf("lifted engine logic: all checks passed\n");
    return 0;
}
