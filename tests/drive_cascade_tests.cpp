// The DLL's side of the direct drives' address resolution (src/dll/drive_cascade.cpp), as shipped, run on a module
// that is really loaded: this test's own executable. What runs here that the synthetic-image tests cannot reach:
// the module is opened through VirtualQuery and its own headers, its .pdata is read where the loader mapped it
// and compared with what the operating system answers for the same addresses, its TLS index is taken from its PE
// TLS directory, a pattern is found through one of "this mod's hooks" (a patch reported by the hook-patch
// source), the address cache's file is really written, read back, replaced, refused, found damaged on disk and
// left alone when it could not be read, the one line that says which rung won is really formatted, the proofs an
// install makes after its plan are run on a module whose unwind info cannot be read (they must fail, not fault),
// a module whose function table cannot be read is opened all the same, a drive installed a second time in the
// same module is verified from memory with no search and no file -- or, when its first search did not find all
// of it, searched for again a bounded number of times -- and the hook watchdog is stepped as the cold worker
// steps it. It maps no game code and opens no game file.
//
// Each resolution below stands for a launch of its own unless it says otherwise: the helpers forget what the
// process resolved before (drive_cascade::ForgetResolutions), as a new process has nothing to remember.
//
// Built twice by CMake: as a player build compiles the file (no fault injection: the dev key must do nothing) and
// as a dev build does (HALOMCCVR_DEV_DIAGNOSTICS=1, ADDRCASCADE_FAULTS=1: each forced fault must show).
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/common/config.h"
#include "../src/common/title_build_identity.h"
#include "../src/dll/drive_cascade.h"
#include "../src/dll/drive_install.h"
#include "../src/dll/sigscan.h"

#ifndef HALOMCCVR_DEV_DIAGNOSTICS
#define HALOMCCVR_DEV_DIAGNOSTICS 0
#endif

namespace
{
unsigned checks{}, failures{};
void Check(bool ok, const char* what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

std::vector<std::string> lines;
bool printLines{};
size_t Lines(const char* text)
{
    size_t n = 0;
    for (const std::string& line : lines)
        n += line.find(text) != std::string::npos ? 1u : 0u;
    return n;
}

// How many lines hold `before`, then a number with two decimals -- what a resolution took, in milliseconds, which
// no test can know beforehand -- and then `after`.
size_t Said(const char* before, const char* after)
{
    size_t n = 0;
    for (const std::string& line : lines)
    {
        const size_t at = line.find(before);
        if (at == std::string::npos)
            continue;
        size_t i = at + std::strlen(before), digits = 0;
        for (; i < line.size() && line[i] >= '0' && line[i] <= '9'; ++i)
            ++digits;
        const auto digit = [&line](size_t k) { return k < line.size() && line[k] >= '0' && line[k] <= '9'; };
        if (!digits || i >= line.size() || line[i] != '.' || !digit(i + 1) || !digit(i + 2))
            continue;
        n += line.compare(i + 3, std::strlen(after), after) == 0 ? 1u : 0u;
    }
    return n;
}
} // namespace

void Logf(const char* format, ...)
{
    char text[2048];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    lines.emplace_back(text);
    if (printLines)
        std::printf("    | %s\n", text);
}

// Forty bytes in a section of their own that the image marks executable, so the search covers them. No
// instruction stream reads like them, and nothing else in the image holds them.
#pragma section(".drvmk", read, execute)
__declspec(allocate(".drvmk")) extern const uint8_t g_marker[40] = {
    0x4D, 0xC3, 0x56, 0x52, 0xD1, 0x7E, 0x0B, 0xA5, 0x93, 0x1C, 0xE8, 0x47, 0x2F, 0x60, 0xBD, 0x19,
    0x84, 0xF2, 0x3A, 0x75, 0xC9, 0x0E, 0x51, 0xAC, 0x66, 0xDB, 0x12, 0x9F, 0x38, 0xE1, 0x7A, 0x05,
    0xB4, 0x2D, 0x91, 0x6C, 0xF7, 0x40, 0x8B, 0x23};

// A thread-local the test reads, so this image has a PE TLS directory (and the loader's _tls_index is its index).
thread_local int t_touched = 0;
extern "C" unsigned long _tls_index;

namespace
{
using drive_known::Rung;

uintptr_t g_base{};
size_t g_size{};
uint32_t Rva(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - g_base); }

// The drive every resolution here is made for. This executable is not a build any title's tables were measured
// on, so nothing is ever verified at recorded addresses: each resolution is a search, or saved addresses.
constexpr drive_cascade::Drive kHalo3{GameTitle::Halo3, "control-update"};
constexpr drive_cascade::Drive kHalo4{GameTitle::Halo4, "pilot-controller"};
constexpr drive_cascade::Drive kReach{GameTitle::HaloReach, "control-update"};

// One of "this mod's hooks", as the hook-patch source reports it: the five bytes at the marker's start are
// overwritten in the loaded image, and what stood there is what a scan must read.
sig::HookPatch g_patch{};
bool g_patched{};
uint32_t PatchSource(uintptr_t base, size_t size, sig::HookPatch* out, uint32_t capacity)
{
    if (!g_patched || g_patch.address + g_patch.size <= base || g_patch.address >= base + size)
        return 0;
    if (capacity)
        out[0] = g_patch;
    return 1;
}

bool WriteCode(void* at, const void* bytes, size_t size)
{
    DWORD old = 0;
    if (!VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &old))
        return false;
    std::memcpy(at, bytes, size);
    DWORD ignored = 0;
    VirtualProtect(at, size, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), at, size);
    return true;
}

std::string MarkerPattern()
{
    std::string text;
    char byte[4];
    for (size_t i = 0; i < sizeof(g_marker); ++i)
    {
        std::snprintf(byte, sizeof(byte), "%02X", static_cast<const volatile uint8_t*>(g_marker)[i]);
        text += i ? " " : "";
        text += byte;
    }
    return text;
}

// A function with a frame, for the function-table checks.
__declspec(noinline) int Framed(int depth)
{
    char text[64];
    std::snprintf(text, sizeof(text), "%d", depth);
    return depth > 0 ? Framed(depth - 1) + static_cast<int>(std::strlen(text)) : 0;
}

struct Found
{
    uint32_t marker = 0;
    uint32_t tls = 0;
    uint32_t tlsWrong = 0;
    bool framedIsStart = false, framedInsideIsStart = false;
    uint32_t framedStart = 0;
    uint32_t tableChecked = 0, tableAgreed = 0;
    uint32_t tlsDirectory = 0;
    uint32_t executableRanges = 0;
    drive_resolve::Verdict verdict = drive_resolve::Verdict::None;
};

// The plan a drive's install would run: one signature, the TLS index against an operand, and the function table.
Found Plan(drive_resolve::Resolver& r, uint32_t markerRecorded, const std::string& pattern, uintptr_t framed)
{
    Found f;
    f.marker = r.Unique("MARKER", markerRecorded, pattern.c_str());
    f.verdict = r.report().count ? r.report().lines[r.report().count - 1].verdict : drive_resolve::Verdict::None;
    f.tlsDirectory = r.image().tlsIndexRva;
    f.executableRanges = r.image().executableCount;
    f.tls = r.TlsIndex("TLS index", Rva(&_tls_index), Rva(&_tls_index));
    f.tlsWrong = r.TlsIndex("TLS index (another operand)", Rva(&_tls_index), Rva(&_tls_index) + 8);
    const uint32_t framedRva = static_cast<uint32_t>(framed - g_base);
    f.framedStart = drive_resolve::FunctionStartOf(r.image(), framedRva + 2);
    f.framedIsStart = r.IsFunctionStart(framedRva);
    f.framedInsideIsStart = r.IsFunctionStart(framedRva + 2);
    // THE FUNCTION TABLE IS THE OPERATING SYSTEM'S: at addresses spread over the code, the entry the resolution
    // reads from .pdata is the one RtlLookupFunctionEntry answers with.
    for (uint32_t i = 0; i < r.image().executableCount; ++i)
    {
        const drive_resolve::Range range = r.image().executable[i];
        for (uint32_t at = range.rva; at < range.rva + range.size; at += 0x397)
        {
            DWORD64 imageBase = 0;
            const PRUNTIME_FUNCTION os = RtlLookupFunctionEntry(g_base + at, &imageBase, nullptr);
            const drive_resolve::RuntimeFunction* ours = drive_resolve::EntryAt(r.image(), at);
            ++f.tableChecked;
            if (os && imageBase == g_base ? (ours && ours->begin == os->BeginAddress && ours->end == os->EndAddress)
                                          : ours == nullptr)
                ++f.tableAgreed;
        }
    }
    return f;
}

bool Run(drive_cascade::Resolution<Found>& out, uint32_t markerRecorded, const std::string& pattern, uintptr_t framed)
{
    drive_cascade::ForgetResolutions();
    return drive_cascade::Resolve(g_base, g_size, kHalo3, out, [&](drive_resolve::Resolver& r) {
        return Plan(r, markerRecorded, pattern, framed);
    });
}

void TestOpenAndResolve()
{
    drive_cascade::SetCacheFile(nullptr);   // no file: every resolution here is a search
    const std::string pattern = MarkerPattern();
    const uint32_t markerRva = Rva(g_marker);
    // The function's own start, as the operating system has it (an incremental-link thunk is not the function).
    DWORD64 imageBase = 0;
    const PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(&Framed), &imageBase, nullptr);
    const uintptr_t framed = entry ? g_base + entry->BeginAddress : 0;
    Check(entry && imageBase == g_base && framed, "the test's framed function has an unwind entry in this image");
    Check(Framed(3) == 3, "the framed function runs");

    drive_cascade::Resolution<Found> found;
    Check(Run(found, markerRva, pattern, framed) && !found.why, "this executable opens as a module");
    Check(!found.rungs.recordedBuild && found.rungs.outcome.rung == Rung::Search && !found.rungs.outcome.recordedTried,
          "it is no title's recorded build, so nothing is taken from recorded addresses: it is searched");
    Check(found.found.marker == markerRva && found.found.verdict == drive_resolve::Verdict::AsRecorded,
          "a signature is found in the loaded image, where it is recorded");
    Check(found.found.executableRanges >= 2, "the code section and the marker's own section are both searched");
    Check(found.found.tlsDirectory == Rva(&_tls_index), "the TLS index is read from the image's PE TLS directory");
    Check(found.found.tls == Rva(&_tls_index), "an operand that names the directory's index is the TLS index");
    Check(found.found.tlsWrong == 0, "an operand that names another address is refused");
    Check(found.found.framedIsStart && !found.found.framedInsideIsStart &&
              found.found.framedStart == static_cast<uint32_t>(framed - g_base),
          "a function's start is read from the loaded image's .pdata");
    Check(found.found.tableChecked > 16 && found.found.tableAgreed == found.found.tableChecked,
          "at every address tried, the .pdata entry is the one the operating system's lookup answers with");

    // Recorded somewhere else: the search wins, and says the code moved.
    drive_cascade::Resolution<Found> moved;
    Check(Run(moved, markerRva + 0x10, pattern, framed) && moved.found.marker == markerRva &&
              moved.found.verdict == drive_resolve::Verdict::Moved,
          "a wrong recorded RVA: the signature is still found where it is, and reported MOVED");

    // A signature that is not in the image.
    std::string absent = pattern;
    absent[0] = absent[0] == '0' ? '1' : '0';
    drive_cascade::Resolution<Found> missing;
    Check(Run(missing, markerRva, absent, framed) && missing.found.marker == 0 &&
              missing.found.verdict == drive_resolve::Verdict::Missing,
          "a signature that is nowhere in the image is not found, and nothing stands in for it");
    char first[200];
    drive_cascade::FirstFailure(missing.report, first, sizeof(first));
    Check(std::strstr(first, "MARKER") != nullptr, "the first failure names the anchor");

    // THROUGH ONE OF THIS MOD'S HOOKS. The marker's first five bytes are overwritten in the loaded image, as an
    // enabled hook overwrites the function it is on; the hook-patch source reports what stood there.
    uint8_t original[5];
    std::memcpy(original, g_marker, sizeof(original));
    const uint8_t jump[5] = {0xE9, 0x11, 0x22, 0x33, 0x44};
    g_patch.address = reinterpret_cast<uintptr_t>(g_marker);
    g_patch.size = 5;
    std::memcpy(g_patch.original, original, sizeof(original));
    void* const markerAt = const_cast<void*>(static_cast<const void*>(g_marker));
    Check(WriteCode(markerAt, jump, sizeof(jump)), "the marker's start is overwritten as a hook would");
    drive_cascade::Resolution<Found> blind;
    Check(Run(blind, markerRva, pattern, framed) && blind.found.marker == 0,
          "with no word of the hook, the overwritten bytes are not the signature");
    g_patched = true;
    sig::SetHookPatchSource(&PatchSource);
    drive_cascade::Resolution<Found> through;
    Check(Run(through, markerRva, pattern, framed) && through.found.marker == markerRva &&
              through.found.verdict == drive_resolve::Verdict::AsRecorded,
          "under one of this mod's enabled hooks the game's own bytes are what is matched");
    g_patched = false;
    sig::SetHookPatchSource(nullptr);
    Check(WriteCode(markerAt, original, sizeof(original)), "the marker is put back");

    // What cannot be opened is refused with a reason, and nothing is resolved.
    drive_cascade::Resolution<Found> none;
    Check(!drive_cascade::Resolve(uintptr_t{0}, g_size, kHalo3, none, [&](drive_resolve::Resolver& r) {
        return Plan(r, markerRva, pattern, framed);
    }) && none.why && none.report.count == 0, "no module: not opened, with the reason");
    static uint8_t notAnImage[0x2000];
    drive_cascade::Resolution<Found> junk;
    Check(!drive_cascade::Resolve(reinterpret_cast<uintptr_t>(notAnImage), sizeof(notAnImage), kHalo3, junk,
                                  [&](drive_resolve::Resolver& r) { return Plan(r, markerRva, pattern, framed); }) &&
              junk.why && junk.report.count == 0,
          "memory that is not a module: not opened, with the reason");
}

// The two-line drive the rest of the tests resolve: a signature, and the TLS index. `recorded`: what its table
// records for the signature (another value is another version of the table).
struct Simple
{
    uint32_t marker = 0, tls = 0;
};
using Resolved = drive_cascade::Resolution<Simple>;

bool Resolve(Resolved& out, const drive_cascade::Drive& drive, uint32_t recorded)
{
    static const std::string pattern = MarkerPattern();
    drive_cascade::ForgetResolutions();
    return drive_cascade::Resolve(g_base, g_size, drive, out, [&](drive_resolve::Resolver& r) {
        Simple f;
        f.marker = r.Unique("MARKER", recorded, pattern.c_str());
        f.tls = r.TlsIndex("TLS index", Rva(&_tls_index), Rva(&_tls_index));
        return f;
    });
}

bool Whole(const Resolved& r) { return r.found.marker == Rva(g_marker) && r.found.tls == Rva(&_tls_index); }

void TestAnnounceAndFaults()
{
    drive_cascade::SetCacheFile(nullptr);
    const uint32_t markerRva = Rva(g_marker);
    Resolved healthy;
    lines.clear();
    Check(Resolve(healthy, kHalo3, markerRva) && Whole(healthy) && healthy.rungs.outcome.learn && !healthy.rungs.saved,
          "a healthy resolution, with no cache file to save to");
    Check(healthy.rungs.outcome.moduleScans == 1 && healthy.rungs.outcome.scans == 1 && healthy.rungs.ms > 0.0 &&
              healthy.rungs.ms < 60000.0,
          "what it cost the worker is measured: one search of the whole module, and a time");
    drive_cascade::Announce("TEST DRIVE", healthy);
    Check(lines.size() == 1 &&
              Said("TEST DRIVE: found by search (address resolution: 1 scans, ",
                   " ms): of its 2 addresses 2 are where the tables record them, 0 MOVED, 0 were taken at the recorded "
                   "address among several matches; NOT saved for the next launch (the mod's folder is not known)") == 1,
          "which rung won is said with what it cost: found by search, one scan, its time, and why it was not saved");
    char cost[120];
    std::snprintf(cost, sizeof(cost), "TEST DRIVE: found by search (address resolution: 1 scans, %.2f ms): of its 2 addresses",
                  healthy.rungs.ms);
    Check(Lines(cost) == 1, "the time in the line is the time that was measured, in milliseconds to two decimals");
    drive_cascade::Announce("TEST DRIVE", healthy);
    Check(lines.size() == 2, "the line is said at every install: each one's cost is its own");

    // The dev key, in each flavour.
    struct Case
    {
        int mask;
        const char* what;
    };
    const Case cases[] = {{0x01, "a signature finds nothing"},
                          {0x02, "every signature is ambiguous"},
                          {0x04, "every recorded RVA is wrong"},
                          {0x06, "ambiguous, with a wrong record"},
                          {0x08, "the PE TLS directory disagrees"},
                          {0x40, "the saved addresses are stale"}};
    for (const Case& c : cases)
    {
        g_config.drive_fault = c.mask;
        Resolved forced;
        lines.clear();
        const bool opened = Resolve(forced, kHalo3, markerRva);
        drive_cascade::Announce("TEST DRIVE", forced);
        const drive_resolve::Verdict verdict = forced.report.lines[0].verdict;
        char what[160];
#if HALOMCCVR_DEV_DIAGNOSTICS
        using drive_resolve::Verdict;
        bool as = false;
        switch (c.mask)
        {
        case 0x01: as = forced.found.marker == 0 && verdict == Verdict::Missing; break;
        case 0x02: as = forced.found.marker == markerRva && verdict == Verdict::RecordAmongSeveral; break;
        case 0x04: as = forced.found.marker == markerRva && verdict == Verdict::Moved; break;
        case 0x06: as = forced.found.marker == 0 && verdict == Verdict::Ambiguous; break;
        case 0x08: as = forced.found.marker == markerRva && forced.found.tls == 0; break;
        case 0x40:
            as = Whole(forced) && forced.rungs.outcome.savedTried &&
                 forced.rungs.savedRefused == drive_known::kSavedStale && forced.rungs.outcome.rung == Rung::Search &&
                 Lines("the addresses saved earlier were not used (this game DLL does not have that code at them)") == 1;
            break;
        }
        std::snprintf(what, sizeof(what), "dev build, drive_fault 0x%02X (%s): the resolution shows it", c.mask, c.what);
        Check(opened && as, what);
        std::snprintf(what, sizeof(what), "dev build, drive_fault 0x%02X: the line says the result is forced", c.mask);
        Check(Lines("DEV fault injection (drive_fault") == 1 && lines.size() == 2, what);
        std::snprintf(what, sizeof(what), "dev build, drive_fault 0x%02X: nothing is saved under a forced fault", c.mask);
        Check(!forced.rungs.saved, what);
#else
        std::snprintf(what, sizeof(what), "player build, drive_fault 0x%02X (%s): the key does nothing", c.mask, c.what);
        Check(opened && Whole(forced) && verdict == drive_resolve::Verdict::AsRecorded && lines.size() == 1 &&
                  Said("TEST DRIVE: found by search (address resolution: 1 scans, ", " ms): of its 2 addresses 2 are where the tables record "
                                                                 "them") == 1,
              what);
#endif
    }
    g_config.drive_fault = 0;
    Resolved again;
    lines.clear();
    Check(Resolve(again, kHalo3, markerRva) && Whole(again), "healthy again");
    drive_cascade::Announce("TEST DRIVE", again);
    Check(lines.size() == 1 &&
              Said("TEST DRIVE: found by search (address resolution: 1 scans, ", " ms): of its 2 addresses 2 are where the tables record "
                                                             "them") == 1 &&
              Lines("DEV fault injection") == 0,
          "healthy again is said as any install is, without the forced-result line");

    // THE LINE OF A RUNG THIS EXECUTABLE CANNOT REACH (it is no title's recorded build), said from a result made
    // by hand: the same report, verified at the recorded addresses, with a cost of the test's own choosing.
    {
        drive_cascade::Rungs rungs;
        rungs.recordedBuild = true;
        rungs.outcome.rung = Rung::Recorded;
        rungs.outcome.recordedTried = true;
        rungs.ms = 0.114;
        lines.clear();
        drive_cascade::Announce("TEST DRIVE", again.report, rungs);
        Check(lines.size() == 1 &&
                  Lines("TEST DRIVE: verified at the recorded addresses, all 2 of them, with no search (address resolution: 0 scans, 0.11 "
                        "ms) (this game DLL is the build the tables were measured on)") == 1,
              "the recorded build's one line, as formatted: its scans, its milliseconds to two decimals");
        // The dev self-test's two lines. The search it ran beside the verified addresses is in the count.
        rungs.outcome.searchedToo = true;
        rungs.outcome.searchAgrees = true;
        rungs.outcome.moduleScans = 1;
        rungs.ms = 18.4;
        lines.clear();
        drive_cascade::Announce("TEST DRIVE", again.report, rungs);
        const size_t agreed = Lines("DEV self-test (drive_fault 0x80): the search was run as well, and it chose the "
                                    "same address for every line");
        const size_t counted = Lines("with no search (address resolution: 1 scans, 18.40 ms) (this game DLL");
        rungs.outcome.searchAgrees = false;
        drive_cascade::Announce("TEST DRIVE", again.report, rungs);
        const size_t disagreed = Lines("DEV self-test (drive_fault 0x80): the search was run as well, and it DISAGREES "
                                       "with the verified addresses");
#if HALOMCCVR_DEV_DIAGNOSTICS
        Check(agreed == 1 && disagreed == 1 && counted == 1 && Lines("verified at the recorded addresses") == 2,
              "dev build: the self-test says whether the search agrees, after the rung's own line");
#else
        Check(agreed == 0 && disagreed == 0 && counted == 1 && Lines("verified at the recorded addresses") == 2,
              "player build: there is no self-test line");
#endif
    }
}

// ---- the address cache's file, on disk ---------------------------------------------------------------------------
std::string ReadAll(const std::wstring& path)
{
    std::string text;
    const HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return text;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(handle, buffer, sizeof(buffer), &read, nullptr) && read)
        text.append(buffer, read);
    CloseHandle(handle);
    return text;
}

bool WriteAll(const std::wstring& path, const std::string& text)
{
    const HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool ok = WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
                    written == text.size();
    CloseHandle(handle);
    return ok;
}

bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

// The cache file's text with one drive's entry changed and the file written out again as the mod writes it, so
// that the entry's checksum is its own: what a file left by another build of the game, or by another version of
// this mod, holds.
std::string Rewritten(const std::string& text, const char* module, const char* id, void (*change)(drive_known::Entry&))
{
    static drive_known::File file;
    static char out[drive_known::kMaxFileText];
    drive_known::Parse(text.data(), text.size(), file);
    for (uint32_t i = 0; i < file.count; ++i)
        if (!std::strcmp(file.entries[i].module, module) && !std::strcmp(file.entries[i].id, id))
            change(file.entries[i]);
    return std::string(out, drive_known::Format(file, out, sizeof(out)));
}

void TestCacheFile()
{
    const uint32_t markerRva = Rva(g_marker);
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring folder = std::wstring(temp) + L"halomccvr_drive_cascade_tests_" +
                                std::to_wstring(GetCurrentProcessId()) + L"\\";
    CreateDirectoryW(folder.c_str(), nullptr);
    const std::wstring cache = folder + L"HaloMCCVR-drive-addresses.txt";
    const std::wstring temporary = cache + L".tmp";
    DeleteFileW(cache.c_str());
    drive_cascade::SetCacheFolder(folder.c_str());
    g_config.drive_address_cache = 1;
    g_config.drive_fault = 0;

    // FIRST LAUNCH on a build no table describes: searched, and saved.
    Resolved first;
    lines.clear();
    Check(Resolve(first, kHalo3, markerRva) && Whole(first) && first.rungs.outcome.rung == Rung::Search &&
              first.rungs.outcome.scans == 1 && first.rungs.outcome.learn && first.rungs.saved && !first.rungs.notSaved,
          "first launch: found by search (one scan of the module), and saved");
    drive_cascade::Announce("TEST DRIVE", first);
    Check(Said("TEST DRIVE: found by search (address resolution: 1 scans, ",
               " ms): of its 2 addresses 2 are where the tables record them, 0 MOVED, 0 were taken at the recorded "
               "address among several matches; saved for the next launch") == 1,
          "and says so");
    const std::string saved = ReadAll(cache);
    Check(Exists(cache) && !Exists(temporary) && saved.find("NOT a setting") != std::string::npos &&
              saved.find("Safe to DELETE") != std::string::npos && saved.find("schema=2\r\n") != std::string::npos &&
              saved.find("entry halo3.dll ") != std::string::npos && saved.find(" control-update ") != std::string::npos,
          "the file is beside the mod's log, says what it is, and holds the drive's entry; no temporary file is left");

    // SECOND LAUNCH: verified at the saved addresses, the module not searched at all.
    Resolved second;
    lines.clear();
    Check(Resolve(second, kHalo3, markerRva) && Whole(second) && second.rungs.outcome.rung == Rung::Saved &&
              second.rungs.outcome.scans == 0 && second.report.scans == 0 && !second.rungs.saved &&
              second.report.lines[0].how == drive_resolve::How::Known,
          "second launch: verified at the saved addresses with zero scans of the module");
    drive_cascade::Announce("TEST DRIVE", second);
    Check(Said("TEST DRIVE: verified at the addresses saved from an earlier launch of this game build, all 2 of them, "
               "with no search (address resolution: 0 scans, ", " ms) (0 of them are not where the tables record them)") == 1,
          "and says so");
    Check(ReadAll(cache) == saved, "a launch that only verifies leaves the file as it was");

    // ANOTHER TITLE'S DRIVE: its entry is added, and the first title's is kept.
    Resolved other;
    Check(Resolve(other, kHalo4, markerRva) && other.rungs.outcome.rung == Rung::Search && other.rungs.saved,
          "another title's drive is searched and saved");
    const std::string both = ReadAll(cache);
    Check(both.find("entry halo3.dll ") != std::string::npos && both.find("entry halo4.dll ") != std::string::npos &&
              both.find(" pilot-controller ") != std::string::npos,
          "two titles' entries coexist in the file");
    Resolved again3, again4;
    Check(Resolve(again3, kHalo3, markerRva) && again3.rungs.outcome.rung == Rung::Saved && Whole(again3) &&
              Resolve(again4, kHalo4, markerRva) && again4.rungs.outcome.rung == Rung::Saved && Whole(again4) &&
              again3.rungs.outcome.scans + again4.rungs.outcome.scans == 0,
          "and each is verified from it at its next launch, with no search");

    // ANOTHER BUILD'S ENTRY (the stamp differs): not tried, said, searched, and replaced.
    {
        const std::string stale = Rewritten(both, "halo3.dll", "control-update",
                                            [](drive_known::Entry& e) { e.stamp ^= 1ull << 32; });
        Check(stale != both && stale.size() == both.size() && WriteAll(cache, stale),
              "the file is given an entry of another build");
        Resolved r;
        lines.clear();
        Check(Resolve(r, kHalo3, markerRva) && Whole(r) && r.rungs.outcome.rung == Rung::Search &&
                  !r.rungs.outcome.savedTried && r.rungs.savedRefused && r.rungs.saved,
              "an entry of another build of the DLL is not tried: the drive is searched, and saved again");
        drive_cascade::Announce("TEST DRIVE", r);
        Check(Lines("the addresses saved earlier were not used (they are of another build of this game DLL)") == 1,
              "one line says the saved addresses were of another build");
        Check(ReadAll(cache) == both, "the file again holds this build's entry, and the other title's untouched");
    }

    // FOUND UNDER OTHER RULES OF THE SEARCH: not tried, said, searched, and replaced.
    {
        const std::string aged =
            Rewritten(both, "halo3.dll", "control-update", [](drive_known::Entry& e) { ++e.rules; });
        Check(aged != both && WriteAll(cache, aged), "the file is given an entry found under other rules of the search");
        Resolved r;
        lines.clear();
        Check(Resolve(r, kHalo3, markerRva) && Whole(r) && r.rungs.outcome.rung == Rung::Search &&
                  !r.rungs.outcome.savedTried && r.rungs.savedRefused == drive_known::kOtherRules && r.rungs.saved,
              "an entry found under other rules is not tried, though every address in it would verify: the drive is "
              "searched, and saved again");
        drive_cascade::Announce("TEST DRIVE", r);
        Check(Lines("the addresses saved earlier were not used (they were found under other rules of this mod's "
                    "search)") == 1,
              "one line says the saved addresses were found under other rules");
        Check(ReadAll(cache) == both, "the file again holds an entry under this mod's rules, and the other title's untouched");
    }

    // A DAMAGED ENTRY (one digit of it changed, so its checksum is no longer its own): as if it were not there.
    {
        const size_t at = both.find("entry halo3.dll ") + 16;
        std::string damaged = both;
        damaged[at] = damaged[at] == '1' ? '2' : '1';   // the first digit of the stamp
        Check(WriteAll(cache, damaged), "one digit of an entry in the file is changed");
        Resolved r;
        lines.clear();
        Check(Resolve(r, kHalo3, markerRva) && Whole(r) && r.rungs.outcome.rung == Rung::Search &&
                  !r.rungs.outcome.savedTried && !r.rungs.savedRefused && r.rungs.saved,
              "an entry whose checksum is not its own is not read at all: the drive is searched as on a first launch");
        drive_cascade::Announce("TEST DRIVE", r);
        Check(Lines("the addresses saved earlier were not used") == 0 && Lines("saved for the next launch") == 1,
              "nothing is said of it: there was no entry");
        Resolved halo3Again, halo4Again;
        Check(Resolve(halo3Again, kHalo3, markerRva) && halo3Again.rungs.outcome.rung == Rung::Saved &&
                  Resolve(halo4Again, kHalo4, markerRva) && halo4Again.rungs.outcome.rung == Rung::Saved,
              "the drive's entry is whole again, and the other title's was kept through it");
    }

    // ANOTHER VERSION OF THE DRIVE'S TABLES (the plan differs): verified bytes are not enough; searched, replaced.
    {
        Resolved r;
        lines.clear();
        Check(Resolve(r, kHalo3, markerRva + 0x10) && Whole(r) && r.rungs.outcome.rung == Rung::Search &&
                  r.rungs.outcome.savedTried && r.rungs.savedRefused == drive_known::kSavedOtherPlan && r.rungs.saved,
              "an entry saved for another version of the tables is not used: the drive is searched, and saved again");
        drive_cascade::Announce("TEST DRIVE", r);
        Check(Lines("the addresses saved earlier were not used (they were saved for another version of this drive's "
                    "tables)") == 1 && Lines("MARKER MOVED from") == 1,
              "one line says so, and names what the search found moved");
        Resolved next;
        Check(Resolve(next, kHalo3, markerRva + 0x10) && next.rungs.outcome.rung == Rung::Saved && Whole(next) &&
                  next.report.lines[0].verdict == drive_resolve::Verdict::Moved,
              "the next launch with those tables verifies the new entry, still reported as moved");
        Check(ReadAll(cache).find("entry halo4.dll ") != std::string::npos, "the other title's entry is still there");
    }

    // A FILE THAT IS NOT ONE: a cold start, and a whole file again afterwards.
    for (const std::string& broken : {std::string("\x01\x02 not a cache \xFF\r\n"), std::string(), both.substr(0, both.size() / 2),
                                      std::string("schema=7\r\n") + both.substr(both.find("entry halo3.dll "))})
    {
        Check(WriteAll(cache, broken), "the file is replaced by something that is not a cache");
        Resolved r;
        Check(Resolve(r, kHalo3, markerRva) && Whole(r) && r.rungs.outcome.rung == Rung::Search &&
                  !r.rungs.outcome.savedTried && r.rungs.saved,
              "a corrupted, empty, truncated or foreign-schema file is a cold start: searched, and saved");
        const std::string after = ReadAll(cache);
        Check(after.find("schema=2\r\n") != std::string::npos && after.find("entry halo3.dll ") != std::string::npos &&
                  after.back() == '\n',
              "and the file is whole again");
    }
    {
        // Cut short inside its LAST entry: the whole entries before it are still used.
        Resolved seed;
        (void)Resolve(seed, kHalo4, markerRva);   // halo3's entry, then halo4's
        const std::string two = ReadAll(cache);
        Check(two.find("entry halo3.dll ") < two.find("entry halo4.dll "), "two entries, halo4's last");
        Check(WriteAll(cache, two.substr(0, two.size() - 4)), "the file loses its last four bytes");
        Resolved kept, cut;
        Check(Resolve(kept, kHalo3, markerRva) && kept.rungs.outcome.rung == Rung::Saved,
              "a whole entry in a file that was cut short is still verified and used");
        Check(Resolve(cut, kHalo4, markerRva) && cut.rungs.outcome.rung == Rung::Search && cut.rungs.saved,
              "the entry that was cut is not: that drive is searched and saved again");
    }

    // THE SETTING OFF: the file is neither read nor written.
    {
        const std::string before = ReadAll(cache);
        g_config.drive_address_cache = 0;
        Resolved r;
        lines.clear();
        Check(Resolve(r, kHalo3, markerRva) && Whole(r) && r.rungs.outcome.rung == Rung::Search &&
                  !r.rungs.outcome.savedTried && !r.rungs.savedRefused && !r.rungs.saved && r.rungs.notSaved,
              "drive_address_cache = 0: a good entry is not read, and the drive is searched");
        drive_cascade::Announce("TEST DRIVE", r);
        Check(Lines("NOT saved for the next launch (drive_address_cache = 0)") == 1, "the line says why nothing was saved");
        Check(ReadAll(cache) == before, "and the file is not written");
        DeleteFileW(cache.c_str());
        Resolved none;
        Check(Resolve(none, kHalo3, markerRva) && Whole(none) && !Exists(cache) && !Exists(temporary),
              "with no file and the setting off, none is created");
        g_config.drive_address_cache = 1;
    }

    // A WRITE THAT FAILS LEAVES THE OLD FILE WHOLE.
    {
        Resolved seed;
        Check(Resolve(seed, kHalo3, markerRva) && seed.rungs.saved, "a good file");
        const std::string before = ReadAll(cache);
        Check(CreateDirectoryW(temporary.c_str(), nullptr), "the temporary file's name is taken by a folder");
        Resolved r;
        lines.clear();
        Check(Resolve(r, kHalo3, markerRva + 0x20) && Whole(r) && r.rungs.outcome.rung == Rung::Search &&
                  r.rungs.outcome.learn && !r.rungs.saved && r.rungs.notSaved,
              "the drive is resolved all the same; only the saving failed");
        drive_cascade::Announce("TEST DRIVE", r);
        Check(Lines("NOT saved for the next launch (the file could not be written)") == 1, "the line says so");
        Check(ReadAll(cache) == before, "the old file is whole, byte for byte");
        RemoveDirectoryW(temporary.c_str());
        Resolved retry;
        Check(Resolve(retry, kHalo3, markerRva + 0x20) && retry.rungs.saved && !Exists(temporary),
              "and the next launch saves it");
    }

    // THE DEV KEY AND THE FILE: while ANY bit of drive_fault is set the file is neither read nor written. That
    // goes for the bits that force no result too -- the silenced watchdog (0x10) and the self-test (0x80).
    {
        Resolved seed;
        Check(Resolve(seed, kHalo3, markerRva) && seed.rungs.saved, "the file holds this drive's entry for these tables");
        const std::string before = ReadAll(cache);
        for (const int mask : {0x04, 0x10, 0x80, 0x90})
        {
            g_config.drive_fault = mask;
            Resolved forced;
            lines.clear();
            const bool resolved = Resolve(forced, kHalo3, markerRva) && Whole(forced);
            drive_cascade::Announce("TEST DRIVE", forced);
            char what[200];
#if HALOMCCVR_DEV_DIAGNOSTICS
            std::snprintf(what, sizeof(what), "dev build, drive_fault 0x%02X: a good entry is not read, the drive is "
                                              "searched, and what the search found is not saved", mask);
            Check(resolved && forced.rungs.outcome.rung == Rung::Search && !forced.rungs.outcome.savedTried &&
                      !forced.rungs.savedRefused && forced.rungs.outcome.learn && !forced.rungs.saved &&
                      Lines("NOT saved for the next launch (drive_fault is set: a launch with the dev key in it neither "
                            "reads nor writes the file)") == 1,
                  what);
            std::snprintf(what, sizeof(what), "dev build, drive_fault 0x%02X: the file is as it was", mask);
#else
            std::snprintf(what, sizeof(what), "player build, drive_fault 0x%02X: the key does nothing, and the saved "
                                              "addresses are verified as ever", mask);
            Check(resolved && forced.rungs.outcome.rung == Rung::Saved && forced.rungs.outcome.scans == 0 &&
                      forced.report.lines[0].verdict == drive_resolve::Verdict::AsRecorded &&
                      Lines("verified at the addresses saved from an earlier launch") == 1,
                  what);
            std::snprintf(what, sizeof(what), "player build, drive_fault 0x%02X: nothing written", mask);
#endif
            Check(ReadAll(cache) == before && !Exists(temporary), what);
        }
        // ... and with NO ENTRY PRESENT, when the search's result is one worth saving: no file is made.
        DeleteFileW(cache.c_str());
        for (const int mask : {0x80, 0x10, 0x90, 0x04})
        {
            g_config.drive_fault = mask;
            Resolved r;
            const bool searched = Resolve(r, kHalo3, markerRva) && Whole(r) && r.rungs.outcome.rung == Rung::Search &&
                                  r.rungs.outcome.learn;
            char what[200];
#if HALOMCCVR_DEV_DIAGNOSTICS
            std::snprintf(what, sizeof(what), "dev build, drive_fault 0x%02X with no entry present: the drive is searched, "
                                              "and no file is written", mask);
            Check(searched && !r.rungs.saved && r.rungs.notSaved && !Exists(cache) && !Exists(temporary), what);
#else
            std::snprintf(what, sizeof(what), "player build, drive_fault 0x%02X with no entry present: the key does "
                                              "nothing, and the search is saved as ever", mask);
            Check(searched && r.rungs.saved && Exists(cache), what);
            DeleteFileW(cache.c_str());
#endif
        }
        g_config.drive_fault = 0;
    }

    // A LINE SAVED AS THE SINGLE MATCH IN ITS FUNCTION: the next launch searches that one function again, and
    // says so.
    {
        DWORD64 imageBase = 0;
        const PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(&Framed), &imageBase, nullptr);
        const uint32_t start = entry ? entry->BeginAddress : 0, end = entry ? entry->EndAddress : 0;
        // Twelve bytes of the framed function's own body that stand in it once. The plan records them four bytes
        // further on, where they are not: the search finds them off their offset, as the function's single match.
        const auto* const code = reinterpret_cast<const uint8_t*>(g_base);
        uint32_t at = 0;
        for (uint32_t candidate = start + 4; start && candidate + 16 <= end && !at; ++candidate)
        {
            uint32_t matches = 0;
            for (uint32_t other = start; other + 12 <= end; ++other)
                matches += !std::memcmp(code + other, code + candidate, 12) ? 1u : 0u;
            if (matches == 1)
                at = candidate;
        }
        Check(at != 0, "the framed function holds twelve bytes that stand in it once");
        std::string row;
        char byte[4];
        for (uint32_t i = 0; at && i < 12; ++i)
        {
            std::snprintf(byte, sizeof(byte), "%02X", code[at + i]);
            row += i ? " " : "";
            row += byte;
        }
        struct WithRow
        {
            uint32_t marker = 0, row = 0;
        };
        const auto resolve = [&](drive_cascade::Resolution<WithRow>& out) {
            static const std::string marker = MarkerPattern();
            drive_cascade::ForgetResolutions();
            return drive_cascade::Resolve(g_base, g_size, kReach, out, [&](drive_resolve::Resolver& r) {
                WithRow f;
                f.marker = r.Unique("MARKER", markerRva, marker.c_str());
                f.row = r.InFunction("ROW", at + 4, start, start, row.c_str());
                return f;
            });
        };
        drive_cascade::Resolution<WithRow> first, next;
        lines.clear();
        Check(at && resolve(first) && first.found.row == at && first.rungs.outcome.rung == Rung::Search &&
                  first.report.lines[1].how == drive_resolve::How::FunctionExtent && first.rungs.saved,
              "a row that is not at its recorded offset is found as the single match in its function, and saved");
        Check(at && resolve(next) && next.found.row == at && next.found.marker == markerRva &&
                  next.rungs.outcome.rung == Rung::Saved && next.rungs.outcome.scans == 1 && next.report.scans == 1,
              "the next launch verifies it with that one function searched again, and the module not searched");
        drive_cascade::Announce("TEST DRIVE", next);
        Check(Said("TEST DRIVE: verified at the addresses saved from an earlier launch of this game build, all 2 of "
                   "them, with no search of the module (address resolution: 0 scans, ",
                   " ms) (1 of them are not where the tables record them; 1 of its functions were searched again, each "
                   "for a line saved as the single match in it)") == 1 &&
                  next.rungs.outcome.moduleScans == 0,
              "and its line says which search was made again: one function's, and none of the module");
    }

    drive_cascade::SetCacheFile(nullptr);
    DeleteFileW(cache.c_str());
    RemoveDirectoryW(temporary.c_str());
    RemoveDirectoryW(folder.c_str());
}

// ---- a cache file that is there and could not be read is left as it is ------------------------------------------
// "There is no file" is a cold start. "There is a file, and it was not read" is not: the other drives' and the
// other titles' entries are in it, and a write would replace them all with one.
#if HALOMCCVR_DEV_DIAGNOSTICS
// Another program that has the cache's file open only while this mod reads it. A dev build calls the probe before
// each open of the file: at open number g_holdAtOpen it takes the file and shares nothing, and at the open after
// that it lets go.
const wchar_t* g_holdPath = nullptr;
HANDLE g_holding = INVALID_HANDLE_VALUE;
int g_holdAtOpen = 0, g_holdOpens = 0;
void HoldingProbe()
{
    ++g_holdOpens;
    if (g_holdOpens == g_holdAtOpen)
    {
        g_holding = CreateFileW(g_holdPath, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    else if (g_holding != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_holding);
        g_holding = INVALID_HANDLE_VALUE;
    }
}
#endif

void TestUnreadCacheFileIsNotReplaced()
{
    const uint32_t markerRva = Rva(g_marker);
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring folder = std::wstring(temp) + L"halomccvr_drive_cascade_unread_" +
                                std::to_wstring(GetCurrentProcessId()) + L"\\";
    CreateDirectoryW(folder.c_str(), nullptr);
    const std::wstring cache = folder + L"HaloMCCVR-drive-addresses.txt";
    const std::wstring temporary = cache + L".tmp";
    DeleteFileW(cache.c_str());
    drive_cascade::SetCacheFolder(folder.c_str());
    g_config.drive_address_cache = 1;
    g_config.drive_fault = 0;
    Resolved halo3, halo4;
    Check(Resolve(halo3, kHalo3, markerRva) && halo3.rungs.saved && Resolve(halo4, kHalo4, markerRva) &&
              halo4.rungs.saved,
          "a file with two titles' entries");
    const std::string before = ReadAll(cache);
    Check(before.find("entry halo3.dll ") != std::string::npos && before.find("entry halo4.dll ") != std::string::npos,
          "both are in it");

    // ANOTHER PROGRAM HOLDS THE FILE and shares nothing: it cannot be opened for reading.
    {
        const HANDLE held =
            CreateFileW(cache.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        Check(held != INVALID_HANDLE_VALUE, "the file is held by another handle that shares nothing");
        Resolved r;
        lines.clear();
        const bool resolved = Resolve(r, kHalo3, markerRva);
        CloseHandle(held);
        Check(resolved && Whole(r) && r.rungs.outcome.rung == Rung::Search && r.rungs.outcome.learn && !r.rungs.saved &&
                  r.rungs.notSaved && !std::strcmp(r.rungs.notSaved, "the file is there and could not be read"),
              "the drive is resolved by search; what it found is NOT saved, because the file could not be read");
        drive_cascade::Announce("TEST DRIVE", r);
        Check(Lines("NOT saved for the next launch (the file is there and could not be read)") == 1,
              "its line says so, once");
        Check(ReadAll(cache) == before && !Exists(temporary),
              "THE FILE IS AS IT WAS: every other drive's and title's entry is still in it");
    }

#if HALOMCCVR_DEV_DIAGNOSTICS
    // HELD ONLY WHILE IT IS READ BEFORE A SAVE, and let go before the save's write. To take that failed read for
    // an empty file would put one entry in the place of them all, and the write would go through.
    {
        g_holdPath = cache.c_str();
        g_holdAtOpen = 2;   // the look-up's read goes through; the read that a save begins with does not
        g_holdOpens = 0;
        drive_cascade::SetCacheProbe(&HoldingProbe);
        Resolved r;
        lines.clear();
        const bool resolved = Resolve(r, kHalo3, markerRva + 0x10);   // other tables: searched, and worth saving
        drive_cascade::SetCacheProbe(nullptr);
        const bool stillHeld = g_holding != INVALID_HANDLE_VALUE;
        if (stillHeld)
            CloseHandle(g_holding);
        g_holding = INVALID_HANDLE_VALUE;
        Check(resolved && Whole(r) && r.rungs.outcome.rung == Rung::Search && r.rungs.outcome.learn && !r.rungs.saved &&
                  r.rungs.notSaved && !std::strcmp(r.rungs.notSaved, "the file is there and could not be read") &&
                  stillHeld && g_holdOpens == 2,
              "dev build: a file held only while it is read before a save: nothing is saved, and no write is tried");
        Check(ReadAll(cache) == before && !Exists(temporary),
              "dev build: EVERY ENTRY IS STILL IN THE FILE, though a write would have gone through");
    }
#endif

    // A FILE LARGER THAN ANY THIS MOD WRITES: not this mod's. It is left alone too.
    {
        std::string large = before;
        while (large.size() <= drive_known::kMaxFileText)
            large += "# a line this mod did not write\r\n";
        Check(WriteAll(cache, large), "the file is made larger than any this mod writes");
        Resolved r;
        lines.clear();
        Check(Resolve(r, kHalo3, markerRva) && Whole(r) && r.rungs.outcome.rung == Rung::Search &&
                  !r.rungs.outcome.savedTried && r.rungs.outcome.learn && !r.rungs.saved && r.rungs.notSaved &&
                  !std::strcmp(r.rungs.notSaved, "the file is there and is larger than any this mod writes"),
              "nothing is taken from it, the drive is searched for, and what it found is NOT saved");
        drive_cascade::Announce("TEST DRIVE", r);
        Check(Lines("NOT saved for the next launch (the file is there and is larger than any this mod writes)") == 1,
              "its line says why");
        Check(ReadAll(cache) == large && !Exists(temporary), "and the file is as it was");
    }

    // NO FILE is a cold start, as ever: one is written.
    {
        DeleteFileW(cache.c_str());
        Resolved r;
        Check(Resolve(r, kHalo3, markerRva) && r.rungs.saved && !r.rungs.notSaved && Exists(cache),
              "with no file there, one is written");
    }

    drive_cascade::SetCacheFile(nullptr);
    DeleteFileW(cache.c_str());
    RemoveDirectoryW(folder.c_str());
}

// ---- the installs' proofs after the plan (src/dll/drive_install.h) never fault -----------------------------------
// A plan reads the module through checks: a page that cannot be read costs it one line, and it returns with that
// line refused and everything else still set. What an install does next runs with no guard around it. Here the
// dispatcher's unwind info lies on a page that cannot be read.
void TestInstallProofsNeverFault()
{
    // A three-page "module" with a function table of its own: a dispatcher at +0x100 (two pushes and
    // `sub rsp, 0xC0`: a 0xD0 frame) that calls +0x180 from +0x140; its unwind info alone on the third page.
    auto* const image = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x3000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Check(image != nullptr, "three pages for a module of the test's own");
    if (!image)
        return;
    const uintptr_t base = reinterpret_cast<uintptr_t>(image);
    std::memset(image, 0xCC, 0x1000);
    const uint8_t prologue[] = {0x48, 0x8B, 0xC4, 0x55, 0x53, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00};
    std::memcpy(image + 0x100, prologue, sizeof(prologue));
    image[0x140] = 0xE8;
    const int32_t rel = 0x180 - 0x145;
    std::memcpy(image + 0x141, &rel, sizeof(rel));
    const uint8_t unwind[12] = {0x01, 12, 3, 0, 12, 0x11, 0xD0, 0, 0, 0, 0, 0};   // one ALLOC_LARGE of 0xD0
    std::memcpy(image + 0x2000, unwind, sizeof(unwind));
    static RUNTIME_FUNCTION table[1];
    table[0].BeginAddress = 0x100;
    table[0].EndAddress = 0x170;
    table[0].UnwindData = 0x2000;
    Check(RtlAddFunctionTable(table, 1, base) != 0, "the module's function table is the system's");

    uint32_t frame = 0;
    const char* why = nullptr;
    Check(drive_install::ProveDispatcher(base, 0x3000, 0x100, 0x140, frame, why) && frame == 0xD8,
          "with everything readable the dispatcher's frame is proven from the system's table");

    DWORD old = 0;
    Check(VirtualProtect(image + 0x2000, 0x1000, PAGE_NOACCESS, &old) != 0, "the unwind info's page can no longer be read");
    // THE READ ITSELF IS GUARDED: without the guard this faults, on a thread that has none.
    frame = 1;
    why = nullptr;
    Check(!drive_install::ProveDispatcher(base, 0x3000, 0x100, 0x140, frame, why) && frame == 0 && why != nullptr,
          "unwind info that cannot be read is a failed proof, not a fault");

    // AND NOTHING THE PLAN REFUSED IS READ AGAIN. The plan's result for such a module: the control update and
    // the dispatcher found, the frame refused.
    drive_sites::Callers callers;
    callers.accumulatorCall = 0x140;
    callers.dispatcher = 0x100;
    callers.frame = 0;
    aim_caller::CallerMap map;
    drive_install::CallerMapProof proof =
        drive_install::BuildCallerMap(base, 0x3000, 0x180, callers, drive_sites::kReachCallerAnchors, map);
    Check(!proof.proven && proof.why && std::strstr(proof.why, "the plan did not prove") && !map.stick_proven(),
          "a caller map whose frame the plan refused is not proven, and says that the plan did not prove it");
    callers.frame = 0xD8;   // a plan that did prove it, on a module whose page has since gone
    proof = drive_install::BuildCallerMap(base, 0x3000, 0x180, callers, drive_sites::kReachCallerAnchors, map);
    Check(!proof.proven && proof.why != nullptr, "a frame that can no longer be read is not proven either, with no fault");
    // Roles the plan refused: the map says so, not that the stick update's call failed.
    callers.why = drive_sites::kRolesNotDistinct;
    DWORD ignored = 0;
    VirtualProtect(image + 0x2000, 0x1000, PAGE_READWRITE, &ignored);
    proof = drive_install::BuildCallerMap(base, 0x3000, 0x180, callers, drive_sites::kReachCallerAnchors, map);
    Check(!proof.proven && proof.why == drive_sites::kRolesNotDistinct,
          "a map with no role left says what the plan refused: the roles, not the stick update's call");
    VirtualProtect(image + 0x2000, 0x1000, PAGE_NOACCESS, &ignored);

    // A pilot's drive.
    const drive_sites::Anchor rows[1] = {{"ROW", 0x180, drive_sites::kAnywhere, "CC"}};
    const uint32_t rowsFound[1] = {0x180};
    drive_sites::Flight flight;
    flight.entry = 0x180;
    flight.call = 0x140;
    flight.dispatcher = 0x100;
    flight.tlsIndex = 0x1800;
    flight.frame = 0;
    drive_install::FlightDecision decision =
        drive_install::DecideFlight(base, 0x3000, true, rows, rowsFound, flight, base + 0x1800, false, 1, 0);
    Check(decision.rowsOk && decision.tlsOk && decision.callOk && !decision.frameOk && !decision.hook,
          "a pilot's drive whose frame the plan refused is not hooked, and its unwind info is not read");
    flight.frame = 0xD8;
    decision = drive_install::DecideFlight(base, 0x3000, true, rows, rowsFound, flight, base + 0x1800, false, 1, 0);
    Check(!decision.frameOk && !decision.hook && decision.frameWhy != nullptr,
          "nor is one whose frame can no longer be read: a failed proof, no fault");
    // A call on a page that cannot be read.
    VirtualProtect(image, 0x1000, PAGE_NOACCESS, &ignored);
    Check(drive_install::CallReturn(base, 0x3000, 0x140, 0x180) == 0, "a call that cannot be read lands nowhere");
    VirtualProtect(image, 0x3000, PAGE_READWRITE, &ignored);
    Check(drive_install::CallReturn(base, 0x3000, 0x140, 0x180) == base + 0x145, "and read again when it can be");

    RtlDeleteFunctionTable(table);
    VirtualFree(image, 0, MEM_RELEASE);
}

// ---- a module whose function table cannot be read ----------------------------------------------------------------
// The table's last entries are the first thing a reader looks at (its zeroed tail is trimmed). When that page
// cannot be read the module must still open: its signatures are found, and only what needs a function's start
// or extent is lost.
void TestUnreadableFunctionTable()
{
    drive_cascade::SetCacheFile(nullptr);
    constexpr size_t kSize = 0x5000;
    auto* const image = static_cast<uint8_t*>(VirtualAlloc(nullptr, kSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Check(image != nullptr, "five pages for a module of the test's own");
    if (!image)
        return;
    const uintptr_t base = reinterpret_cast<uintptr_t>(image);
    const auto put16 = [image](size_t at, uint16_t v) { std::memcpy(image + at, &v, sizeof(v)); };
    const auto put32 = [image](size_t at, uint32_t v) { std::memcpy(image + at, &v, sizeof(v)); };
    // PE32+ headers: .text at +0x1000 (two pages), unwind info at +0x3000, the function table alone at +0x4000.
    const size_t nt = 0x80, optional = nt + 24, sections = optional + 240;
    put16(0, 0x5A4D);
    put32(0x3C, static_cast<uint32_t>(nt));
    put32(nt, 0x00004550u);
    put16(nt + 4, 0x8664);
    put16(nt + 6, 3);
    put16(nt + 20, 240);
    put16(optional, 0x20B);
    put32(optional + 56, static_cast<uint32_t>(kSize));
    put32(optional + 108, 16);
    put32(sections + 8, 0x2000);
    put32(sections + 12, 0x1000);
    put32(sections + 36, 0x60000020u);
    put32(sections + 40 + 8, 0x1000);
    put32(sections + 40 + 12, 0x3000);
    put32(sections + 40 + 36, 0x40000040u);
    put32(sections + 80 + 8, 0x1000);
    put32(sections + 80 + 12, 0x4000);
    put32(sections + 80 + 36, 0x40000040u);
    put32(optional + 112 + 3 * 8, 0x4000);
    put32(optional + 112 + 3 * 8 + 4, 36);   // two entries and a zeroed third
    std::memset(image + 0x1000, 0xCC, 0x2000);
    const uint8_t entryBytes[16] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0x33};
    std::memcpy(image + 0x1100, entryBytes, sizeof(entryBytes));
    const RUNTIME_FUNCTION entries[2] = {{0x1100, 0x1200, 0x3000}, {0x1200, 0x1280, 0x3004}};
    std::memcpy(image + 0x4000, entries, sizeof(entries));
    image[0x3000] = 0x01;
    image[0x3004] = 0x01;

    struct Probe
    {
        uint32_t entry = 0;
        bool isStart = false;
        uint32_t functions = 0;
    };
    const auto resolve = [&](drive_cascade::Resolution<Probe>& out) {
        drive_cascade::ForgetResolutions();
        return drive_cascade::Resolve(base, kSize, kHalo3, out, [](drive_resolve::Resolver& r) {
            Probe f;
            f.entry = r.Unique("ENTRY", 0x1100, "48 89 5C 24 08 57 48 83 EC 40 8B DA 48 8B F9 33");
            f.isStart = r.IsFunctionStart(f.entry);
            f.functions = r.image().functionCount;
            return f;
        });
    };
    drive_cascade::Resolution<Probe> whole, without;
    Check(resolve(whole) && !whole.why && whole.found.entry == 0x1100 && whole.found.isStart && whole.found.functions == 2,
          "with everything readable the module opens, and its function table says where a function starts");
    DWORD old = 0;
    Check(VirtualProtect(image + 0x4000, 0x1000, PAGE_NOACCESS, &old) != 0, "the function table's page can no longer be read");
    Check(resolve(without) && !without.why,
          "a module whose function table cannot be read still opens: the table is asked about before it is read");
    Check(without.found.entry == 0x1100 && !without.found.isStart && without.found.functions == 0,
          "its signatures are still found, and nothing in it is known to be a function's start");
    VirtualFree(image, 0, MEM_RELEASE);
}

// ---- a module that IS a title's recorded build -------------------------------------------------------------------
// This executable is no title's recorded build, so nothing above reaches rung 1 (a) through the DLL's side. This
// module does: a few pages with PE headers that carry Halo 2's recorded PE timestamp and image size (the rest of
// that size is reserved and never touched), and a "drive" of one signature that must be where a function starts.
struct RecordedModule
{
    uint8_t* image = nullptr;
    uintptr_t base = 0;
    size_t size = 0;

    RecordedModule()
    {
        const title_build::Identity* const known = title_build::Known(GameTitle::Halo2);
        size = known ? known->sizeOfImage : 0;
        image = size ? static_cast<uint8_t*>(VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS)) : nullptr;
        if (!image || !VirtualAlloc(image, 0x5000, MEM_COMMIT, PAGE_READWRITE))
            return;
        base = reinterpret_cast<uintptr_t>(image);
        const auto put16 = [this](size_t at, uint16_t v) { std::memcpy(image + at, &v, sizeof(v)); };
        const auto put32 = [this](size_t at, uint32_t v) { std::memcpy(image + at, &v, sizeof(v)); };
        const size_t nt = 0x80, optional = nt + 24, sections = optional + 240;
        put16(0, 0x5A4D);
        put32(0x3C, static_cast<uint32_t>(nt));
        put32(nt, 0x00004550u);
        put16(nt + 4, 0x8664);
        put16(nt + 6, 3);
        put32(nt + 8, known->peTimestamp);
        put16(nt + 20, 240);
        put16(optional, 0x20B);
        put32(optional + 56, known->sizeOfImage);
        put32(optional + 108, 16);
        put32(sections + 8, 0x2000);
        put32(sections + 12, 0x1000);
        put32(sections + 36, 0x60000020u);
        put32(sections + 40 + 8, 0x1000);
        put32(sections + 40 + 12, 0x3000);
        put32(sections + 40 + 36, 0x40000040u);
        put32(sections + 80 + 8, 0x1000);
        put32(sections + 80 + 12, 0x4000);
        put32(sections + 80 + 36, 0x40000040u);
        put32(optional + 112 + 3 * 8, 0x4000);
        put32(optional + 112 + 3 * 8 + 4, 24);
        std::memset(image + 0x1000, 0xCC, 0x2000);
        std::memcpy(image + kEntryRva, kEntryBytes, sizeof(kEntryBytes));
        const RUNTIME_FUNCTION entries[2] = {{kEntryRva, kEntryRva + 0x100, 0x3000}, {0x1200, 0x1280, 0x3004}};
        std::memcpy(image + 0x4000, entries, sizeof(entries));
        image[0x3000] = 0x01;
        image[0x3004] = 0x01;
    }
    ~RecordedModule()
    {
        if (image)
            VirtualFree(image, 0, MEM_RELEASE);
    }

    static constexpr uint32_t kEntryRva = 0x1100;
    static constexpr uint8_t kEntryBytes[16] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
                                                0xEC, 0x40, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0x33};
    static constexpr char kEntryPattern[] = "48 89 5C 24 08 57 48 83 EC 40 8B DA 48 8B F9 33";

    struct Found
    {
        uint32_t entry = 0;
    };
    // The drive's plan: the hooked function by its signature, which must be where a function starts.
    static Found Plan(drive_resolve::Resolver& r)
    {
        Found f;
        f.entry = r.Unique("ENTRY", kEntryRva, kEntryPattern);
        if (f.entry && !r.IsFunctionStart(f.entry))
            f.entry = r.Refuse("it is not where a function starts");
        return f;
    }
    // `nextLaunch`: as a new process, with nothing remembered (false: the same process installing the drive again).
    bool Resolve(drive_cascade::Resolution<Found>& out, bool nextLaunch = true) const
    {
        if (nextLaunch)
            drive_cascade::ForgetResolutions();
        return drive_cascade::Resolve(base, size, drive_cascade::Drive{GameTitle::Halo2, "control-update"}, out,
                                      [](drive_resolve::Resolver& r) { return Plan(r); });
    }
};

// ON THE RECORDED BUILD, WITH EVERYTHING VERIFYING, THE CACHE'S FILE IS NEVER OPENED.
void TestRecordedBuildOpensNoFile()
{
    RecordedModule module;
    Check(module.base != 0, "a module with Halo 2's recorded PE timestamp and image size");
    if (!module.base)
        return;
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring folder = std::wstring(temp) + L"halomccvr_drive_cascade_recorded_" +
                                std::to_wstring(GetCurrentProcessId()) + L"\\";
    CreateDirectoryW(folder.c_str(), nullptr);
    const std::wstring cache = folder + L"HaloMCCVR-drive-addresses.txt";
    DeleteFileW(cache.c_str());
    drive_cascade::SetCacheFolder(folder.c_str());
    g_config.drive_address_cache = 1;
    g_config.drive_fault = 0;

    drive_cascade::Resolution<RecordedModule::Found> first, again;
    uint32_t opens = drive_cascade::CacheFileOpens();
    Check(module.Resolve(first) && first.rungs.recordedBuild && first.rungs.outcome.rung == Rung::Recorded &&
              first.found.entry == RecordedModule::kEntryRva && first.rungs.outcome.scans == 0 && first.report.Complete(),
          "through the DLL's side it is the recorded build, and its drive is verified at the recorded address");
    Check(module.Resolve(again) && again.rungs.outcome.rung == Rung::Recorded &&
              drive_cascade::CacheFileOpens() == opens && !first.rungs.outcome.savedLooked && !Exists(cache),
          "ON THE RECORDED BUILD WITH EVERYTHING VERIFYING THE CACHE'S FILE IS NEVER OPENED, at the first install "
          "or at the next");
    // What a player's log holds for that install, through the DLL's own side: the rung, and what it cost.
    lines.clear();
    drive_cascade::Announce(drive_cascade::kHalo2Name, first);
    drive_cascade::Announce(drive_cascade::kHalo2Name, again);
    Check(lines.size() == 2 && first.rungs.outcome.moduleScans == 0 && first.rungs.ms > 0.0 &&
              Said("Halo 2 DRIVE: verified at the recorded addresses, all 1 of them, with no search (address resolution: 0 scans, ",
                   " ms) (this game DLL is the build the tables were measured on)") == 2,
          "each install's line: verified at the recorded addresses, no scan of the module, and its time");

    // The same process installing it again, as at the next level: rung 1 (a) again. It needs nothing remembered.
    drive_cascade::Resolution<RecordedModule::Found> sameProcess;
    Check(module.Resolve(sameProcess, false) && sameProcess.rungs.outcome.rung == Rung::Recorded &&
              !sameProcess.rungs.outcome.rememberedTried && drive_cascade::CacheFileOpens() == opens,
          "installed again in the same process it is verified at the recorded addresses again: nothing is remembered "
          "for a drive rung 1 (a) settles");

    // One byte of the recorded build no longer the game's: rung 1 (a) fails, and only then is the file opened.
    module.image[RecordedModule::kEntryRva + 3] ^= 0xFF;
    drive_cascade::Resolution<RecordedModule::Found> broken;
    opens = drive_cascade::CacheFileOpens();
    Check(module.Resolve(broken) && broken.rungs.outcome.recordedTried && broken.rungs.outcome.rung == Rung::Search &&
              broken.rungs.outcome.savedLooked && !broken.found.entry && drive_cascade::CacheFileOpens() == opens + 1,
          "when the recorded addresses do not verify, the file is opened once, to ask for saved ones");
    // REFUSED ON THE RECORDED BUILD, AND INSTALLED AGAIN. Its next three installs search again, without the file
    // being opened for it again; the one after them repeats the refusal with no search, and says so.
    opens = drive_cascade::CacheFileOpens();
    bool searchedAgain = broken.rungs.outcome.moduleScans == 1;
    for (uint32_t n = 1; n <= drive_known::kFreshSearches; ++n)
    {
        drive_cascade::Resolution<RecordedModule::Found> again;
        searchedAgain = searchedAgain && module.Resolve(again, false) && again.rungs.outcome.recordedTried &&
                        again.rungs.outcome.rung == Rung::Search && again.rungs.outcome.moduleScans == 1 &&
                        again.rungs.searchedAgain == n && !again.found.entry;
    }
    Check(searchedAgain && drive_cascade::CacheFileOpens() == opens,
          "a drive refused on the recorded build is searched for again at its next three installs (one scan each), "
          "and the file is not opened for it again");
    drive_cascade::Resolution<RecordedModule::Found> refusedAgain;
    lines.clear();
    Check(module.Resolve(refusedAgain, false) && refusedAgain.rungs.outcome.recordedTried &&
              refusedAgain.rungs.outcome.rung == Rung::Remembered && refusedAgain.rungs.outcome.scans == 0 &&
              refusedAgain.rungs.outcome.moduleScans == 0 && refusedAgain.rungs.searches == 4 &&
              !refusedAgain.found.entry && drive_cascade::CacheFileOpens() == opens,
          "at the install after those: zero scans, and the file not opened");
    drive_cascade::Announce(drive_cascade::kHalo2Name, refusedAgain);
    Check(lines.size() == 1 &&
              Said("Halo 2 DRIVE: as this session already resolved it in this module, with no search (address "
                   "resolution: 0 scans, ",
                   " ms): 0 of its 1 addresses verified again where they stand, 1 were NOT found by its 4 searches of "
                   "this module and are not searched again in this module: ENTRY NOT FOUND") == 1,
          "and its line says the refusal again, that it is not searched again, and what this install's "
          "resolution cost");
    // On the recorded build even that heals: rung 1 (a) is tried at every install, whatever is remembered.
    module.image[RecordedModule::kEntryRva + 3] ^= 0xFF;
    drive_cascade::Resolution<RecordedModule::Found> healed;
    Check(module.Resolve(healed, false) && healed.rungs.outcome.rung == Rung::Recorded &&
              healed.found.entry == RecordedModule::kEntryRva && healed.rungs.outcome.scans == 0,
          "and once the recorded address holds its bytes again, the next install verifies it there: the refusal "
          "is gone");

    // The same module as another build (its PE timestamp one higher): asked for, searched, saved, and found there
    // by the install after.
    uint32_t stamp = 0;
    std::memcpy(&stamp, module.image + 0x80 + 8, sizeof(stamp));
    ++stamp;
    std::memcpy(module.image + 0x80 + 8, &stamp, sizeof(stamp));
    drive_cascade::Resolution<RecordedModule::Found> other, next;
    opens = drive_cascade::CacheFileOpens();
    Check(module.Resolve(other) && !other.rungs.recordedBuild && other.rungs.outcome.rung == Rung::Search &&
              other.rungs.outcome.savedLooked && other.rungs.saved && Exists(cache) &&
              drive_cascade::CacheFileOpens() == opens + 3,
          "on another build the file is opened to ask (once), and to save what the search found (read, then written)");
    opens = drive_cascade::CacheFileOpens();
    Check(module.Resolve(next) && next.rungs.outcome.rung == Rung::Saved && next.rungs.outcome.scans == 0 &&
              drive_cascade::CacheFileOpens() == opens + 1,
          "and the install after opens it once, and verifies what it holds");

    drive_cascade::SetCacheFile(nullptr);
    DeleteFileW(cache.c_str());
    RemoveDirectoryW(folder.c_str());
}

// ---- a miss that was true only for a moment ------------------------------------------------------------------------
// A first search can miss a line for a reason that was true only then. The drive is searched for again at its
// next installs, and a search that finds everything takes the old result's place: from the install after it the
// drive is verified in place, whole. `required`: the line that is missing at first is one the drive cannot do
// without (it is refused), not one it can.
void TestSearchedAgainUntilWhole(bool required)
{
    RecordedModule module;   // by Halo 2's stamp; resolved here for Halo 3, for which it is no recorded build
    Check(module.base != 0, "a module of the test's own");
    if (!module.base)
        return;
    static constexpr uint32_t kOptionRva = 0x1800;
    static constexpr uint8_t kOptionBytes[16] = {0x0F, 0x1F, 0x84, 0x00, 0x11, 0x22, 0x33, 0x44,
                                                 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xC0};
    struct Parts
    {
        uint32_t entry = 0, option = 0;
    };
    using Installed = drive_cascade::Resolution<Parts>;
    const auto install = [&](Installed& out) {
        return drive_cascade::Resolve(module.base, module.size, kHalo3, out, [](drive_resolve::Resolver& r) {
            Parts f;
            f.entry = r.Unique("ENTRY", RecordedModule::kEntryRva, RecordedModule::kEntryPattern);
            if (f.entry && !r.IsFunctionStart(f.entry))
                f.entry = r.Refuse("it is not where a function starts");
            f.option = r.Unique("OPTION", kOptionRva, "0F 1F 84 00 11 22 33 44 55 66 77 88 99 AA BB C0");
            return f;
        });
    };
    // What is missing at first, and how it comes back.
    const auto disturb = [&] {
        if (required)
            module.image[RecordedModule::kEntryRva + 3] ^= 0xFF;   // one byte of the hooked function's signature
    };
    const auto settle = [&] {
        if (required)
            module.image[RecordedModule::kEntryRva + 3] ^= 0xFF;
        else
            std::memcpy(module.image + kOptionRva, kOptionBytes, sizeof(kOptionBytes));
    };
    if (required)
        std::memcpy(module.image + kOptionRva, kOptionBytes, sizeof(kOptionBytes));   // only the entry is disturbed
    disturb();

    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring folder = std::wstring(temp) + L"halomccvr_drive_cascade_healed_" +
                                std::to_wstring(GetCurrentProcessId()) + L"\\";
    CreateDirectoryW(folder.c_str(), nullptr);
    const std::wstring cache = folder + L"HaloMCCVR-drive-addresses.txt";
    DeleteFileW(cache.c_str());
    drive_cascade::SetCacheFolder(folder.c_str());
    g_config.drive_address_cache = 1;
    g_config.drive_fault = 0;
    drive_cascade::ForgetResolutions();
    const char* const which = required ? "a required line missed at first" : "an optional line missed at first";
    char what[240];

    Installed first, second, third, fourth;
    const bool missed = install(first) && first.rungs.outcome.rung == Rung::Search && !first.report.Complete() &&
                        (required ? first.found.entry == 0 : first.found.option == 0) && install(second) &&
                        second.rungs.outcome.rung == Rung::Search && second.rungs.searchedAgain == 1 &&
                        !second.report.Complete();
    std::snprintf(what, sizeof(what), "%s: install 1 does not find it%s, and install 2 searches again and does not "
                                      "either", which, required ? " (no hook)" : "");
    Check(missed, what);
    settle();   // what kept the line from being found is over
    lines.clear();
    const bool found = install(third) && third.rungs.outcome.rung == Rung::Search && third.rungs.searchedAgain == 2 &&
                       third.rungs.outcome.moduleScans == 2 && third.report.Complete() &&
                       third.found.entry == RecordedModule::kEntryRva && third.found.option == kOptionRva;
    drive_cascade::Announce("TEST DRIVE", third);
    std::snprintf(what, sizeof(what), "%s: INSTALL 3 SEARCHES AGAIN AND FINDS EVERYTHING%s, and its line says so",
                  which, required ? " (the drive can be hooked after all)" : "");
    Check(found && third.rungs.saved && lines.size() == 1 &&
              Said("TEST DRIVE: searched again, 2 of 3, and found (address resolution: 2 scans, ",
                   " ms): of its 2 addresses 2 are where the tables record them, 0 MOVED, 0 were taken at the recorded "
                   "address among several matches; saved for the next launch") == 1,
          what);
    const uint32_t opens = drive_cascade::CacheFileOpens();
    lines.clear();
    const bool verified = install(fourth) && fourth.rungs.outcome.rung == Rung::Remembered &&
                          fourth.rungs.outcome.scans == 0 && fourth.rungs.outcome.moduleScans == 0 &&
                          fourth.rungs.searches == 0 && fourth.rungs.searchedAgain == 0 && fourth.report.Complete() &&
                          fourth.found.entry == RecordedModule::kEntryRva && fourth.found.option == kOptionRva &&
                          drive_cascade::CacheFileOpens() == opens;
    drive_cascade::Announce("TEST DRIVE", fourth);
    std::snprintf(what, sizeof(what), "%s: THE WHOLE RESULT HAS TAKEN ITS PLACE: install 4 verifies it in place with "
                                      "zero scans and no file", which);
    Check(verified && lines.size() == 1 &&
              Said("TEST DRIVE: as this session already resolved it in this module, with no search (address "
                   "resolution: 0 scans, ",
                   " ms): all 2 of its addresses verified again where they stand") == 1,
          what);

    drive_cascade::ForgetResolutions();
    drive_cascade::SetCacheFile(nullptr);
    DeleteFileW(cache.c_str());
    RemoveDirectoryW(folder.c_str());
}

// ---- what this process already resolved ---------------------------------------------------------------------------
// A drive is installed again whenever its title's core is. The second install of a WHOLE drive in the same
// module verifies what the first came to: no search, no file. A drive that is not whole -- a line missing that it
// can do without, or one it cannot (a refusal) -- is searched for again at its next three installs, and only a
// miss that those searches repeat is then said again without one.
uint32_t g_testGeneration = 1;
uint32_t TestGeneration(GameTitle) { return g_testGeneration; }

void TestRememberedInProcess()
{
    const uint32_t markerRva = Rva(g_marker);
    static const std::string marker = MarkerPattern();
    static std::string absent;   // the marker with its first byte changed: a signature that is nowhere in this image
    absent = marker;
    absent[0] = absent[0] == '0' ? '1' : '0';
    struct Parts
    {
        uint32_t marker = 0, option = 0, tls = 0;
    };
    using Installed = drive_cascade::Resolution<Parts>;
    enum class Kind
    {
        Whole,           // every line is in the image
        OptionMissing,   // one line more, which is not in the image and which the drive can do without
        Refused,         // its one signature is not in the image
    };
    // One install of the drive, in this process: nothing is forgotten first.
    const auto install = [&](Installed& out, Kind kind, uint32_t recorded) {
        return drive_cascade::Resolve(g_base, g_size, kHalo3, out, [&](drive_resolve::Resolver& r) {
            Parts f;
            f.marker = r.Unique("MARKER", recorded, kind == Kind::Refused ? absent.c_str() : marker.c_str());
            if (kind == Kind::OptionMissing)
                f.option = r.Unique("OPTION", recorded + 0x100, absent.c_str());
            f.tls = r.TlsIndex("TLS index", Rva(&_tls_index), Rva(&_tls_index));
            return f;
        });
    };
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring folder = std::wstring(temp) + L"halomccvr_drive_cascade_remembered_" +
                                std::to_wstring(GetCurrentProcessId()) + L"\\";
    CreateDirectoryW(folder.c_str(), nullptr);
    const std::wstring cache = folder + L"HaloMCCVR-drive-addresses.txt";
    DeleteFileW(cache.c_str());
    drive_cascade::SetCacheFolder(folder.c_str());
    g_config.drive_address_cache = 1;
    g_config.drive_fault = 0;
    drive_cascade::ForgetResolutions();

    // A WHOLE DRIVE is verified from what the process remembered from its second install on.
    {
        Installed first, second, third;
        Check(install(first, Kind::Whole, markerRva) && first.rungs.outcome.rung == Rung::Search &&
                  first.rungs.outcome.moduleScans == 1 && first.report.Complete(),
              "whole: the first install of the drive in this module is a search (1 scan)");
        const uint32_t opens = drive_cascade::CacheFileOpens();
        lines.clear();
        Check(install(second, Kind::Whole, markerRva) && install(third, Kind::Whole, markerRva) &&
                  second.rungs.outcome.rung == Rung::Remembered && second.rungs.outcome.scans == 0 &&
                  second.rungs.outcome.moduleScans == 0 && second.rungs.searchedAgain == 0 &&
                  second.rungs.searches == 0 && third.rungs.outcome.rung == Rung::Remembered &&
                  third.rungs.outcome.scans == 0 && third.rungs.outcome.moduleScans == 0,
              "whole: THE SECOND INSTALL OF THE SAME IMAGE DOES ZERO SCANS, and so does the third");
        Check(drive_cascade::CacheFileOpens() == opens && !second.rungs.outcome.savedLooked && !second.rungs.saved,
              "whole: and opens no file");
        Check(drive_known::SameReport(second.report, first.report) && second.found.marker == markerRva &&
                  second.found.tls == first.found.tls,
              "whole: it comes to what the first came to, line for line");
        drive_cascade::Announce("TEST DRIVE", second);
        Check(lines.size() == 1 &&
                  Said("TEST DRIVE: as this session already resolved it in this module, with no search (address "
                       "resolution: 0 scans, ",
                       " ms): all 2 of its addresses verified again where they stand") == 1,
              "whole: its line says so, with what this install's resolution cost");
    }

    // A RESULT THAT IS NOT WHOLE IS NOT BELIEVED FROM ONE SEARCH: one search can miss a line for a reason that was
    // true only then. Installs 2, 3 and 4 search again; install 5 does not, and from there the miss is repeated.
    struct NotWhole
    {
        Kind kind;
        const char* name;
        uint32_t scans;         // searches of the whole module at each search of it
        const char* searched;   // what a search's line says after its cost
        const char* repeated;   // what the line says once it is no longer searched for
    };
    const NotWhole notWhole[] = {
        {Kind::OptionMissing, "an optional line missing", 2,
         " ms): of its 3 addresses 2 are where the tables record them, 0 MOVED, 0 were taken at the recorded address "
         "among several matches, 1 were NOT found: OPTION NOT FOUND",
         " ms): 2 of its 3 addresses verified again where they stand, 1 were NOT found by its 4 searches of this "
         "module and are not searched again in this module: OPTION NOT FOUND"},
        {Kind::Refused, "refused", 1,
         " ms): of its 2 addresses 1 are where the tables record them, 0 MOVED, 0 were taken at the recorded address "
         "among several matches, 1 were NOT found: MARKER NOT FOUND",
         " ms): 1 of its 2 addresses verified again where they stand, 1 were NOT found by its 4 searches of this "
         "module and are not searched again in this module: MARKER NOT FOUND"},
    };
    for (const NotWhole& c : notWhole)
    {
        Installed first;
        char what[260], expect[120];
        lines.clear();
        const bool firstOk = install(first, c.kind, markerRva) && first.rungs.outcome.rung == Rung::Search &&
                             first.rungs.outcome.moduleScans == c.scans && !first.report.Complete() &&
                             first.rungs.searchedAgain == 0 && first.rungs.searches == 0;
        drive_cascade::Announce("TEST DRIVE", first);
        std::snprintf(expect, sizeof(expect), "searched (address resolution: %u scans, ", c.scans);
        std::snprintf(what, sizeof(what), "%s: the first install is a search (%u scans), and says \"searched\"",
                      c.name, c.scans);
        Check(firstOk && lines.size() == 1 && Said(expect, c.searched) == 1, what);

        const uint32_t opens = drive_cascade::CacheFileOpens();
        bool againOk = true;
        for (uint32_t n = 1; n <= drive_known::kFreshSearches; ++n)
        {
            Installed again;
            lines.clear();
            const bool ok = install(again, c.kind, markerRva) && again.rungs.outcome.rung == Rung::Search &&
                            !again.rungs.outcome.rememberedTried && again.rungs.outcome.moduleScans == c.scans &&
                            again.rungs.searchedAgain == n && again.rungs.searches == 0 &&
                            drive_known::SameReport(again.report, first.report);
            drive_cascade::Announce("TEST DRIVE", again);
            std::snprintf(expect, sizeof(expect), "TEST DRIVE: searched again, %u of 3 (address resolution: %u scans, ",
                          n, c.scans);
            againOk = againOk && ok && lines.size() == 1 && Said(expect, c.searched) == 1;
        }
        std::snprintf(what, sizeof(what), "%s: INSTALLS 2, 3 AND 4 SEARCH AGAIN (%u scans each), and each line says "
                                          "which of the three it was", c.name, c.scans);
        Check(againOk, what);
        std::snprintf(what, sizeof(what), "%s: those searches do not open the cache's file: it holds nothing for a "
                                          "drive that was never whole", c.name);
        Check(drive_cascade::CacheFileOpens() == opens, what);

        Installed fifth, sixth;
        lines.clear();
        const bool repeated = install(fifth, c.kind, markerRva) && install(sixth, c.kind, markerRva) &&
                              fifth.rungs.outcome.rung == Rung::Remembered && fifth.rungs.outcome.scans == 0 &&
                              fifth.rungs.outcome.moduleScans == 0 && fifth.rungs.searches == 4 &&
                              fifth.rungs.searchedAgain == 0 && sixth.rungs.outcome.rung == Rung::Remembered &&
                              sixth.rungs.outcome.scans == 0 && sixth.rungs.searches == 4 &&
                              drive_known::SameReport(fifth.report, first.report) &&
                              (c.kind == Kind::Refused ? fifth.found.marker == 0 : fifth.found.marker == markerRva) &&
                              drive_cascade::CacheFileOpens() == opens;
        std::snprintf(what, sizeof(what), "%s: INSTALL 5 DOES ZERO SCANS, and so does the one after: a miss that four "
                                          "searches repeat is the module's own", c.name);
        Check(repeated, what);
        char firstLine[200], fifthLine[200];
        drive_cascade::FirstFailure(first.report, firstLine, sizeof(firstLine));
        drive_cascade::FirstFailure(fifth.report, fifthLine, sizeof(fifthLine));
        drive_cascade::Announce("TEST DRIVE", fifth);
        std::snprintf(what, sizeof(what), "%s: its line says it is not searched again in this module, and names again "
                                          "what was not found", c.name);
        Check(lines.size() == 1 && !std::strcmp(firstLine, fifthLine) &&
                  Said("TEST DRIVE: as this session already resolved it in this module, with no search (address "
                       "resolution: 0 scans, ",
                       c.repeated) == 1,
              what);
    }

    // NOT STICKY. What is remembered is of one module instance; anything else resolves the drive afresh.
    {
        Installed r, after;
        // The cases above left a refusal that is no longer searched for. MCC unloads the DLL and loads it again
        // at the same base: the title's module generation changes, and the drive is searched for as for the
        // first time, with its three fresh searches before it.
        drive_cascade::SetModuleGenerationSource(&TestGeneration);
        Check(install(r, Kind::Refused, markerRva) && r.rungs.outcome.rung == Rung::Search &&
                  !r.rungs.outcome.rememberedTried && r.rungs.searchedAgain == 0,
              "a module generation where none was known before: another module instance, searched as a first time");
        Check(install(r, Kind::Refused, markerRva) && r.rungs.outcome.rung == Rung::Search &&
                  r.rungs.searchedAgain == 1,
              "the same generation again: the first of its three fresh searches");
        ++g_testGeneration;
        Check(install(r, Kind::Refused, markerRva) && r.rungs.outcome.rung == Rung::Search &&
                  !r.rungs.outcome.rememberedTried && r.rungs.outcome.moduleScans == 1 && !r.found.marker &&
                  r.rungs.searchedAgain == 0,
              "A REFUSAL IS NOT CARRIED OVER A RELOAD OF THE DLL: the same base and build, another generation, and "
              "the drive is searched for again, its count of searches begun anew");
        Check(install(after, Kind::Refused, markerRva) && after.rungs.outcome.rung == Rung::Search &&
                  after.rungs.searchedAgain == 1,
              "and searched again from there, as any result that is not whole");
        drive_cascade::SetModuleGenerationSource(nullptr);
        g_testGeneration = 1;
        drive_cascade::ForgetResolutions();

        // Another plan (another version of the drive's tables: the marker recorded elsewhere).
        Check(install(r, Kind::Whole, markerRva) && r.rungs.outcome.rung != Rung::Remembered &&
                  install(r, Kind::Whole, markerRva) && r.rungs.outcome.rung == Rung::Remembered,
              "a whole drive: resolved once (from the file the cases above left), then remembered");
        Check(install(r, Kind::Whole, markerRva + 0x10) && r.rungs.outcome.rememberedTried &&
                  r.rungs.outcome.rung != Rung::Remembered && r.found.marker == markerRva,
              "a plan that asks for other lines than were remembered: resolved afresh");
        // Another module at another base, for the same drive.
        RecordedModule elsewhere;
        drive_cascade::Resolution<RecordedModule::Found> there;
        const auto onOther = [&](drive_cascade::Resolution<RecordedModule::Found>& out) {
            return drive_cascade::Resolve(elsewhere.base, elsewhere.size, kHalo3, out,
                                          [](drive_resolve::Resolver& q) { return RecordedModule::Plan(q); });
        };
        Check(elsewhere.base && onOther(there) && !there.rungs.outcome.rememberedTried &&
                  there.rungs.outcome.rung == Rung::Search && there.found.entry == RecordedModule::kEntryRva,
              "the same drive in a module at another base: nothing remembered of it there, searched");
        Check(onOther(there) && there.rungs.outcome.rung == Rung::Remembered && there.rungs.outcome.scans == 0,
              "and remembered there from then on");
        // The same base, another build stamp (the module's PE timestamp one higher, as a patched DLL loaded where
        // the old one stood): what was remembered is of another build, and is dropped.
        uint32_t stamp = 0;
        std::memcpy(&stamp, elsewhere.image + 0x80 + 8, sizeof(stamp));
        ++stamp;
        std::memcpy(elsewhere.image + 0x80 + 8, &stamp, sizeof(stamp));
        Check(onOther(there) && !there.rungs.outcome.rememberedTried && there.rungs.outcome.rung != Rung::Remembered &&
                  there.found.entry == RecordedModule::kEntryRva,
              "the same base with another build stamp: nothing remembered of the old build is used");
        Check(onOther(there) && there.rungs.outcome.rung == Rung::Remembered,
              "and the new build's result is remembered in its turn");
        // A module that does not open leaves nothing remembered for the drive.
        Installed none;
        Check(!drive_cascade::Resolve(uintptr_t{0}, g_size, kHalo3, none,
                                      [](drive_resolve::Resolver&) { return Parts{}; }) &&
                  onOther(there) && !there.rungs.outcome.rememberedTried &&
                  there.rungs.outcome.rung != Rung::Remembered && there.found.entry == RecordedModule::kEntryRva,
              "a module that did not open: what was remembered for the drive is dropped, and the next install "
              "starts from the other rungs");
    }

    // WITH THE CACHE OFF it is the only thing that keeps a second install from searching.
    {
        drive_cascade::ForgetResolutions();
        g_config.drive_address_cache = 0;
        Installed first, second;
        const uint32_t opens = drive_cascade::CacheFileOpens();
        Check(install(first, Kind::Whole, markerRva + 0x20) && first.rungs.outcome.rung == Rung::Search &&
                  first.rungs.notSaved && install(second, Kind::Whole, markerRva + 0x20) &&
                  second.rungs.outcome.rung == Rung::Remembered && second.rungs.outcome.scans == 0 &&
                  drive_cascade::CacheFileOpens() == opens,
              "drive_address_cache = 0: nothing is saved, no file is opened, and the second install still does zero "
              "scans");
        g_config.drive_address_cache = 1;
    }

    // THE DEV KEY: while any bit is set, what is remembered is neither used nor replaced.
    {
        drive_cascade::ForgetResolutions();
        Installed healthy, forced, after;
        Check(install(healthy, Kind::Whole, markerRva) && install(healthy, Kind::Whole, markerRva) &&
                  healthy.rungs.outcome.rung == Rung::Remembered,
              "a whole drive, remembered");
        g_config.drive_fault = 0x10;
        const bool resolved = install(forced, Kind::Whole, markerRva);
        g_config.drive_fault = 0;
        const bool then = install(after, Kind::Whole, markerRva);
#if HALOMCCVR_DEV_DIAGNOSTICS
        Check(resolved && forced.rungs.outcome.rung == Rung::Search && !forced.rungs.outcome.rememberedTried &&
                  forced.rungs.outcome.moduleScans == 1,
              "dev build, drive_fault 0x10: what was remembered is not used, and the drive is searched");
        Check(then && after.rungs.outcome.rung == Rung::Remembered && after.rungs.outcome.scans == 0,
              "dev build: and with the key cleared what was remembered before it still serves");
#else
        Check(resolved && forced.rungs.outcome.rung == Rung::Remembered && then &&
                  after.rungs.outcome.rung == Rung::Remembered,
              "player build: the dev key does nothing, and the drive is verified from what was remembered");
#endif
    }

    drive_cascade::ForgetResolutions();
    drive_cascade::SetCacheFile(nullptr);
    DeleteFileW(cache.c_str());
    DeleteFileW((cache + L".tmp").c_str());
    RemoveDirectoryW(folder.c_str());
}

#if HALOMCCVR_DEV_DIAGNOSTICS
// ---- the cache's lock is released whatever happens under it ------------------------------------------------------
// The cache is asked from inside the rungs, which run under a structured-exception guard: an exception that left
// the cache's code with its lock held would be swallowed there, and the next resolution of any drive would wait
// on that lock for ever. A dev build's probe raises one, with the lock held, at the open the test chooses.
int g_probeCountdown = 0;   // the probe raises at this open from now (0: never)
bool g_probeRaised = false;
void RaisingProbe()
{
    if (g_probeCountdown && --g_probeCountdown == 0)
    {
        g_probeRaised = true;
        RaiseException(0xE0C0FFEE, 0, 0, nullptr);
    }
}

Resolved g_lockResolved;
bool g_lockResolvedOk = false;
void ResolveForLockTest() { g_lockResolvedOk = Resolve(g_lockResolved, kHalo3, Rva(g_marker)); }

// Runs `call` under a guard of the test's own. (Nothing in this function has a destructor.)
bool Faulted(void (*call)())
{
    __try
    {
        call();
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return true;
    }
}

void TestCacheLockReleased()
{
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring folder = std::wstring(temp) + L"halomccvr_drive_cascade_lock_" +
                                std::to_wstring(GetCurrentProcessId()) + L"\\";
    CreateDirectoryW(folder.c_str(), nullptr);
    const std::wstring cache = folder + L"HaloMCCVR-drive-addresses.txt";
    DeleteFileW(cache.c_str());
    drive_cascade::SetCacheFolder(folder.c_str());
    g_config.drive_address_cache = 1;
    g_config.drive_fault = 0;
    drive_cascade::SetCacheProbe(&RaisingProbe);
    Check(!drive_cascade::CacheLockHeld(), "the cache's lock is free to begin with");

    // AN EXCEPTION WHILE THE CACHE IS ASKED, inside the rungs: their guard swallows it and the drive is not
    // resolved -- and the lock must not have stayed held.
    g_probeRaised = false;
    g_probeCountdown = 1;   // the first open: the look-up's read
    const bool faulted = Faulted(&ResolveForLockTest);
    Check(g_probeRaised && !faulted && !g_lockResolvedOk && g_lockResolved.why != nullptr,
          "an exception while the cache is asked is swallowed by the resolution's guard: the drive is not resolved");
    const bool heldAfterAsk = drive_cascade::CacheLockHeld();
    Check(!heldAfterAsk, "THE CACHE'S LOCK IS NOT LEFT HELD by an exception raised while it was asked");
    if (heldAfterAsk)
    {
        // The next resolution would wait for ever. Leave the rest be.
        drive_cascade::SetCacheProbe(nullptr);
        return;
    }
    Check(Resolve(g_lockResolved, kHalo3, Rva(g_marker)) && Whole(g_lockResolved) && g_lockResolved.rungs.saved,
          "the next resolution is not kept waiting: it asks the cache, searches and saves");

    // AN EXCEPTION WHILE THE FILE IS BEING SAVED (outside the rungs' guard: it reaches the caller).
    DeleteFileW(cache.c_str());
    g_probeRaised = false;
    g_probeCountdown = 2;   // the second open: the read that a save begins with
    const bool saveFaulted = Faulted(&ResolveForLockTest);
    const bool heldAfterSave = drive_cascade::CacheLockHeld();
    Check(g_probeRaised && saveFaulted && !heldAfterSave,
          "nor by one raised while the file was being saved, which goes on to the caller");
    drive_cascade::SetCacheProbe(nullptr);
    g_probeCountdown = 0;
    if (!heldAfterSave)
        Check(Resolve(g_lockResolved, kHalo3, Rva(g_marker)) && Whole(g_lockResolved) && g_lockResolved.rungs.saved,
              "and the resolution after that saves as ever");

    drive_cascade::SetCacheFile(nullptr);
    DeleteFileW(cache.c_str());
    DeleteFileW((cache + L".tmp").c_str());
    RemoveDirectoryW(folder.c_str());
}
#endif

// ---- a plan that asks for more lines than a report holds --------------------------------------------------------
// Refused through the DLL's side as a module that does not open is: not resolved, with its own reason, nothing
// found, nothing remembered, nothing saved.
void TestPlanTooLongIsRefused()
{
    const uint32_t markerRva = Rva(g_marker);
    static const std::string marker = MarkerPattern();
    struct Count
    {
        uint32_t found = 0;
    };
    const auto resolve = [&](drive_cascade::Resolution<Count>& out, uint32_t asked) {
        return drive_cascade::Resolve(g_base, g_size, kHalo3, out, [&](drive_resolve::Resolver& r) {
            Count f;
            for (uint32_t i = 0; i < asked; ++i)
                f.found += r.Unique("MARKER", markerRva, marker.c_str()) ? 1u : 0u;
            return f;
        });
    };
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring folder = std::wstring(temp) + L"halomccvr_drive_cascade_toolong_" +
                                std::to_wstring(GetCurrentProcessId()) + L"\\";
    CreateDirectoryW(folder.c_str(), nullptr);
    const std::wstring cache = folder + L"HaloMCCVR-drive-addresses.txt";
    DeleteFileW(cache.c_str());
    drive_cascade::SetCacheFolder(folder.c_str());
    g_config.drive_address_cache = 1;
    g_config.drive_fault = 0;
    drive_cascade::ForgetResolutions();

    drive_cascade::Resolution<Count> tooLong, again;
    tooLong.found.found = 4242;
    Check(!resolve(tooLong, drive_resolve::kMaxLines + 1) && tooLong.why == drive_known::kTooManyLines &&
              tooLong.report.count == 0 && tooLong.found.found == 4242 && !tooLong.rungs.saved && !Exists(cache),
          "one line more than a report holds: the drive is not resolved, the reason is its own, and nothing of it is "
          "found or saved");
    Check(!resolve(again, drive_resolve::kMaxLines + 1) && again.why == drive_known::kTooManyLines,
          "the next install is refused the same way");
    drive_cascade::Resolution<Count> fits;
    Check(resolve(fits, drive_resolve::kMaxLines) && !fits.why && !fits.rungs.outcome.rememberedTried &&
              fits.rungs.outcome.rung == Rung::Search,
          "nothing of the refused plan was remembered: the same drive with a plan that fits starts from a search");
    Check(fits.found.found == drive_resolve::kMaxLines && fits.report.Complete() && fits.rungs.saved,
          "a plan of exactly as many lines as a report holds resolves, and is saved");

    drive_cascade::ForgetResolutions();
    drive_cascade::SetCacheFile(nullptr);
    DeleteFileW(cache.c_str());
    RemoveDirectoryW(folder.c_str());
}

// The cold worker passes over a watch about every 50 ms.
struct Worker
{
    drive_cascade::HookWatch watch;
    uint64_t now = 1000000;
    // Passes for `ms`, returns how many times the watch fired. `install`: 0 for no hook.
    int Run(uint64_t install, bool possible, bool called, uint32_t ms)
    {
        int fired = 0;
        for (uint32_t t = 0; t < ms; t += 50)
        {
            now += 50;
            fired += watch.Pass(install, possible, called, now) ? 1 : 0;
        }
        return fired;
    }
};

void TestHookWatch()
{
    const int somewhere = 0;
    const uint64_t hookA = drive_cascade::HookWatch::Install(&somewhere, 1);
    // The same address, installed again: another install.
    const uint64_t hookB = drive_cascade::HookWatch::Install(&somewhere, 2);
    Check(hookA && hookB && hookA != hookB && !drive_cascade::HookWatch::Install(nullptr, 3),
          "an install is its address and its number; no hook is none");
    const uint32_t limitMs = drive_cascade::HookWatch::kPeriods * drive_cascade::HookWatch::kPeriodMs;
    {
        Worker w;
        Check(w.Run(0, true, false, 60000) == 0, "no hook: nothing to say");
    }
    {
        Worker w;
        Check(w.Run(hookA, true, false, limitMs - 200) == 0, "installed, possible, uncalled: silent before the limit");
        Check(w.Run(hookA, true, false, 400) == 1, "and said exactly once when the limit is reached");
        Check(w.Run(hookA, true, false, 60000) == 0, "and never again for that install");
        Check(w.Run(hookB, true, false, limitMs + 200) == 1,
              "the same hook installed again is a new install, and is watched afresh");
    }
    {
        Worker w;
        Check(w.Run(hookA, false, false, 600000) == 0, "ten minutes of a menu, a load or a cutscene count for nothing");
        Check(w.Run(hookA, true, false, limitMs - 200) == 0, "and leave nothing behind for the play that follows");
        Check(w.Run(hookA, true, false, 400) == 1, "which is counted from its own beginning");
    }
    {
        Worker w;
        int fired = 0;
        // Play broken by a cutscene every eight seconds: never ten seconds of possible time in a row.
        for (int i = 0; i < 20; ++i)
        {
            fired += w.Run(hookA, true, false, 8000);
            fired += w.Run(hookA, false, false, 2100);
        }
        Check(fired == 0, "possible time is counted only in an unbroken run");
        // One pass in a period that found the call impossible spoils that period.
        Worker one;
        fired = one.Run(hookA, true, false, 7900);
        fired += one.watch.Pass(hookA, false, false, one.now += 50) ? 1 : 0;
        fired += one.Run(hookA, true, false, 6000);
        Check(fired == 0, "a single pass that found the call impossible restarts the count");
    }
    {
        Worker w;
        Check(w.Run(hookA, true, false, 4000) == 0 && w.Run(hookA, true, true, 50) == 0 && w.watch.everCalled(),
              "one call is seen");
        Check(w.Run(hookA, true, false, 600000) == 0, "a hook that was called once is never called dead");
        // Installed again a moment later: the call belonged to the hook before it. (The drive clears its counters
        // when it installs, so `called` is false for the new one.)
        Check(w.Run(hookB, true, false, limitMs + 200) == 1,
              "an install that follows a called one is watched, and is said when it is never called");
    }
    {
        // A stalled worker: two passes a minute apart are one period, not thirty.
        Worker w;
        int fired = w.watch.Pass(hookA, true, false, w.now) ? 1 : 0;
        fired += w.watch.Pass(hookA, true, false, w.now += 60000) ? 1 : 0;
        Check(fired == 0, "a worker that stalled for a minute counted one period");
    }
    {
        // The dev key's bit 4: the watchdog is told the hook never ran.
        addrcascade::set_fault_mask(0x10);
        Worker w;
        const int fired = w.Run(hookA, true, true, limitMs + 200);
#if HALOMCCVR_DEV_DIAGNOSTICS
        Check(fired == 1, "dev build, drive_fault 0x10: a hook that IS called is called dead, once");
        lines.clear();
        drive_cascade::SayNeverCalled("TEST DRIVE", "the control update", 0x1234, "the player was in control");
        Check(Lines("TEST DRIVE: INSTALLED BUT NEVER CALLED. The hook on the control update at +0x1234 has stood "
                    "through 10 s in which the player was in control") == 1 &&
                  Lines("DEV fault injection (drive_fault 0x10) told that watchdog") == 1,
              "and its line is followed by one saying the fault was forced");
#else
        Check(fired == 0, "player build: the fault mask does nothing to a watchdog");
        lines.clear();
        drive_cascade::SayNeverCalled("TEST DRIVE", "the control update", 0x1234, "the player was in control");
        Check(lines.size() == 1 &&
                  Lines("TEST DRIVE: INSTALLED BUT NEVER CALLED. The hook on the control update at +0x1234 has stood "
                        "through 10 s in which the player was in control") == 1,
              "the watchdog's one line, as formatted");
#endif
        addrcascade::set_fault_mask(0);
    }
}

// A title's hooks watched as one set (drive_cascade::HookWatchBoard), stepped as the cold worker steps it: Begin,
// one Pass per watch, then SummaryDue, every 50 ms.
struct BoardWorker
{
    drive_cascade::HookWatchBoard board;
    uint64_t now = 1000000;
    // Per watch: where its hook is (null: none), whether its call is possible, whether it is called.
    struct Hook { const void* target; bool possible; bool called; };
    int fired = 0, summaries = 0;
    void Run(uint32_t install, const Hook* hooks, size_t count, bool inPlay, uint32_t ms)
    {
        for (uint32_t t = 0; t < ms; t += 50)
        {
            now += 50;
            board.Begin(install);
            for (size_t i = 0; i < count; ++i)
                fired += board.Pass(i, hooks[i].target, hooks[i].possible, hooks[i].called, now) ? 1 : 0;
            summaries += board.SummaryDue(inPlay, now) ? 1 : 0;
        }
    }
    std::string Summary(const char* const* names, size_t count) const
    {
        char text[512];
        board.Summary("Test title", names, count, text, sizeof(text));
        return text;
    }
};

void TestHookWatchBoard()
{
    using Verdict = drive_cascade::HookWatchBoard::Verdict;
    static const int a = 0, b = 0, c = 0;
    const char* const names[]{"alpha", "beta", "gamma"};
    const uint32_t limitMs = drive_cascade::HookWatch::kPeriods * drive_cascade::HookWatch::kPeriodMs;
    {
        BoardWorker w;
        const BoardWorker::Hook hooks[]{{&a, true, false}, {&b, true, false}};
        w.Run(0, hooks, 2, true, 120000);
        Check(w.fired == 0 && w.summaries == 0 && w.board.VerdictOf(0) == Verdict::NotInstalled,
              "board: no install of the set, nothing is watched and nothing is said");
    }
    {
        BoardWorker w;
        const BoardWorker::Hook hooks[]{{&a, true, true}, {&b, true, true}};
        w.Run(1, hooks, 2, true, 100);
        Check(w.summaries == 1 && w.Summary(names, 2) == "Test title hook watch: 2 watched, all called",
              "board: every hook called, the one summary at once");
        w.Run(1, hooks, 2, true, 120000);
        Check(w.summaries == 1 && w.fired == 0, "board: and never again for that install");
    }
    {
        BoardWorker w;
        const BoardWorker::Hook hooks[]{{&a, true, true}, {&b, true, false}, {nullptr, true, false}};
        w.Run(1, hooks, 3, true, limitMs - 200);
        Check(w.fired == 0 && w.summaries == 0 && w.board.VerdictOf(1) == Verdict::Waiting,
              "board: a possible, uncalled hook waits until its limit");
        w.Run(1, hooks, 3, true, 400);
        Check(w.fired == 1 && w.board.VerdictOf(1) == Verdict::NeverCalled && w.summaries == 1,
              "board: it fires once, and that settles the set: the summary follows in the same pass");
        Check(w.Summary(names, 3) == "Test title hook watch: 2 watched: 1 called; 1 NEVER CALLED: beta",
              "board: the summary names it; a hook that is not installed is not counted");
        // The set installed again: everything starts over, the one summary included.
        w.Run(2, hooks, 3, true, limitMs + 200);
        Check(w.fired == 2 && w.summaries == 2, "board: a new install of the set is watched and summed up afresh");
    }
    {
        // A hook whose state never comes (a seat, in a level without vehicles): the summary waits for a minute of
        // play, and says the hook is still waiting.
        BoardWorker w;
        const BoardWorker::Hook hooks[]{{&a, true, true}, {&b, false, false}};
        w.Run(1, hooks, 2, false, 600000);
        Check(w.summaries == 0, "board: ten minutes outside play do not count toward the summary");
        w.Run(1, hooks, 2, true, static_cast<uint32_t>(drive_cascade::HookWatchBoard::kSummaryAfterMs) - 500);
        Check(w.summaries == 0, "board: nor does less than a minute of play");
        w.Run(1, hooks, 2, true, 1000);
        Check(w.summaries == 1 && w.fired == 0 &&
                  w.Summary(names, 2) ==
                      "Test title hook watch: 2 watched: 1 called; 1 not yet in the state that calls them: beta",
              "board: after a minute of play the summary says which hook still waits for its state");
    }
    {
        // A stalled worker: two passes a minute apart are one period of play, not a minute.
        BoardWorker w;
        const BoardWorker::Hook hooks[]{{&a, false, false}};
        w.Run(1, hooks, 1, true, 50);
        w.now += 60000;
        w.board.Begin(1);
        (void)w.board.Pass(0, &a, false, false, w.now);
        Check(!w.board.SummaryDue(true, w.now), "board: a worker that stalled for a minute counted one period");
    }
    {
        // A hook that goes away and comes back within one install of the set (one installed only while wanted):
        // its verdict goes with it, and it is watched afresh.
        BoardWorker w;
        BoardWorker::Hook hooks[]{{&a, true, true}, {&c, true, false}};
        w.Run(1, hooks, 2, true, limitMs + 200);
        Check(w.fired == 1 && w.board.VerdictOf(1) == Verdict::NeverCalled, "board: the on-demand hook fired");
        hooks[1].target = nullptr;
        w.Run(1, hooks, 2, true, 100);
        Check(w.board.VerdictOf(1) == Verdict::NotInstalled, "board: gone, it is not installed");
        hooks[1] = {&c, true, true};
        w.Run(1, hooks, 2, true, 100);
        Check(w.board.VerdictOf(1) == Verdict::Called && w.fired == 1, "board: back and called, it is called");
    }
    {
        char tiny[24];
        drive_cascade::HookWatchBoard board;
        board.Summary("A very long title name", names, 3, tiny, sizeof(tiny));
        Check(std::strlen(tiny) == sizeof(tiny) - 1, "board: a summary longer than its buffer is cut, terminated");
    }
}
} // namespace

int main(int argc, char** argv)
{
    printLines = argc > 1 && !std::strcmp(argv[1], "--print");
    t_touched = 1;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(g_base + dos->e_lfanew);
    g_size = nt->OptionalHeader.SizeOfImage;
    Check(t_touched == 1 && nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress != 0,
          "this image has a PE TLS directory");

    TestOpenAndResolve();
    TestAnnounceAndFaults();
    TestCacheFile();
    TestUnreadCacheFileIsNotReplaced();
    TestInstallProofsNeverFault();
    TestUnreadableFunctionTable();
    TestRecordedBuildOpensNoFile();
    TestRememberedInProcess();
    TestSearchedAgainUntilWhole(false);
    TestSearchedAgainUntilWhole(true);
    TestPlanTooLongIsRefused();
#if HALOMCCVR_DEV_DIAGNOSTICS
    TestCacheLockReleased();
#endif
    TestHookWatch();
    TestHookWatchBoard();

    std::printf("drive_cascade_tests (%s build): %u checks, %u failures\n",
                HALOMCCVR_DEV_DIAGNOSTICS ? "dev" : "player", checks, failures);
    return failures ? 1 : 0;
}
