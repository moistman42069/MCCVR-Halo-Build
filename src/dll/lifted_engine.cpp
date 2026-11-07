// Project Reclaimer 0.10's recompiled Halo 3 for the mod's Halo 3 code: lifted_engine.h says what and why,
// src/common/lifted_engine_logic.h the convention, lifted_engine_thunks.asm the register work.

#include "lifted_engine.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "../common/config.h"
#include "../common/lifted_engine_logic.h"
#include "../common/log.h"
#include "../common/reclaimer_host_logic.h"
#include "../common/title_build_identity.h"

extern "C" uint64_t LiftedCallNative(void* function, uint8_t* ctx, const uint64_t* stackArgs);
extern "C" uint64_t LiftedCallBody(void* body, uint8_t* ctx, uint64_t guestRsp, const uint64_t hostArgs[4]);
extern "C" void LiftedHookEntry();
extern "C" void LiftedReplacementEntry();
extern "C" void LiftedOrigEntry();
extern "C" void LiftedCallEntry();

namespace
{
// One hooked body, or one game function the mod calls. Never freed: a stub may still be running when a hook goes.
struct Record
{
    uintptr_t guestTarget = 0;  // the function's start in the guest image
    uint32_t rva = 0;
    uintptr_t body = 0;         // the translated body
    lifted_engine::Adapter adapter;
    void* detour = nullptr;     // hooks: the mod's detour
    void* trampoline = nullptr; // hooks: MinHook's way into the unhooked body; calls: the body
    void* entryStub = nullptr;  // hooks: what the body's first instruction jumps to; calls: what the mod calls
    void* origStub = nullptr;   // hooks: the detour's "original"
    uint32_t stackArgs = 0;     // stack arguments written back on the call to the original
    std::atomic<uint32_t> unrunnable{0};  // calls refused for want of a guest call on the thread
    uint32_t unrunnableReported = 0;
    // Hooks: how their game caller was found (GuestReturnAddress), for the report in Poll.
    std::atomic<uint32_t> callersNamed{0}, callersUnnamed{0}, callerDepth{0};
    std::atomic<uint64_t> unnamedEnteredFrom{0}, namedExample{0};
    bool callerReported = false;
    // Hooks: calls to the detour, for Poll's report of which hooks the translated game reaches.
    std::atomic<uint32_t> entered{0};
    uint32_t enteredReported = 0;
    // Hooks on a function the client replaced with a version of its own that never runs the translated body: the slot
    // of the stub its callers reach that version through, the version, and the entry the slot holds while the hook is
    // enabled (lifted_engine_logic.h, ForEachStubCall).
    volatile uint64_t* replacementSlot = nullptr;
    void* replacement = nullptr;
    void* replacementEntry = nullptr;
    // Hooks: which of the first four arguments are floating-point (bit n: the guest's xmm n), and whether the function
    // returns one in xmm0 (SetFloatArguments, SetFloatResult).
    uint32_t floatArgs = 0;
    bool floatResult = false;
};

// The guest's volatile registers in ctx (rcx, rdx, r8-r11, xmm0-xmm5): 0x10 + 0x20 + 0x60 bytes.
constexpr size_t kVolatileBytes = 0x90;

struct Frame
{
    uint8_t* ctx;
    uint64_t guestRsp;
    const Record* record;
    const uint64_t* hostReturnSlot; // hooks: where the call's return address sits on the host stack; null for the
                                    // mod's own calls
    bool viaReplacement;            // entered through the client's version: the guest return address is at guestRsp
    bool inOriginal;                // ... and that version is running now, as the detour's original
    bool originalRan;               // hooks: the original ran, and left volatilesAfter
    // Hooks: the guest's volatile registers as the caller left them, and as the original left them (FinishFrame).
    uint8_t volatilesEntry[kVolatileBytes];
    uint8_t volatilesAfter[kVolatileBytes];
};
constexpr int kMaxFrames = 64;
thread_local Frame t_frames[kMaxFrames];
thread_local int t_depth = 0;

// Below a hooked call's guest stack pointer, where a game function the mod calls gets its own frame.
constexpr uint64_t kCallGap = 0x2000;

Frame& PushFrame(uint8_t* ctx, uint64_t guestRsp, const Record* record, const uint64_t* hostReturnSlot,
                 bool viaReplacement)
{
    Frame& frame = t_frames[t_depth++];
    frame.ctx = ctx;
    frame.guestRsp = guestRsp;
    frame.record = record;
    frame.hostReturnSlot = hostReturnSlot;
    frame.viaReplacement = viaReplacement;
    frame.inOriginal = false;
    frame.originalRan = false;
    return frame;
}

void SaveVolatiles(const uint8_t* ctx, uint8_t* out)
{
    std::memcpy(out, ctx + 0x08, 0x10);
    std::memcpy(out + 0x10, ctx + 0x40, 0x20);
    std::memcpy(out + 0x30, ctx + 0x90, 0x60);
}
void RestoreVolatiles(uint8_t* ctx, const uint8_t* in)
{
    std::memcpy(ctx + 0x08, in, 0x10);
    std::memcpy(ctx + 0x40, in + 0x10, 0x20);
    std::memcpy(ctx + 0x90, in + 0x30, 0x60);
}

// halo3.dll is built with whole-program optimisation, and its translation keeps that: a caller may read guest registers
// the called function leaves alone, or the ones it leaves behind, right after the call (seen for the playback check
// 0xF01A0 and the weapon-IK mode setter 0x26DD98). So a hook is transparent to the guest's volatile registers: after
// the detour they are as the original left them (as the caller left them, when the detour did not run it), the
// detour's own xmm0 only for a function that returns a float.
void FinishFrame(const Record* record, uint8_t* ctx, const Frame& frame)
{
    uint8_t result[16];
    std::memcpy(result, ctx + lifted_engine::kCtxXmm0, sizeof(result)); // the detour's, as LiftedCallNative left it
    RestoreVolatiles(ctx, frame.originalRan ? frame.volatilesAfter : frame.volatilesEntry);
    if (record->floatResult)
        std::memcpy(ctx + lifted_engine::kCtxXmm0, result, sizeof(result));
}

SRWLOCK g_lock = SRWLOCK_INIT;
std::atomic<bool> g_active{false};
bool g_ruledOut = false;
ULONGLONG g_nextLookMs = 0;
uintptr_t g_guestBase = 0, g_guestCodeStart = 0, g_guestCodeEnd = 0;
size_t g_guestSize = 0;
uintptr_t g_code = 0;
lifted_engine::Table g_table;
std::unordered_map<uintptr_t, Record*> g_hooks;     // by guest target
std::unordered_map<uintptr_t, Record*> g_callables; // by guest target
std::unordered_map<uintptr_t, unsigned> g_stackArgs;
std::unordered_map<uintptr_t, uint32_t> g_floatArgs;
std::unordered_map<uintptr_t, bool> g_floatResults;
std::vector<Record*> g_allRecords;
// The stub slots of the client's own versions of guest functions, by guest RVA; built on the first hook.
std::unordered_map<uint32_t, volatile uint64_t*> g_replacements;
bool g_replacementsBuilt = false;
size_t g_codeReadable = 0; // how much from g_code is readable: the code and the stubs past it (BuildReplacements)
// The guest image's code section as halo3.dll has it: what signature searches read (sig::SetScanShadow).
std::vector<uint8_t> g_shadow;
std::atomic<bool> g_shadowReady{false};
uint8_t* g_stubPage = nullptr;
size_t g_stubUsed = 0, g_stubCapacity = 0;

constexpr size_t kStubSize = 32;

// mov r10, record ; jmp [rip] ; target
void* MakeStub(const Record* record, void (*target)())
{
    if (!g_stubPage || g_stubUsed + kStubSize > g_stubCapacity)
    {
        g_stubCapacity = 0x10000;
        g_stubPage = static_cast<uint8_t*>(
            VirtualAlloc(nullptr, g_stubCapacity, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        g_stubUsed = 0;
        if (!g_stubPage)
            return nullptr;
    }
    uint8_t* stub = g_stubPage + g_stubUsed;
    g_stubUsed += kStubSize;
    const uint64_t recordAddress = reinterpret_cast<uint64_t>(record);
    const uint64_t targetAddress = reinterpret_cast<uint64_t>(target);
    stub[0] = 0x49;
    stub[1] = 0xBA;
    std::memcpy(stub + 2, &recordAddress, 8);
    stub[10] = 0xFF;
    stub[11] = 0x25;
    std::memset(stub + 12, 0, 4);
    std::memcpy(stub + 16, &targetAddress, 8);
    FlushInstructionCache(GetCurrentProcess(), stub, kStubSize);
    return stub;
}

bool Readable(const void* address, size_t size)
{
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT)
        return false;
    const DWORD bad = PAGE_NOACCESS | PAGE_GUARD;
    if (info.Protect & bad)
        return false;
    const uintptr_t end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    return reinterpret_cast<uintptr_t>(address) + size <= end;
}

std::wstring EngineCodePath()
{
    wchar_t buffer[1024]{};
    if (GetEnvironmentVariableW(L"HALOMCCVR_RECLAIMER_ENGINE_CODE", buffer, 1024) && buffer[0])
        return buffer;
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, 1024))
        return {};
    return std::wstring(buffer) + L"\\ProjectReclaimer\\Cache\\native\\retail-engine\\engine.code";
}

bool LoadFile(const std::wstring& path, std::vector<uint8_t>& data)
{
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < (1ll << 31);
    if (ok)
    {
        data.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) && read == data.size();
    }
    CloseHandle(file);
    return ok;
}

// halo3.dll's image inside the client's own executable: a PE header with halo3.dll's timestamp and image size.
uintptr_t FindGuestImage()
{
    const title_build::Identity* known = title_build::Known(GameTitle::Halo3);
    if (!known)
        return 0;
    const auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(exe + dos->e_lfanew);
    const uintptr_t end = exe + nt->OptionalHeader.SizeOfImage;
    for (uintptr_t at = exe + 0x1000; at + 0x1000 <= end;)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof(info)))
            break;
        const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        const bool readable = info.State == MEM_COMMIT && !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD));
        for (; readable && at + 0x1000 <= regionEnd; at += 0x1000)
        {
            const auto* candidate = reinterpret_cast<const IMAGE_DOS_HEADER*>(at);
            if (candidate->e_magic != IMAGE_DOS_SIGNATURE || candidate->e_lfanew <= 0 ||
                candidate->e_lfanew > 0x800)
                continue;
            const auto* headers = reinterpret_cast<const IMAGE_NT_HEADERS64*>(at + candidate->e_lfanew);
            if (headers->Signature != IMAGE_NT_SIGNATURE ||
                headers->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
                headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
                continue;
            if (headers->FileHeader.TimeDateStamp == known->peTimestamp &&
                headers->OptionalHeader.SizeOfImage == known->sizeOfImage &&
                at + known->sizeOfImage <= end)
                return at;
        }
        at = (std::max)(at, regionEnd);
    }
    return 0;
}

// The translated code: a private executable block whose bytes at a sample of bodies are engine.code's.
uintptr_t FindCode(const std::vector<std::pair<uint64_t, std::vector<uint8_t>>>& samples, size_t codeLength)
{
    MEMORY_BASIC_INFORMATION info{};
    for (uintptr_t at = 0x10000; VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof(info));)
    {
        const uintptr_t base = reinterpret_cast<uintptr_t>(info.BaseAddress);
        const uintptr_t next = base + info.RegionSize;
        const DWORD executable = PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE;
        if (info.State == MEM_COMMIT && info.Type == MEM_PRIVATE && (info.Protect & executable) &&
            info.RegionSize >= codeLength)
        {
            // Nine in ten: a function the client itself patched must not hide the rest.
            size_t matched = 0;
            for (const auto& [offset, bytes] : samples)
                matched += std::memcmp(reinterpret_cast<const void*>(base + offset), bytes.data(), bytes.size()) == 0;
            if (samples.size() >= 32 && matched * 10 >= samples.size() * 9)
                return base;
        }
        if (next <= at)
            break;
        at = next;
    }
    return 0;
}

// halo3.dll on disk: the MCC folder the client uses (its mcc-folder.txt) + halo3\halo3.dll, or
// HALOMCCVR_RECLAIMER_HALO3_DLL.
std::wstring Halo3DllPath()
{
    wchar_t buffer[1024]{};
    if (GetEnvironmentVariableW(L"HALOMCCVR_RECLAIMER_HALO3_DLL", buffer, 1024) && buffer[0])
        return buffer;
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, 1024))
        return {};
    std::vector<uint8_t> text;
    if (!LoadFile(std::wstring(buffer) + L"\\ProjectReclaimer\\mcc-folder.txt", text) || text.empty())
        return {};
    std::string folder(text.begin(), text.end());
    if (folder.compare(0, 3, "\xEF\xBB\xBF") == 0)
        folder.erase(0, 3);
    while (!folder.empty() && (folder.back() == '\r' || folder.back() == '\n' || folder.back() == ' ' ||
                               folder.back() == '\\' || folder.back() == '/'))
        folder.pop_back();
    const int wide = MultiByteToWideChar(CP_UTF8, 0, folder.data(), static_cast<int>(folder.size()), nullptr, 0);
    std::wstring path(static_cast<size_t>(wide > 0 ? wide : 0), L'\0');
    if (wide > 0)
        MultiByteToWideChar(CP_UTF8, 0, folder.data(), static_cast<int>(folder.size()), path.data(), wide);
    return path.empty() ? std::wstring() : path + L"\\halo3\\halo3.dll";
}

// THE SCAN SHADOW. In the guest image the client has replaced the first five bytes of every function with a call to
// its trap just past the image (E8 rel32 to G + SizeOfImage; measured on 0.10.0: 22423 of them), and every signature
// that starts a function then fails. The copy has the image's code section with each such call put back from
// halo3.dll on disk (the same build: its timestamp and image size are checked against the image's header).
bool BuildShadow(uintptr_t guest, const IMAGE_NT_HEADERS64* nt, uint32_t textRva, uint32_t textSize,
                 std::wstring& why, size_t& restored)
{
    const std::wstring path = Halo3DllPath();
    std::vector<uint8_t> file;
    if (path.empty() || !LoadFile(path, file))
    {
        why = L"halo3.dll was not found (" + path + L")";
        return false;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    if (file.size() < 0x400 || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > file.size())
    {
        why = path + L" is not a PE file";
        return false;
    }
    const auto* fileNt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
    if (fileNt->FileHeader.TimeDateStamp != nt->FileHeader.TimeDateStamp ||
        fileNt->OptionalHeader.SizeOfImage != nt->OptionalHeader.SizeOfImage)
    {
        why = path + L" is another build than the client's image";
        return false;
    }
    const auto* section = IMAGE_FIRST_SECTION(fileNt);
    const IMAGE_SECTION_HEADER* text = nullptr;
    for (unsigned i = 0; i < fileNt->FileHeader.NumberOfSections; ++i, ++section)
        if (section->VirtualAddress == textRva)
            text = section;
    if (!text || static_cast<size_t>(text->PointerToRawData) + text->SizeOfRawData > file.size())
    {
        why = path + L" has no code section where the image has it";
        return false;
    }
    const uint8_t* raw = file.data() + text->PointerToRawData;
    const size_t rawSize = (std::min)(static_cast<size_t>(text->SizeOfRawData), static_cast<size_t>(textSize));
    std::vector<uint8_t> shadow(textSize);
    std::memcpy(shadow.data(), reinterpret_cast<const void*>(guest + textRva), textSize);
    const uint64_t trap = guest + nt->OptionalHeader.SizeOfImage;
    restored = 0;
    for (size_t i = 0; i + 5 <= rawSize;)
    {
        int32_t rel = 0;
        std::memcpy(&rel, shadow.data() + i + 1, 4);
        if (shadow[i] == 0xE8 && guest + textRva + i + 5 + static_cast<int64_t>(rel) == trap)
        {
            std::memcpy(shadow.data() + i, raw + i, 5);
            ++restored;
            i += 5;
        }
        else
            ++i;
    }
    g_shadow = std::move(shadow);
    return true;
}

bool InGuestCodeUnlocked(uintptr_t address)
{
    return g_guestCodeStart && address >= g_guestCodeStart && address < g_guestCodeEnd;
}

// A record for the translated function starting at `target`, or null (logged) when there is none to use.
Record* NewRecord(uintptr_t target, const char* what)
{
    const uint32_t rva = static_cast<uint32_t>(target - g_guestBase);
    const lifted_engine::Record* entry = lifted_engine::FindStart(g_table, rva);
    if (!entry || !lifted_engine::HasBody(*entry))
    {
        LOG("RECLAIMER 0.10: %s at halo3.dll+0x%X refused: not the start of a translated function (a hook inside a "
            "function, or a byte patch, has no effect on the translated code)", what, rva);
        return nullptr;
    }
    const uintptr_t block = g_code + entry->hostOffset;
    lifted_engine::Adapter adapter;
    if (!lifted_engine::DecodeAdapter(reinterpret_cast<const uint8_t*>(block), entry->bodyOffset, adapter))
    {
        LOG("RECLAIMER 0.10: %s at halo3.dll+0x%X refused: its translated adapter could not be read", what, rva);
        return nullptr;
    }
    auto* record = new Record;
    record->guestTarget = target;
    record->rva = rva;
    record->body = block + entry->bodyOffset;
    record->adapter = adapter;
    g_allRecords.push_back(record);
    return record;
}

// The function a guest `call rel32` returning to `returnRva` calls, read from halo3.dll's own bytes.
bool GuestCallTarget(uint32_t returnRva, uint32_t& target)
{
    const uintptr_t site = g_guestBase + returnRva - 5;
    if (site < g_guestCodeStart || site + 5 > g_guestCodeEnd)
        return false;
    uint8_t bytes[5];
    if (g_shadowReady.load(std::memory_order_acquire) && site + 5 - g_guestCodeStart <= g_shadow.size())
        std::memcpy(bytes, g_shadow.data() + (site - g_guestCodeStart), 5);
    else if (Readable(reinterpret_cast<const void*>(site), 5))
        std::memcpy(bytes, reinterpret_cast<const void*>(site), 5);
    else
        return false;
    if (bytes[0] != 0xE8)
        return false;
    int32_t rel = 0;
    std::memcpy(&rel, bytes + 1, 4);
    target = static_cast<uint32_t>(static_cast<int64_t>(returnRva) + rel);
    return true;
}

// Which guest functions the client replaced with versions of its own, from the stubs its translated code calls them
// through (lifted_engine_logic.h, ForEachStubCall). A stub named for two functions, or a function with two stubs, is
// left out.
void BuildReplacements()
{
    g_replacementsBuilt = true;
    const size_t limit = g_table.codeLength + 0x100000;
    size_t size = 0;
    for (uintptr_t at = g_code; at - g_code < limit;)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(info.Protect & 0xEE))
            break;
        at = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        size = (std::min)(static_cast<size_t>(at - g_code), limit);
    }
    g_codeReadable = size;
    if (size <= g_table.codeLength + lifted_engine::kStubSize)
    {
        LOG("RECLAIMER 0.10: nothing readable past the translated code: the client's own versions of game functions "
            "are not looked for");
        return;
    }
    constexpr uint32_t kAmbiguous = UINT32_MAX;
    std::unordered_map<size_t, uint32_t> stubFunction;
    size_t named = 0;
    lifted_engine::ForEachStubCall(
        reinterpret_cast<const uint8_t*>(g_code), g_table.codeLength, size,
        static_cast<uint32_t>(g_guestCodeStart - g_guestBase), static_cast<uint32_t>(g_guestCodeEnd - g_guestBase + 5),
        [&](size_t stub, uint32_t returnRva) {
            uint32_t target = 0;
            if (!returnRva || !GuestCallTarget(returnRva, target))
                return;
            ++named;
            const auto [found, inserted] = stubFunction.emplace(stub, target);
            if (!inserted && found->second != target)
                found->second = kAmbiguous;
        });
    for (const auto& [stub, rva] : stubFunction)
    {
        if (rva == kAmbiguous)
            continue;
        auto* slot = reinterpret_cast<volatile uint64_t*>(g_code + stub + 8);
        const auto [found, inserted] = g_replacements.emplace(rva, slot);
        if (!inserted && found->second != slot)
            found->second = nullptr;
    }
    LOG("RECLAIMER 0.10: the client calls %zu game functions' own versions of it through stubs past the translated "
        "code (%zu calls named the function)", g_replacements.size(), named);
}

// Gives a new hook the client's own version of its function, when its callers reach that instead of the translated
// body and it never runs the body itself (a version that does is a wrapper, and the hook on the body runs inside it).
void AttachReplacement(Record* record)
{
    if (!g_replacementsBuilt)
        BuildReplacements();
    const auto found = g_replacements.find(record->rva);
    if (found == g_replacements.end() || !found->second)
        return;
    volatile uint64_t* slot = found->second;
    void* version = reinterpret_cast<void*>(*slot);
    const auto* function = static_cast<const uint8_t*>(version);
    if (Readable(function, 5) && function[0] == 0xE9) // a jump table's entry
    {
        int32_t rel = 0;
        std::memcpy(&rel, function + 1, 4);
        function += 5 + static_cast<int64_t>(rel);
    }
    constexpr size_t kLook = 0xA0;
    if (!Readable(function, kLook))
        return;
    const lifted_engine::Record* entry = lifted_engine::FindStart(g_table, record->rva);
    const bool wraps = lifted_engine::CallsTranslatedFunction(
        function, kLook, reinterpret_cast<uint64_t>(function), g_code + entry->hostOffset, record->body,
        [](uint64_t address, uint64_t& value) {
            if (!Readable(reinterpret_cast<const void*>(address), 8))
                return false;
            std::memcpy(&value, reinterpret_cast<const void*>(address), 8);
            return true;
        });
    const auto exe = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
    if (wraps)
    {
        LOG("RECLAIMER 0.10: halo3.dll+0x%X: the client's own version of it (exe+0x%llX) runs the translated body, "
            "where the hook is", record->rva, static_cast<unsigned long long>(reinterpret_cast<uint64_t>(function) - exe));
        return;
    }
    record->replacementEntry = MakeStub(record, &LiftedReplacementEntry);
    if (!record->replacementEntry)
        return;
    record->replacementSlot = slot;
    record->replacement = version;
    LOG("RECLAIMER 0.10: halo3.dll+0x%X: its callers call the client's own version of it (exe+0x%llX), which never "
        "runs the translated body: the hook is placed on that call as well",
        record->rva, static_cast<unsigned long long>(reinterpret_cast<uint64_t>(function) - exe));
}

// Points the stub at the hook's entry (on) or back at the client's version.
void SetReplacement(Record* record, bool on)
{
    if (!record->replacementSlot)
        return;
    const auto value = static_cast<LONG64>(reinterpret_cast<uint64_t>(on ? record->replacementEntry : record->replacement));
    MEMORY_BASIC_INFORMATION info{};
    const bool writable = VirtualQuery(const_cast<uint64_t*>(record->replacementSlot), &info, sizeof(info)) &&
                          (info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE));
    DWORD old = 0;
    if (!writable && !::VirtualProtect(const_cast<uint64_t*>(record->replacementSlot), 8, PAGE_EXECUTE_READWRITE, &old))
        return;
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(record->replacementSlot), value);
    if (!writable)
        ::VirtualProtect(const_cast<uint64_t*>(record->replacementSlot), 8, old, &old);
}
} // namespace

// ---- the bridges ---------------------------------------------------------------------------------------------------

// Callers already paired with their guest call site (GuestReturnAddress), per host return address and hooked function.
// kNoPair marks one that could not be paired.
struct CallSite
{
    std::atomic<uint64_t> key{0};
    std::atomic<uint64_t> guestReturn{0};
};
constexpr size_t kCallSiteSlots = 512;
constexpr uint64_t kNoPair = 1;
CallSite g_callSites[kCallSiteSlots];
// How far up the host stack a caller is looked for when the client's own code called the hooked body for it.
constexpr int kCallerScanSlots = 4096;

// The record whose host block holds `address` in the translated code, or null.
const lifted_engine::Record* RecordOfHost(uint64_t address)
{
    if (address < g_code || address - g_code >= g_table.codeLength)
        return nullptr;
    const uint64_t offset = address - g_code;
    size_t lo = 0, hi = g_table.records.size();
    while (lo < hi)
    {
        const size_t mid = lo + (hi - lo) / 2;
        if (g_table.records[mid].hostOffset + g_table.records[mid].hostSize <= offset)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < g_table.records.size() && g_table.records[lo].hostOffset <= offset ? &g_table.records[lo] : nullptr;
}

// The guest return address of the call that returns to `hostReturn` in `caller`'s body, made to `hostTarget` (the
// hooked body, or the client's stub in front of it), paired by order with `caller`'s guest calls to `guestTarget`;
// kNoPair when the two sides do not pair. Remembered per call site.
uint64_t PairCallSite(uint64_t hostReturn, const lifted_engine::Record& caller, uint64_t hostTarget,
                      uint64_t guestTarget)
{
    const uint64_t key = hostReturn ^ (guestTarget << 1);
    const size_t first = static_cast<size_t>((key >> 4) * 0x9E3779B97F4A7C15ull >> 55) % kCallSiteSlots;
    CallSite* free = nullptr;
    for (size_t probe = 0; probe < 8; ++probe)
    {
        CallSite& site = g_callSites[(first + probe) % kCallSiteSlots];
        const uint64_t held = site.key.load(std::memory_order_acquire);
        if (held == key)
        {
            const uint64_t known = site.guestReturn.load(std::memory_order_acquire);
            if (known)
                return known;
            break;
        }
        if (!held && !free)
            free = &site;
    }
    uint64_t paired = lifted_engine::GuestReturnOfCall(
        reinterpret_cast<const uint8_t*>(g_code + caller.hostOffset), caller.hostSize, g_code + caller.hostOffset,
        hostReturn - 5, hostTarget, reinterpret_cast<const uint8_t*>(g_guestBase + caller.guestRva), caller.guestSize,
        g_guestBase + caller.guestRva, guestTarget);
    if (!paired)
        paired = kNoPair;
    uint64_t expected = 0;
    if (free && free->key.compare_exchange_strong(expected, key, std::memory_order_acq_rel))
        free->guestReturn.store(paired, std::memory_order_release);
    return paired;
}

extern "C" uint64_t LiftedDispatch(Record* record, uint8_t* ctx, uint64_t guestRsp, const uint64_t hostArgs[4],
                                   const uint64_t* hostReturnSlot)
{
    // Too deep to track, or the client's own version of this function, run as this hook's original, running the body
    // itself: the original alone.
    if (t_depth >= kMaxFrames || (t_depth && t_frames[t_depth - 1].record == record && t_frames[t_depth - 1].inOriginal))
        return LiftedCallBody(record->trampoline, ctx, guestRsp, hostArgs);
    record->entered.fetch_add(1, std::memory_order_relaxed);
    Frame& frame = PushFrame(ctx, guestRsp, record, hostReturnSlot, false);
    SaveVolatiles(ctx, frame.volatilesEntry); // as the caller left them, before the arguments are written in
    lifted_engine::CtxFromHostArgs(record->adapter, hostArgs, ctx);
    uint64_t stack[8];
    std::memcpy(stack, reinterpret_cast<const void*>(guestRsp + 0x28), sizeof(stack));
    const uint64_t result = LiftedCallNative(record->detour, ctx, stack);
    FinishFrame(record, ctx, frame);
    --t_depth;
    return result;
}

namespace
{
// Runs the client's own version of a hooked function on `ctx`, its stack pointer back at the guest return address.
uint64_t CallReplacement(const Record* record, uint8_t* ctx, uint64_t guestRsp)
{
    *reinterpret_cast<uint64_t*>(ctx + lifted_engine::kCtxRsp) = guestRsp;
    reinterpret_cast<void(__fastcall*)(uint8_t*)>(record->replacement)(ctx);
    return *reinterpret_cast<const uint64_t*>(ctx + lifted_engine::kCtxRax);
}
} // namespace

// The stub of a function the client replaced, while its hook is enabled (LiftedReplacementEntry): the call that would
// have run the client's version runs the detour, whose original is that version.
extern "C" uint64_t LiftedReplacementDispatch(Record* record, uint8_t* ctx, const uint64_t* hostReturnSlot)
{
    const uint64_t guestRsp = *reinterpret_cast<const uint64_t*>(ctx + lifted_engine::kCtxRsp);
    uint64_t result = 0;
    if (t_depth >= kMaxFrames)
        result = CallReplacement(record, ctx, guestRsp);
    else
    {
        record->entered.fetch_add(1, std::memory_order_relaxed);
        Frame& frame = PushFrame(ctx, guestRsp, record, hostReturnSlot, true);
        SaveVolatiles(ctx, frame.volatilesEntry);
        uint64_t stack[8];
        std::memcpy(stack, reinterpret_cast<const void*>(guestRsp + 0x28), sizeof(stack));
        result = LiftedCallNative(record->detour, ctx, stack);
        FinishFrame(record, ctx, frame);
        --t_depth;
        *reinterpret_cast<uint64_t*>(ctx + lifted_engine::kCtxRax) = result;
    }
    // As the client's version leaves it, whether the detour ran that version or not: the guest return address popped.
    *reinterpret_cast<uint64_t*>(ctx + lifted_engine::kCtxRsp) = guestRsp + 8;
    return result;
}

namespace
{
// The block the Windows-side entries fill: rcx, rdx, r8, r9, then xmm0-xmm3. Every one of them, for a game function the
// mod calls (its signature is not known here).
void ArgsToCtx(const uint8_t* block, uint8_t* ctx)
{
    for (int i = 0; i < 4; ++i)
        std::memcpy(ctx + lifted_engine::kGuestArgOffsets[i], block + 8 * i, 8);
    std::memcpy(ctx + lifted_engine::kCtxXmm0, block + 0x20, 0x40);
}

// Only the hooked function's own arguments: the integer ones its body takes (its adapter), the floating-point ones it
// was declared to take. The rest of the guest's registers stay as the caller left them (FinishFrame).
void HookArgsToCtx(const Record& record, const uint8_t* block, uint8_t* ctx)
{
    for (int i = 0; i < 4; ++i)
    {
        if (record.floatArgs & (1u << i))
            std::memcpy(ctx + lifted_engine::kCtxXmm0 + lifted_engine::kCtxXmmStride * i, block + 0x20 + 0x10 * i,
                        0x10);
        else if (record.adapter.hostRegOfGuestArg[i] != lifted_engine::kNoReg)
            std::memcpy(ctx + lifted_engine::kGuestArgOffsets[i], block + 8 * i, 8);
    }
}
} // namespace

extern "C" uint64_t LiftedCallGame(Record* record, uint8_t* block, const uint64_t* callerStack);

extern "C" uint64_t LiftedCallOriginal(Record* record, uint8_t* block, const uint64_t* callerStack)
{
    int at = t_depth - 1;
    while (at >= 0 && t_frames[at].record != record)
        --at;
    // Not inside its own hook: the mod's own query of a hooked function (the aim trace's collision_test_vector, from
    // inside the unit update's hook). It runs as any game function the mod calls, as MinHook's trampoline would.
    if (at < 0)
        return LiftedCallGame(record, block, callerStack);
    Frame& frame = t_frames[at];
    RestoreVolatiles(frame.ctx, frame.volatilesEntry); // as the caller left them...
    HookArgsToCtx(*record, block, frame.ctx);          // ...with the detour's arguments
    if (record->stackArgs)
        std::memcpy(reinterpret_cast<void*>(frame.guestRsp + 0x28), callerStack,
                    8 * (std::min)(record->stackArgs, 8u));
    uint64_t result = 0;
    if (frame.viaReplacement)
    {
        frame.inOriginal = true;
        result = CallReplacement(record, frame.ctx, frame.guestRsp);
        frame.inOriginal = false;
    }
    else
    {
        uint64_t host[4]{};
        lifted_engine::HostArgsFromCtx(record->adapter, frame.ctx, host);
        result = LiftedCallBody(record->trampoline, frame.ctx, frame.guestRsp, host);
    }
    frame.originalRan = true;
    SaveVolatiles(frame.ctx, frame.volatilesAfter);
    std::memcpy(block + 0x20, frame.ctx + lifted_engine::kCtxXmm0, 16);
    return result;
}

extern "C" uint64_t LiftedCallGame(Record* record, uint8_t* block, const uint64_t* callerStack)
{
    if (!record->trampoline || !t_depth || t_depth >= kMaxFrames)
    {
        record->unrunnable.fetch_add(1, std::memory_order_relaxed);
        std::memset(block + 0x20, 0, 16);
        return 0;
    }
    const Frame& top = t_frames[t_depth - 1];
    // A frame of its own below the innermost call's: 16-aligned before the call, the return slot 8 below.
    const uint64_t guestRsp = ((top.guestRsp - kCallGap) & ~uint64_t{0xF}) - 8;
    *reinterpret_cast<uint64_t*>(guestRsp) = 0;
    std::memcpy(reinterpret_cast<void*>(guestRsp + 0x28), callerStack, 8 * 8);
    ArgsToCtx(block, top.ctx);
    uint64_t host[4]{};
    lifted_engine::HostArgsFromCtx(record->adapter, top.ctx, host);
    uint8_t* const ctx = top.ctx;
    PushFrame(ctx, guestRsp, record, nullptr, false);
    const uint64_t result = LiftedCallBody(record->trampoline, ctx, guestRsp, host);
    --t_depth;
    std::memcpy(block + 0x20, ctx + lifted_engine::kCtxXmm0, 16);
    return result;
}

// ---- the API -------------------------------------------------------------------------------------------------------

namespace lifted
{
bool Active()
{
    return g_active.load(std::memory_order_acquire);
}

HMODULE Halo3Module()
{
    return Active() ? reinterpret_cast<HMODULE>(g_guestBase) : nullptr;
}

bool InGuestCode(uintptr_t address)
{
    return Active() && InGuestCodeUnlocked(address);
}

bool ScanShadow(uintptr_t& start, size_t& size, const uint8_t*& copy)
{
    if (!g_shadowReady.load(std::memory_order_acquire))
        return false;
    start = g_guestCodeStart;
    size = g_shadow.size();
    copy = g_shadow.data();
    return true;
}

bool ModuleRangeFallback(const wchar_t* moduleName, uintptr_t& base, size_t& size)
{
    if (!Active() || !moduleName || _wcsicmp(moduleName, L"halo3.dll") != 0)
        return false;
    base = g_guestBase;
    size = g_guestSize;
    return true;
}

bool Poll()
{
    if (Active())
    {
        // Calls the mod could not run (no hooked game call on their thread), reported here, off the game's threads.
        AcquireSRWLockExclusive(&g_lock);
        const auto exe = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
        for (Record* record : g_allRecords)
        {
            const uint32_t named = record->callersNamed.load(std::memory_order_relaxed);
            const uint32_t unnamed = record->callersUnnamed.load(std::memory_order_relaxed);
            if (!record->callerReported && named + unnamed >= 100)
            {
                record->callerReported = true;
                const uint64_t from = record->unnamedEnteredFrom.load(std::memory_order_relaxed);
                const uint64_t example = record->namedExample.load(std::memory_order_relaxed);
                LOG("RECLAIMER 0.10: hook halo3.dll+0x%X: of its first %u calls, %u named their game caller (e.g. "
                    "+0x%llX, found %u stack slots up) and %u did not%s%s+0x%llX)", record->rva, named + unnamed,
                    named, static_cast<unsigned long long>(example ? example - g_guestBase : 0),
                    record->callerDepth.load(std::memory_order_relaxed), unnamed,
                    unnamed ? " (the first entered from " : "",
                    !unnamed ? "" : from >= g_code && from - g_code < g_table.codeLength ? "translated code" : "exe",
                    static_cast<unsigned long long>(!unnamed ? 0 : from >= g_code && from - g_code < g_table.codeLength
                                                                       ? from - g_code : from - exe));
            }
            const uint32_t count = record->unrunnable.load(std::memory_order_relaxed);
            if (count != record->unrunnableReported)
            {
                LOG("RECLAIMER 0.10: halo3.dll+0x%X was called %u time(s) and NOT run (it returned 0): %s",
                    record->rva, count - record->unrunnableReported,
                    record->trampoline ? "outside a hooked game call on its thread"
                                       : "it is not the start of a translated function");
                record->unrunnableReported = count;
            }
        }
        // Every 30 s: how often each hook's detour ran, and the hooks the translated game never reached.
        static ULONGLONG nextCallReportMs = 0;
        const ULONGLONG nowMs = GetTickCount64();
        if (!g_hooks.empty() && nowMs >= nextCallReportMs)
        {
            const bool first = !nextCallReportMs;
            nextCallReportMs = nowMs + 30000;
            std::vector<Record*> hooks;
            for (const auto& entry : g_hooks)
                hooks.push_back(entry.second);
            std::sort(hooks.begin(), hooks.end(), [](const Record* a, const Record* b) { return a->rva < b->rva; });
            std::string called, idle;
            char item[48];
            for (Record* record : hooks)
            {
                const uint32_t total = record->entered.load(std::memory_order_relaxed);
                const uint32_t calls = total - record->enteredReported;
                record->enteredReported = total;
                std::snprintf(item, sizeof(item), calls ? " +0x%X %u" : " +0x%X", record->rva, calls);
                (calls ? called : idle) += item;
            }
            if (!first)
                LOG("RECLAIMER 0.10: hook calls in the last 30 s:%s; not called:%s", called.empty() ? " none" : called.c_str(),
                    idle.empty() ? " none" : idle.c_str());
        }
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    }
    if (g_ruledOut || !g_hostIsProjectReclaimer)
        return false;
    const ULONGLONG now = GetTickCount64();
    if (now < g_nextLookMs)
        return false;
    g_nextLookMs = now + 1000;

    const uintptr_t guest = FindGuestImage();
    if (!guest)
        return false; // not linked yet (or a client that still loads halo3.dll)

    AcquireSRWLockExclusive(&g_lock);
    const std::wstring path = EngineCodePath();
    std::vector<uint8_t> file;
    lifted_engine::Table table;
    lifted_engine::ParseResult parsed = lifted_engine::ParseResult::NotEngineCode;
    if (!LoadFile(path, file) ||
        (parsed = lifted_engine::Parse(file.data(), file.size(), table)) != lifted_engine::ParseResult::Ok)
    {
        LOG("RECLAIMER 0.10: halo3.dll's image is at %p in the client, but its translated code's table %ls is %s; "
            "Halo 3 stays flat", reinterpret_cast<void*>(guest), path.c_str(),
            file.empty() ? "missing or unreadable" : lifted_engine::ParseResultName(parsed));
        g_ruledOut = true;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    // 16 bytes from each of 64 bodies spread over the table, where they hold no relocation (the client fills those
    // in, so they differ from the file; a body's first bytes can be one).
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> samples;
    for (size_t i = 0; i < 64; ++i)
    {
        const lifted_engine::Record& r = table.records[i * (table.records.size() - 1) / 63];
        if (!lifted_engine::HasBody(r))
            continue;
        const uint8_t* body = file.data() + table.codeOffset + r.hostOffset + r.bodyOffset;
        size_t window = 0;
        if (lifted_engine::RelocationFreeWindow(body, r.hostSize - r.bodyOffset, 16, window))
            samples.emplace_back(r.hostOffset + r.bodyOffset + window,
                                 std::vector<uint8_t>(body + window, body + window + 16));
    }
    const uintptr_t code = FindCode(samples, table.codeLength);
    if (!code)
    {
        static bool reported = false;
        if (!reported)
            LOG("RECLAIMER 0.10: halo3.dll's image is at %p, but no translated code matching %ls is in memory yet "
                "(looked for every second; said once)", reinterpret_cast<void*>(guest), path.c_str());
        reported = true;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(guest);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(guest + dos->e_lfanew);
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
        if (std::memcmp(section->Name, ".text", 6) == 0)
        {
            g_guestCodeStart = guest + section->VirtualAddress;
            g_guestCodeEnd = g_guestCodeStart + section->Misc.VirtualSize;
        }
    std::wstring shadowWhy;
    size_t restored = 0;
    const bool shadowed = g_guestCodeStart &&
        BuildShadow(guest, nt, static_cast<uint32_t>(g_guestCodeStart - guest),
                    static_cast<uint32_t>(g_guestCodeEnd - g_guestCodeStart), shadowWhy, restored);
    if (shadowed)
        g_shadowReady.store(true, std::memory_order_release);
    g_guestBase = guest;
    g_guestSize = nt->OptionalHeader.SizeOfImage;
    g_code = code;
    g_table = std::move(table);
    g_active.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_lock);
    if (shadowed)
        LOG("RECLAIMER 0.10: the client has put a call to its trap over the start of %zu functions in halo3.dll's "
            "image; signature searches read a copy with halo3.dll's own bytes there", restored);
    else
        LOG("RECLAIMER 0.10: signature searches read the image as the client left it (%ls): every signature that "
            "starts a function will fail", shadowWhy.c_str());
    LOG("RECLAIMER 0.10: the client runs a recompiled Halo 3: halo3.dll's image at %p (code section %p-%p, none of "
        "it runs), its %zu translated functions at %p (table %ls). The mod's Halo 3 hooks go on the translated "
        "functions", reinterpret_cast<void*>(guest), reinterpret_cast<void*>(g_guestCodeStart),
        reinterpret_cast<void*>(g_guestCodeEnd), g_table.records.size(), reinterpret_cast<void*>(code), path.c_str());
    return true;
}

MH_STATUS CreateHook(LPVOID target, LPVOID detour, LPVOID* original)
{
    const auto address = reinterpret_cast<uintptr_t>(target);
    if (!InGuestCode(address))
        return MH_CreateHook(target, detour, original);
    AcquireSRWLockExclusive(&g_lock);
    if (g_hooks.count(address))
    {
        ReleaseSRWLockExclusive(&g_lock);
        return MH_ERROR_ALREADY_CREATED;
    }
    Record* record = NewRecord(address, "a hook");
    MH_STATUS status = MH_ERROR_UNSUPPORTED_FUNCTION;
    if (record)
    {
        record->detour = detour;
        const auto args = g_stackArgs.find(address);
        record->stackArgs = args == g_stackArgs.end() ? 0 : args->second;
        const auto floats = g_floatArgs.find(address);
        record->floatArgs = floats == g_floatArgs.end() ? 0 : floats->second;
        record->floatResult = g_floatResults.count(address) != 0;
        record->entryStub = MakeStub(record, &LiftedHookEntry);
        record->origStub = MakeStub(record, &LiftedOrigEntry);
        status = record->entryStub && record->origStub
                     ? MH_CreateHook(reinterpret_cast<LPVOID>(record->body), record->entryStub, &record->trampoline)
                     : MH_ERROR_MEMORY_ALLOC;
        if (status == MH_OK)
        {
            g_hooks[address] = record;
            if (original)
                *original = record->origStub;
        }
    }
    if (status == MH_OK)
        LOG("RECLAIMER 0.10: hook for halo3.dll+0x%X placed on its translated body %p", record->rva,
            reinterpret_cast<void*>(record->body));
    if (status == MH_OK)
        AttachReplacement(record);
    ReleaseSRWLockExclusive(&g_lock);
    return status;
}

LPVOID PatchedAddress(LPVOID target)
{
    const auto address = reinterpret_cast<uintptr_t>(target);
    if (!InGuestCode(address))
        return target;
    AcquireSRWLockShared(&g_lock);
    const auto found = g_hooks.find(address);
    const LPVOID body = found == g_hooks.end() ? target : reinterpret_cast<LPVOID>(found->second->body);
    ReleaseSRWLockShared(&g_lock);
    return body;
}

// The stubs of the hooks on functions the client replaced follow their hooks' state.
static void SetReplacements(LPVOID target, bool on)
{
    if (!Active() || (target != MH_ALL_HOOKS && !InGuestCode(reinterpret_cast<uintptr_t>(target))))
        return;
    AcquireSRWLockShared(&g_lock);
    if (target == MH_ALL_HOOKS)
    {
        for (const auto& entry : g_hooks)
            SetReplacement(entry.second, on);
    }
    else if (const auto found = g_hooks.find(reinterpret_cast<uintptr_t>(target)); found != g_hooks.end())
        SetReplacement(found->second, on);
    ReleaseSRWLockShared(&g_lock);
}

MH_STATUS AfterEnable(LPVOID target, MH_STATUS status)
{
    if (status == MH_OK || status == MH_ERROR_ENABLED)
        SetReplacements(target, true);
    return status;
}
MH_STATUS AfterDisable(LPVOID target, MH_STATUS status)
{
    if (status == MH_OK || status == MH_ERROR_DISABLED)
        SetReplacements(target, false);
    return status;
}

MH_STATUS EnableHook(LPVOID target)
{
    return AfterEnable(target, MH_EnableHook(target == MH_ALL_HOOKS ? target : PatchedAddress(target)));
}
MH_STATUS DisableHook(LPVOID target)
{
    return AfterDisable(target, MH_DisableHook(target == MH_ALL_HOOKS ? target : PatchedAddress(target)));
}
MH_STATUS QueueEnableHook(LPVOID target)
{
    return AfterEnable(target, MH_QueueEnableHook(target == MH_ALL_HOOKS ? target : PatchedAddress(target)));
}
MH_STATUS QueueDisableHook(LPVOID target)
{
    return AfterDisable(target, MH_QueueDisableHook(target == MH_ALL_HOOKS ? target : PatchedAddress(target)));
}
MH_STATUS RemoveHook(LPVOID target)
{
    const auto address = reinterpret_cast<uintptr_t>(target);
    if (!InGuestCode(address))
        return MH_RemoveHook(target);
    AcquireSRWLockExclusive(&g_lock);
    const auto found = g_hooks.find(address);
    MH_STATUS status = MH_ERROR_NOT_CREATED;
    if (found != g_hooks.end())
    {
        SetReplacement(found->second, false);
        status = MH_RemoveHook(reinterpret_cast<LPVOID>(found->second->body));
        if (status == MH_OK)
            g_hooks.erase(found); // the record stays: a stub may still be running
    }
    ReleaseSRWLockExclusive(&g_lock);
    return status;
}

void SetFloatArguments(LPVOID target, uint32_t mask)
{
    AcquireSRWLockExclusive(&g_lock);
    g_floatArgs[reinterpret_cast<uintptr_t>(target)] = mask & 0xF;
    ReleaseSRWLockExclusive(&g_lock);
}

void SetFloatResult(LPVOID target)
{
    AcquireSRWLockExclusive(&g_lock);
    g_floatResults[reinterpret_cast<uintptr_t>(target)] = true;
    ReleaseSRWLockExclusive(&g_lock);
}

void SetStackArguments(LPVOID target, unsigned count)
{
    AcquireSRWLockExclusive(&g_lock);
    g_stackArgs[reinterpret_cast<uintptr_t>(target)] = (std::min)(count, 8u);
    ReleaseSRWLockExclusive(&g_lock);
}

void* Callable(void* target)
{
    const auto address = reinterpret_cast<uintptr_t>(target);
    if (!InGuestCode(address))
        return target;
    AcquireSRWLockExclusive(&g_lock);
    void* result = nullptr;
    const auto found = g_callables.find(address);
    if (found != g_callables.end())
        result = found->second->entryStub;
    else
    {
        // A function that cannot be called still gets an entry: it returns 0, and Poll's report names it.
        Record* record = NewRecord(address, "a call");
        if (!record)
        {
            record = new Record;
            record->guestTarget = address;
            record->rva = static_cast<uint32_t>(address - g_guestBase);
            g_allRecords.push_back(record);
        }
        else
            record->trampoline = reinterpret_cast<void*>(record->body);
        record->entryStub = MakeStub(record, &LiftedCallEntry);
        if (record->entryStub)
        {
            g_callables[address] = record;
            result = record->entryStub;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

BOOL GuardedVirtualProtect(LPVOID address, SIZE_T size, DWORD protect, PDWORD previous)
{
    const auto at = reinterpret_cast<uintptr_t>(address);
    if (!InGuestCode(at))
        return ::VirtualProtect(address, size, protect, previous);
    static std::unordered_map<uintptr_t, bool> reported;
    AcquireSRWLockExclusive(&g_lock);
    const bool first = !reported[at];
    reported[at] = true;
    ReleaseSRWLockExclusive(&g_lock);
    if (first)
        LOG("RECLAIMER 0.10: a code patch at halo3.dll+0x%llX (%zu bytes) refused: halo3.dll's own code never runs "
            "here, only its translated copy; what the patch was for stays stock",
            static_cast<unsigned long long>(at - g_guestBase), static_cast<size_t>(size));
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

PRUNTIME_FUNCTION LookupFunctionEntry(DWORD64 address, PDWORD64 imageBase, PUNWIND_HISTORY_TABLE history)
{
    if (!InGuestCode(static_cast<uintptr_t>(address)))
        return RtlLookupFunctionEntry(address, imageBase, history);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_guestBase);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(g_guestBase + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!directory.VirtualAddress || directory.Size < sizeof(RUNTIME_FUNCTION) ||
        directory.VirtualAddress + directory.Size > g_guestSize)
        return nullptr;
    auto* table = reinterpret_cast<RUNTIME_FUNCTION*>(g_guestBase + directory.VirtualAddress);
    const size_t count = directory.Size / sizeof(RUNTIME_FUNCTION);
    const auto rva = static_cast<DWORD>(address - g_guestBase);
    size_t lo = 0, hi = count;
    while (lo < hi)
    {
        const size_t mid = lo + (hi - lo) / 2;
        if (table[mid].EndAddress <= rva)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == count || table[lo].BeginAddress > rva)
        return nullptr;
    if (imageBase)
        *imageBase = g_guestBase;
    return &table[lo];
}

void** GuestTlsSlots()
{
    if (!t_depth)
        return nullptr;
    return *reinterpret_cast<void***>(t_frames[t_depth - 1].ctx + 0x2D8);
}

// The caller found `depth` slots above the return slot, or 0: a return address in a translated body that follows a
// call and pairs (PairCallSite). `first` is set to the first such caller's function start.
static uint64_t CallerAt(const Frame& frame, const uint64_t* at, uintptr_t& first)
{
    const uint64_t value = *at;
    const lifted_engine::Record* caller = RecordOfHost(value);
    if (!caller || value - 5 < g_code + caller->hostOffset + caller->bodyOffset)
        return 0;
    const auto* call = reinterpret_cast<const uint8_t*>(value - 5);
    if (*call != 0xE8)
        return 0;
    int32_t rel = 0;
    std::memcpy(&rel, call + 1, 4);
    const uint64_t target = value + static_cast<int64_t>(rel);
    // A call through a stub past the code, to the client's own version of a function that ran the hooked body: the
    // guest return address the caller formed for that call (lifted_engine_logic.h, ForEachStubCall).
    if (target >= g_code + g_table.codeLength && target + lifted_engine::kStubSize <= g_code + g_codeReadable &&
        lifted_engine::IsStub(reinterpret_cast<const uint8_t*>(target)))
    {
        if (const uint32_t returnRva = lifted_engine::GuestReturnBeforeCall(
                reinterpret_cast<const uint8_t*>(g_code), value - 5 - g_code,
                static_cast<uint32_t>(g_guestCodeStart - g_guestBase), static_cast<uint32_t>(g_guestCodeEnd - g_guestBase + 5)))
            return g_guestBase + returnRva;
    }
    const uint64_t paired = PairCallSite(value, *caller, target, frame.record->guestTarget);
    if (paired != kNoPair)
        return paired;
    if (!first)
        first = g_guestBase + caller->guestRva;
    return 0;
}

uintptr_t GuestReturnAddress()
{
    if (!t_depth)
        return 0;
    const Frame& frame = t_frames[t_depth - 1];
    if (frame.viaReplacement)
        return *reinterpret_cast<const uintptr_t*>(frame.guestRsp); // the caller stored it, as real hardware does
    if (!frame.hostReturnSlot)
        return 0; // a call the mod made itself
    auto* record = const_cast<Record*>(frame.record);
    // Entered through the hooked function's own adapter: an indirect call's dispatcher, which stores the guest return
    // address on the guest stack, as real hardware does. The client's own version of a function that runs the body
    // (a wrapper) gives it a frame of its own with 0 there: its caller is found up the host stack, as below.
    const uint64_t returned = *frame.hostReturnSlot;
    if (const lifted_engine::Record* own = RecordOfHost(returned);
        own && own->guestRva == record->rva && returned - g_code < own->hostOffset + own->bodyOffset)
        if (const uintptr_t onStack = *reinterpret_cast<const uintptr_t*>(frame.guestRsp))
            return onStack;
    // The caller's translated code: the return address itself for a direct call; when the client's own code called
    // the body for it (it intercepts some of the game's input functions through stubs past the code), the first
    // return address further up the host stack that follows a call in a translated body and pairs. The depth that
    // worked last is tried first.
    const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const auto* stackEnd = reinterpret_cast<const uint64_t*>(tib->StackBase);
    uintptr_t callerStart = 0;
    const uint32_t hint = record->callerDepth.load(std::memory_order_relaxed);
    if (hint && frame.hostReturnSlot + hint < stackEnd)
        if (const uint64_t paired = CallerAt(frame, frame.hostReturnSlot + hint, callerStart))
        {
            record->callersNamed.fetch_add(1, std::memory_order_relaxed);
            return paired;
        }
    const uint64_t* at = frame.hostReturnSlot;
    for (uint32_t n = 0; n < kCallerScanSlots && at < stackEnd; ++n, ++at)
    {
        if (const uint64_t paired = CallerAt(frame, at, callerStart))
        {
            record->callerDepth.store(n, std::memory_order_relaxed);
            record->callersNamed.fetch_add(1, std::memory_order_relaxed);
            uint64_t none = 0;
            record->namedExample.compare_exchange_strong(none, paired, std::memory_order_relaxed);
            return paired;
        }
    }
    record->callersUnnamed.fetch_add(1, std::memory_order_relaxed);
    uint64_t none = 0;
    record->unnamedEnteredFrom.compare_exchange_strong(none, returned, std::memory_order_relaxed);
    return callerStart;
}
} // namespace lifted
