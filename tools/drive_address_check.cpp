// THE OFFLINE CHECK OF THE DIRECT DRIVES' ADDRESSES, against the game's own DLL files. No game, no headset.
//
//   halomccvr_drive_address_check --mcc "<the MCC install folder>" [--faults] [--moved] [--title <dll name>] [--brief]
//                                 [--loaded <a scratch folder>]
//
// BOTH BUILDS' PLANS. A player build of the mod and a dev build do not ask the same of a control update: the dev
// build's plan goes on to two tick anchors, and a dev build has two pilot observers besides. Those are different
// versions of a drive's tables (another plan hash, another count of addresses), so everything below is done
// twice: for the player build's plans, which are what ships, and for the dev build's.
//
// For each game DLL it lays the file out as an image (headers and sections at their RVAs, base relocations
// applied, as the loader would), and runs every direct drive's own resolution on it: the same tables and the same
// code the DLL runs at install (src/common/drive_sites.h, src/common/drive_resolve.h). Per site it prints the
// recorded RVA, the RVA found, how many places the signature matched, how it was found and the verdict.
//
// ACCEPTANCE. On the build the tables were measured on, every site must resolve "as recorded": the resolution
// then hooks and reads exactly what the recorded addresses named. Any other verdict fails the check (exit 1).
// On another build the same table says what moved and what was lost; --any-build prints it without failing.
//
// THE RUNGS (src/common/drive_known.h). That table is the SEARCH's, and its match counts are what make the known
// addresses safe to verify without counting: it is printed first. Then each drive is resolved as the DLL resolves
// it, the known addresses first: on the recorded build that must be rung 1 at the recorded addresses, with no
// scan of the module, choosing the very addresses the search chose; the time each takes is printed.
//
// --faults repeats the whole resolution with each fault forced (drive_resolve.h, kFault...), and checks what each
// must do: a signature that finds nothing leaves the drive unhooked; an ambiguous one is settled by the recorded
// address; a wrong record is overruled by the search (the same addresses, reported as moved); ambiguous with a
// wrong record is refused; a TLS directory that disagrees stops every drive that reads the TLS index; an anchor
// denied its offset is found as the one match in its function, or not at all where it has two.
//
// --moved builds a second image of each DLL in which every section sits 0x40 bytes higher -- the same code after
// something was inserted ahead of it, so that no recorded RVA is right -- and checks that every drive follows it:
// every site found 0x40 above where it was, reported moved, and the drive hookable there. It is resolved twice,
// as two launches would: the first finds the recorded addresses do not verify, searches, and saves what it found
// (through the address cache's own text); the second verifies those saved addresses with no scan at all. With
// --loaded as well, the same two launches are made through the DLL's own side and a REAL FILE in the scratch
// folder: written by the first, read back by the second, left as it was by the second.
//
// --loaded <folder> repeats the healthy resolution on each DLL AS THE WINDOWS LOADER MAPS IT, through the DLL's own
// side of the resolution (src/dll/drive_cascade.cpp: the module opened by VirtualQuery, its .pdata where the
// loader put it, its TLS directory as relocated). A copy of the file is made in that folder and mapped with
// DONT_RESOLVE_DLL_REFERENCES: sections placed and relocations applied, no import resolved, none of its code run.
// Every drive must resolve exactly as it did from the file, and at every site the function-table entry the
// resolution read must be the one the operating system's own lookup answers with. The install's own decision is
// then made on it (src/dll/drive_install.h), and the line the install logs is said by the DLL's own function
// under the DLL's own name for the drive. A cache file is named for all of it, and on the recorded build must
// never come to exist, nor be opened. Each mapped DLL is then resolved once more as a build that is not the
// recorded one would go (for no title, so that nothing is known of it): the first install searches it, and the
// same process installing the drive again must verify what it remembered with no scan; both are timed. The copy,
// and every file this check wrote, is deleted afterwards.
//
// The MCC folder comes from --mcc or the HALOMCCVR_MCC_DIR environment variable. With neither, the check is
// SKIPPED (exit 77), so a machine without the game does not fail its tests -- and has checked nothing: ctest
// counts a skipped check among those that passed. ctest's own run is --moved --faults --brief, never --loaded.
// The DLL files are only read.
#include <Windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "addrcascade/AddressCascade.hpp"
#include "drive_sites.h"
#include "title_build_identity.h"
#include "config.h"
#include "drive_known.h"
#include "../src/dll/drive_cascade.h"
#include "../src/dll/drive_install.h"

// The DLL's side of the resolution logs through this (--loaded): each line is printed, and kept where a list is
// named for it.
static std::vector<std::string>* g_capture = nullptr;

void Logf(const char* format, ...)
{
    char text[2048];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    std::printf("      | %s\n", text);
    if (g_capture)
        g_capture->emplace_back(text);
}

namespace
{
using drive_resolve::Report;
using drive_resolve::Resolver;
using drive_resolve::Verdict;

constexpr int kExitSkipped = 77;

struct Loaded
{
    uint8_t* base = nullptr;
    size_t size = 0;
    uint32_t timestamp = 0;
    drive_resolve::Image image{};
};

// Lays a PE32+ DLL file out as the loader maps it: headers, each section at its RVA, base relocations applied for
// where the copy lives. Imports are not bound (nothing here reads them).
bool LoadAsImage(const std::wstring& path, Loaded& out, std::string& why)
{
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        why = "the file could not be opened";
        return false;
    }
    LARGE_INTEGER fileSize{};
    std::vector<uint8_t> bytes;
    bool read = GetFileSizeEx(file, &fileSize) && fileSize.QuadPart > 0x400 && fileSize.QuadPart < 0x40000000;
    if (read)
    {
        bytes.resize(static_cast<size_t>(fileSize.QuadPart));
        size_t done = 0;
        while (read && done < bytes.size())
        {
            DWORD got = 0;
            const DWORD want = static_cast<DWORD>((std::min)(bytes.size() - done, static_cast<size_t>(1u << 24)));
            read = ReadFile(file, bytes.data() + done, want, &got, nullptr) && got != 0;
            done += got;
        }
    }
    CloseHandle(file);
    if (!read)
    {
        why = "the file could not be read";
        return false;
    }
    const auto u16 = [&bytes](size_t at) { uint16_t v = 0; std::memcpy(&v, bytes.data() + at, 2); return v; };
    const auto u32 = [&bytes](size_t at) { uint32_t v = 0; std::memcpy(&v, bytes.data() + at, 4); return v; };
    const auto u64 = [&bytes](size_t at) { uint64_t v = 0; std::memcpy(&v, bytes.data() + at, 8); return v; };
    const size_t pe = u32(0x3C);
    if (u16(0) != 0x5A4D || pe + 0x108 > bytes.size() || u32(pe) != 0x00004550u || u16(pe + 4) != 0x8664 ||
        u16(pe + 24) != 0x20B)
    {
        why = "not an x64 PE32+ file";
        return false;
    }
    const size_t optional = pe + 24;
    const uint32_t sections = u16(pe + 6);
    const size_t table = optional + u16(pe + 20);
    const uint64_t preferredBase = u64(optional + 24);
    const uint32_t sizeOfImage = u32(optional + 56), sizeOfHeaders = u32(optional + 60);
    if (!sizeOfImage || sizeOfHeaders > bytes.size() || sizeOfHeaders > sizeOfImage ||
        table + static_cast<size_t>(sections) * 40 > bytes.size())
    {
        why = "its headers do not fit the file";
        return false;
    }
    auto* image = static_cast<uint8_t*>(VirtualAlloc(nullptr, sizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!image)
    {
        why = "no memory for the image";
        return false;
    }
    std::memcpy(image, bytes.data(), sizeOfHeaders);
    for (uint32_t i = 0; i < sections; ++i)
    {
        const size_t at = table + static_cast<size_t>(i) * 40;
        const uint32_t virtualSize = u32(at + 8), rva = u32(at + 12), rawSize = u32(at + 16), raw = u32(at + 20);
        const uint32_t copy = virtualSize ? (std::min)(virtualSize, rawSize) : rawSize;
        if (!copy)
            continue;
        if (rva > sizeOfImage || copy > sizeOfImage - rva || raw > bytes.size() || copy > bytes.size() - raw)
        {
            VirtualFree(image, 0, MEM_RELEASE);
            why = "a section does not fit the file or the image";
            return false;
        }
        std::memcpy(image + rva, bytes.data() + raw, copy);
    }
    // Base relocations (IMAGE_DIRECTORY_ENTRY_BASERELOC), type DIR64 only: what the loader does for an x64 image.
    const uint32_t relocRva = u32(optional + 112 + 5 * 8), relocSize = u32(optional + 112 + 5 * 8 + 4);
    const uint64_t delta = reinterpret_cast<uint64_t>(image) - preferredBase;
    if (relocRva && relocSize && relocRva < sizeOfImage && relocSize <= sizeOfImage - relocRva)
    {
        for (uint32_t at = 0; at + 8 <= relocSize;)
        {
            uint32_t page = 0, block = 0;
            std::memcpy(&page, image + relocRva + at, 4);
            std::memcpy(&block, image + relocRva + at + 4, 4);
            if (block < 8 || block > relocSize - at)
                break;
            for (uint32_t e = 8; e + 2 <= block; e += 2)
            {
                uint16_t entry = 0;
                std::memcpy(&entry, image + relocRva + at + e, 2);
                const uint32_t target = page + (entry & 0xFFFu);
                if ((entry >> 12) == 10 && target <= sizeOfImage - 8)
                {
                    uint64_t value = 0;
                    std::memcpy(&value, image + target, 8);
                    value += delta;
                    std::memcpy(image + target, &value, 8);
                }
            }
            at += block;
        }
    }
    out.base = image;
    out.size = sizeOfImage;
    out.timestamp = u32(pe + 8);
    return true;
}

// Reads a laid-out image the way the DLL reads a loaded module: its executable sections and .pdata from its own
// headers, and its TLS index from its PE TLS directory.
bool OpenImage(Loaded& module, std::string& why)
{
    const char* refused = nullptr;
    if (!drive_resolve::ReadImage(module.base, module.size, module.image, refused))
    {
        why = refused ? refused : "its image did not read";
        return false;
    }
    uint32_t index = 0;
    uintptr_t rva = 0;
    if (addrcascade::tls_index(module.base, &index, &rva) && rva && rva + sizeof(uint32_t) <= module.size)
        module.image.tlsIndexRva = static_cast<uint32_t>(rva);
    return true;
}

// THE SAME BUILD WITH EVERYTHING MOVED: a copy of the image in which every section sits `shift` bytes higher, as
// after code was inserted ahead of everything. The code's own bytes are unchanged, so every call and every
// rip-relative operand still lands where it did, `shift` higher; what holds RVAs or addresses and is read here is
// rewritten to match: the section table, the data directories, every .pdata entry (and the parent entry inside a
// chained fragment's unwind info) and the TLS directory. No recorded RVA is right for this image.
bool MoveImage(const Loaded& module, uint32_t shift, Loaded& out, std::string& why)
{
    const uint8_t* const src = module.base;
    const auto s32 = [src](size_t at) { uint32_t v = 0; std::memcpy(&v, src + at, 4); return v; };
    const size_t pe = s32(0x3C), optional = pe + 24;
    uint16_t sections = 0, optionalSize = 0;
    std::memcpy(&sections, src + pe + 6, 2);
    std::memcpy(&optionalSize, src + pe + 20, 2);
    const size_t table = optional + optionalSize;
    uint32_t first = static_cast<uint32_t>(module.size);
    for (uint32_t i = 0; i < sections; ++i)
        first = (std::min)(first, s32(table + static_cast<size_t>(i) * 40 + 12));
    if (first < table + static_cast<size_t>(sections) * 40 || first >= module.size)
    {
        why = "its sections do not follow its headers";
        return false;
    }
    const size_t size = module.size + shift;
    auto* image = static_cast<uint8_t*>(VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!image)
    {
        why = "no memory for the moved image";
        return false;
    }
    std::memcpy(image, src, first);
    std::memcpy(image + first + shift, src + first, module.size - first);
    const auto add32 = [image, shift](size_t at) {
        uint32_t v = 0;
        std::memcpy(&v, image + at, 4);
        if (v)
        {
            v += shift;
            std::memcpy(image + at, &v, 4);
        }
    };
    add32(optional + 56);   // SizeOfImage
    for (uint32_t i = 0; i < sections; ++i)
        add32(table + static_cast<size_t>(i) * 40 + 12);
    for (uint32_t d = 0; d < 16; ++d)
        add32(optional + 112 + static_cast<size_t>(d) * 8);
    // .pdata: each entry's begin, end and unwind address; a chained fragment's unwind info also holds its parent.
    const uint32_t pdata = s32(optional + 112 + 3 * 8) + shift, pdataSize = s32(optional + 112 + 3 * 8 + 4);
    std::vector<uint32_t> chained;
    for (uint32_t at = 0; at + 12 <= pdataSize; at += 12)
    {
        uint32_t begin = 0, unwind = 0;
        std::memcpy(&begin, image + pdata + at, 4);
        if (!begin)
            continue;
        add32(pdata + at);
        add32(pdata + at + 4);
        add32(pdata + at + 8);
        std::memcpy(&unwind, image + pdata + at + 8, 4);
        if (!(unwind & 1u) && unwind + 4 <= size && ((image[unwind] >> 3) & 4u))
            chained.push_back(unwind + 4 + ((static_cast<uint32_t>(image[unwind + 2]) + 1u) & ~1u) * 2u);
    }
    std::sort(chained.begin(), chained.end());
    chained.erase(std::unique(chained.begin(), chained.end()), chained.end());
    for (const uint32_t parent : chained)
        if (parent + 12 <= size)
        {
            add32(parent);
            add32(parent + 4);
            add32(parent + 8);
        }
    // The TLS directory holds addresses (already relocated to where the first copy lives): each moves too.
    const uint32_t tls = s32(optional + 112 + 9 * 8);
    if (tls)
        for (uint32_t field = 0; field < 4; ++field)
        {
            uint64_t v = 0;
            std::memcpy(&v, image + tls + shift + field * 8, 8);
            if (v)
            {
                v = v - reinterpret_cast<uint64_t>(src) + reinterpret_cast<uint64_t>(image) + shift;
                std::memcpy(image + tls + shift + field * 8, &v, 8);
            }
        }
    out = Loaded{};
    out.base = image;
    out.size = size;
    out.timestamp = module.timestamp;
    if (!OpenImage(out, why))
    {
        VirtualFree(image, 0, MEM_RELEASE);
        out = Loaded{};
        return false;
    }
    return true;
}

// One drive's resolution: its report, where it would hook, and whether the hook may exist. And what its plan
// found, typed: the install's own decision (drive_install.h) is made from that.
struct Outcome
{
    bool ok = false;
    uint32_t hook = 0;
    char detail[240]{};
    Report report{};
    drive_sites::ReachControl reachControl{};
    drive_sites::LegacyControl legacy{};
    drive_sites::Callers callers{};
    drive_sites::ReachSabre sabre{};
    drive_sites::Halo4Flight halo4Flight{};
    drive_sites::Halo2Control halo2{};
};

using RunFn = void (*)(Resolver&, Outcome&);

// WHICH BUILD'S PLANS are run. A control update's plan is the installs' own function (drive_sites.h:
// PlanReachControl, PlanLegacyControl): a dev build's install runs it with the two tick anchors, a player build's
// without. Every other drive's plan is one function too, and the same in both builds.
bool g_devPlans = false;

// What a control update's caller map came to, in words.
void DescribeCallers(const drive_sites::Callers& c, const drive_sites::CallerAnchors& anchors, char* text, size_t size)
{
    const bool dispatcher = anchors.accumulatorCall.pattern != nullptr;
    int proven = 0, sites = 0;
    bool stick = false;
    for (int i = 0; i < drive_sites::kMaxCallers; ++i)
    {
        if (!anchors.sites[i].pattern)
            continue;
        ++sites;
        proven += c.call[i] ? 1 : 0;
        stick = stick || (c.call[i] && anchors.sites[i].role == aim_caller::Caller::Stick);
    }
    if (dispatcher)
        std::snprintf(text, size, "caller map %s (dispatcher +0x%X, frame 0x%X, %d of %d callers%s%s)",
                      c.frame && stick ? "proven" : "NOT proven", c.dispatcher, c.frame, proven, sites,
                      c.why ? "; " : "", c.why ? c.why : "");
    else
        std::snprintf(text, size, "caller map %s (by return address, %d of %d callers)", stick ? "proven" : "NOT proven",
                      proven, sites);
}

void RunReachControl(Resolver& r, Outcome& out)
{
    const drive_sites::ReachControlPlan plan =
        g_devPlans ? drive_sites::PlanReachControl<true>(r) : drive_sites::PlanReachControl<false>(r);
    out.ok = plan.control.ok;
    out.hook = plan.control.entry;
    out.reachControl = plan.control;
    out.callers = plan.callers;
    DescribeCallers(plan.callers, drive_sites::kReachCallerAnchors, out.detail, sizeof(out.detail));
}

void RunLegacy(Resolver& r, Outcome& out, int slot)
{
    const drive_sites::LegacyControlPlan plan =
        g_devPlans ? drive_sites::PlanLegacyControl<true>(r, slot) : drive_sites::PlanLegacyControl<false>(r, slot);
    const drive_sites::LegacyControl& f = plan.control;
    out.ok = f.ok;
    out.hook = f.entry;
    char callers[160];
    out.legacy = f;
    out.callers = plan.callers;
    DescribeCallers(plan.callers, drive_sites::kLegacyCallerAnchors[slot], callers, sizeof(callers));
    if (slot == 2)
        std::snprintf(out.detail, sizeof(out.detail), "%s; record unit load %s", callers,
                      f.recordUnitLoad ? "proven (seats may be written)" : "NOT proven (seats keep the stick servo)");
    else
        std::snprintf(out.detail, sizeof(out.detail), "%s; pitch clamp %s", callers,
                      f.pitchLimit ? "found" : "NOT found (a look-steered driver's reticle is not clamped in pitch)");
}
void RunHalo3(Resolver& r, Outcome& out) { RunLegacy(r, out, 0); }
void RunOdst(Resolver& r, Outcome& out) { RunLegacy(r, out, 1); }
void RunHalo4(Resolver& r, Outcome& out) { RunLegacy(r, out, 2); }

void DescribeFlight(const drive_sites::Flight& f, Outcome& out)
{
    out.ok = f.ok;
    out.hook = f.entry;
    std::snprintf(out.detail, sizeof(out.detail),
                  "rows missing %u; controller's call +0x%X; dispatcher +0x%X, frame 0x%X%s%s; callers +0x%X +0x%X "
                  "+0x%X +0x%X (stick update +0x%X)",
                  f.missing, f.call, f.dispatcher, f.frame, f.frameWhy ? ": " : "", f.frameWhy ? f.frameWhy : "",
                  f.callerCall[0], f.callerCall[1], f.callerCall[2], f.callerCall[3], f.stickCall);
}

void RunReachSabre(Resolver& r, Outcome& out)
{
    out.sabre = drive_sites::ResolveReachSabre(r);
    DescribeFlight(out.sabre.flight, out);
}

void RunHalo4Flight(Resolver& r, Outcome& out)
{
    out.halo4Flight = drive_sites::ResolveHalo4Flight(r);
    DescribeFlight(out.halo4Flight.flight, out);
}

void RunHalo2(Resolver& r, Outcome& out)
{
    const drive_sites::Halo2Control f = drive_sites::ResolveHalo2Control(r);
    out.ok = f.ok;
    out.hook = f.entry;
    out.halo2 = f;
    std::snprintf(out.detail, sizeof(out.detail), "control update's call +0x%X; player control globals slot +0x%X",
                  f.call, f.slot);
}

template <size_t N>
void RunT13(Resolver& r, Outcome& out, const drive_sites::Anchor (&rows)[N], const drive_sites::FlightSites& flight,
            const drive_sites::T13Sites& sites)
{
    uint32_t found[N]{};
    const drive_sites::T13 f = drive_sites::ResolveT13(r, rows, found, flight, sites);
    out.ok = f.ok;
    out.hook = 0;
    std::snprintf(out.detail, sizeof(out.detail),
                  "rows missing %u; TLS index +0x%X, tag pages +0x%X, tag instances +0x%X (each agreed by its sites); "
                  "dispatcher frame 0x%X",
                  f.flight.missing, f.tlsIndex, f.pages, f.instances, f.flight.frame);
}
void RunT13Reach(Resolver& r, Outcome& out)
{
    RunT13(r, out, drive_sites::kT13ReachAnchors, drive_sites::kT13ReachFlight, drive_sites::kT13ReachSites);
}
void RunT13Halo4(Resolver& r, Outcome& out)
{
    RunT13(r, out, drive_sites::kT13Halo4Anchors, drive_sites::kT13Halo4Flight, drive_sites::kT13Halo4Sites);
}

// THE INSTALL'S OWN DECISION on a module as the loader maps it: the functions of src/dll/drive_install.h that the
// DLL's install calls once its plan has run. `hook`: the install would hook. `whole`: and everything it proves
// beyond the hook (a control update's caller map) is proven. `legacyTls`: where Halo 4's control update drive
// reads the engine TLS index (the Broadsword's drive writes through its record).
struct InstallVerdict
{
    bool hook;
    bool whole;
};
using InstallFn = InstallVerdict (*)(uintptr_t base, size_t size, const Outcome& out, uintptr_t legacyTls, char* text,
                                     size_t n);

InstallVerdict InstallControlMap(uintptr_t base, size_t size, bool hook, uint32_t entry, const drive_sites::Callers& callers,
                                 const drive_sites::CallerAnchors& anchors, const char* flags, char* text, size_t n)
{
    aim_caller::CallerMap map;
    const drive_install::CallerMapProof proof = drive_install::BuildCallerMap(base, size, entry, callers, anchors, map);
    char sites[200];
    drive_install::CallerMapText(base, anchors, proof, sites, sizeof(sites));
    std::snprintf(text, n, "%s; caller map %s%s%s: %s", flags, proof.proven ? "proven" : "NOT proven (",
                  proof.proven ? "" : (proof.why ? proof.why : "?"), proof.proven ? "" : ")", sites);
    return {hook, proof.proven};
}

InstallVerdict InstallReachControl(uintptr_t base, size_t size, const Outcome& out, uintptr_t, char* text, size_t n)
{
    const drive_install::ReachControlDecision d =
        drive_install::DecideReachControl(base, true, out.reachControl, drive_sites::kReachTlsIndexRva);
    char flags[96];
    std::snprintf(flags, sizeof(flags), "entry=%d tls=%d accumulator=%d tls-index=%d", d.entryOk, d.tlsOk, d.accOk,
                  d.indexOk);
    return InstallControlMap(base, size, d.hook, out.reachControl.entry, out.callers, drive_sites::kReachCallerAnchors,
                             flags, text, n);
}

InstallVerdict InstallLegacy(int slot, uintptr_t base, size_t size, const Outcome& out, char* text, size_t n)
{
    const bool hook = drive_install::DecideLegacyControl(true, out.legacy);
    return InstallControlMap(base, size, hook, out.legacy.entry, out.callers, drive_sites::kLegacyCallerAnchors[slot],
                             hook ? "the control update resolved" : "the control update did NOT resolve", text, n);
}
InstallVerdict InstallHalo3(uintptr_t base, size_t size, const Outcome& out, uintptr_t, char* text, size_t n)
{
    return InstallLegacy(0, base, size, out, text, n);
}
InstallVerdict InstallOdst(uintptr_t base, size_t size, const Outcome& out, uintptr_t, char* text, size_t n)
{
    return InstallLegacy(1, base, size, out, text, n);
}
InstallVerdict InstallHalo4(uintptr_t base, size_t size, const Outcome& out, uintptr_t, char* text, size_t n)
{
    return InstallLegacy(2, base, size, out, text, n);
}

InstallVerdict FlightVerdict(const drive_install::FlightDecision& d, int stick, char* text, size_t n)
{
    std::snprintf(text, n, "rows=%d (failed: %s) tls=%d dispatcher call=%d frame=%d (0x%X%s%s) stick-update caller=%d",
                  d.rowsOk, d.failed[0] ? d.failed : "none", d.tlsOk, d.callOk, d.frameOk, d.frame,
                  d.frameWhy ? ": " : "", d.frameWhy ? d.frameWhy : "", d.callerRet[stick] ? 1 : 0);
    return {d.hook, d.hook};
}

InstallVerdict InstallReachSabre(uintptr_t base, size_t size, const Outcome& out, uintptr_t, char* text, size_t n)
{
    return FlightVerdict(drive_install::DecideFlight(base, size, true, drive_sites::kReachSabreAnchors, out.sabre.rows,
                                                     out.sabre.flight, base + drive_sites::kReachTlsIndexRva, true, 3, 1),
                         1, text, n);
}

InstallVerdict InstallHalo4Flight(uintptr_t base, size_t size, const Outcome& out, uintptr_t legacyTls, char* text,
                                  size_t n)
{
    return FlightVerdict(drive_install::DecideFlight(base, size, true, drive_sites::kHalo4FlightAnchors,
                                                     out.halo4Flight.rows, out.halo4Flight.flight, legacyTls, false, 4, 2),
                         2, text, n);
}

InstallVerdict InstallHalo2(uintptr_t base, size_t size, const Outcome& out, uintptr_t, char* text, size_t n)
{
    const drive_install::Halo2Decision d = drive_install::DecideHalo2(base, size, true, out.halo2);
    std::snprintf(text, n, "entry=%d stride=%d yaw=%d pitch=%d call=%d slot=%d", d.entryOk, d.strideOk, d.yawOk, d.pitchOk,
                  d.callOk, d.slotOk);
    return {d.hook, d.hook};
}

struct Drive
{
    const char* dll;
    const char* id;            // its name in the address cache, as the DLL's install gives it
    const char* name;
    RunFn run;
    bool readsTlsIndex;        // its resolution needs the engine TLS index
    bool survivesOffsetFault;  // with every in-function anchor denied its offset it can still be hooked
    const char* lostToOffsetFault[3];   // the sites that fault must lose (two matches in their function)
    InstallFn install;                  // the install's own decision (null: a dev observer, which rides a drive's hook)
    const char* logName;                // what its lines begin with in the log, as its install names it; or null for
    int legacySlot;                     // ... a control update named from drive_sites::kLegacyDriveSites[legacySlot]
    bool devOnly;                       // a dev build has it, a player build does not
};

constexpr Drive kDrives[] = {
    {"halo2", "control-update", "Halo 2 control update (on foot and seats)", &RunHalo2, false, true, {}, &InstallHalo2,
     drive_cascade::kHalo2Name, -1, false},
    {"halo3", "control-update", "Halo 3 control update (on foot and seats)", &RunHalo3, true, true, {}, &InstallHalo3,
     nullptr, 0, false},
    {"halo3odst", "control-update", "ODST control update (on foot and seats)", &RunOdst, true, true, {}, &InstallOdst,
     nullptr, 1, false},
    {"haloreach", "control-update", "Reach control update (on foot and seats)", &RunReachControl, true, true, {},
     &InstallReachControl, drive_cascade::kReachName, -1, false},
    // A pilot's drive needs every row, and its shortest rows (the four bytes that load the angles' address) are
    // taken at their recorded offset or not at all: denied the offset, the drive is not hooked.
    {"haloreach", "pilot-controller", "Reach Sabre / Seraph pilot controller", &RunReachSabre, true, false, {},
     &InstallReachSabre, drive_cascade::kReachSabreName, -1, false},
    {"haloreach", "pilot-observer", "Reach pilot controller, dev observer (dev builds only)", &RunT13Reach, true, false, {},
     nullptr, drive_cascade::kReachObserverName, -1, true},
    // Halo 4's record unit load stands three times in the control update: without its offset it is not found,
    // and only the seats lose direct drive.
    {"halo4", "control-update", "Halo 4 control update (on foot and seats)", &RunHalo4, true, true, {"record unit load"},
     &InstallHalo4, nullptr, 2, false},
    {"halo4", "pilot-controller", "Halo 4 Broadsword pilot controller", &RunHalo4Flight, true, false, {},
     &InstallHalo4Flight, drive_cascade::kHalo4FlightName, -1, false},
    // The observer's IDLE and RECUNIT rows stand more than once in their functions: the observer stays off.
    {"halo4", "pilot-observer", "Halo 4 pilot controller, dev observer (dev builds only)", &RunT13Halo4, true, false,
     {"IDLE", "RECUNIT"}, nullptr, drive_cascade::kHalo4ObserverName, -1, true},
};

// What the drive's lines begin with in the log: the name its install gives it.
void LogName(const Drive& drive, char* out, size_t size)
{
    if (drive.logName)
        std::snprintf(out, size, "%s", drive.logName);
    else
        drive_cascade::LegacyName(drive_sites::kLegacyDriveSites[drive.legacySlot].name, out, size);
}

struct TitleDll
{
    const char* dll;
    GameTitle title;
};
constexpr TitleDll kTitles[] = {{"halo1", GameTitle::HaloCE},        {"halo2", GameTitle::Halo2},
                                {"halo3", GameTitle::Halo3},         {"halo3odst", GameTitle::Halo3ODST},
                                {"haloreach", GameTitle::HaloReach}, {"halo4", GameTitle::Halo4}};

// THE SEARCH alone: every signature looked for in the whole module and every match counted.
Outcome Run(const Loaded& module, const Drive& drive, uint32_t faults)
{
    Outcome out;
    Resolver r(module.image, faults);
    drive.run(r, out);
    out.report = r.report();
    return out;
}

double Milliseconds(const LARGE_INTEGER& from, const LARGE_INTEGER& to)
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return static_cast<double>(to.QuadPart - from.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
}

// One drive as the DLL resolves it, by the rungs (drive_known.h): what came of it, which rung, and how long.
struct ByRungs
{
    Outcome out{};
    drive_known::Outcome rungs{};
    double ms = 0.0;
};

struct RungContext
{
    const Drive* drive;
    Outcome aside;
    Outcome* out;
};

void RungPlan(Resolver& r, void* raw)
{
    auto* c = static_cast<RungContext*>(raw);
    c->aside = Outcome{};
    c->drive->run(r, c->aside);
}

void RungKeep(void* raw)
{
    auto* c = static_cast<RungContext*>(raw);
    *c->out = c->aside;
}

void RunRungs(const Loaded& module, const Drive& drive, uint32_t faults, bool recordedBuild,
              const drive_known::Entry* saved, ByRungs& result)
{
    result = ByRungs{};
    RungContext context{&drive, Outcome{}, &result.out};
    LARGE_INTEGER from, to;
    QueryPerformanceCounter(&from);
    Report report{};
    result.rungs = drive_known::Run(module.image, faults, recordedBuild, saved, &RungPlan, &RungKeep, &context, report);
    QueryPerformanceCounter(&to);
    result.out.report = report;
    result.ms = Milliseconds(from, to);
}

// One drive through the DLL's own side (src/dll/drive_cascade.cpp), with whatever cache file is named: the image
// opened as an install opens a module, the file read before and written after as an install's launch does.
struct FileRun
{
    drive_cascade::Rungs rungs{};
    Outcome out{};
    double ms = 0.0;
    bool opened = false;
};

// `nextLaunch`: as a new process, with nothing remembered of the drive (false: the same process installing it
// again, as at the next level).
FileRun RunThroughFile(const Loaded& module, GameTitle title, const Drive& drive, bool nextLaunch = true)
{
    if (nextLaunch)
        drive_cascade::ForgetResolutions();
    FileRun run;
    RungContext context{&drive, Outcome{}, &run.out};
    Report report{};
    const char* why = nullptr;
    LARGE_INTEGER from, to;
    QueryPerformanceCounter(&from);
    run.opened = drive_cascade::RunPlan(reinterpret_cast<uintptr_t>(module.base), module.size,
                                        drive_cascade::Drive{title, drive.id}, &RungPlan, &RungKeep, &context, report,
                                        run.rungs, why);
    QueryPerformanceCounter(&to);
    run.out.report = report;
    run.ms = Milliseconds(from, to);
    return run;
}

std::string ReadText(const std::wstring& path)
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

bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

const char* RungText(drive_known::Rung rung)
{
    switch (rung)
    {
    case drive_known::Rung::Recorded: return "RUNG 1, verified at the recorded addresses";
    case drive_known::Rung::Saved: return "RUNG 1, verified at the saved addresses";
    case drive_known::Rung::Search: return "RUNG 2, the search";
    case drive_known::Rung::Remembered: return "RUNG 1, verified as this process already resolved it";
    default: return "no rung";
    }
}

void PrintReport(const Report& report)
{
    std::printf("     %-30s %11s %11s %7s  %-26s %s\n", "site", "recorded", "found", "matches", "how", "verdict");
    for (uint32_t i = 0; i < report.count; ++i)
    {
        const drive_resolve::Line& line = report.lines[i];
        char found[16] = "-", matches[8] = "-";
        if (line.found)
            std::snprintf(found, sizeof(found), "+0x%07X", line.found);
        if (line.how == drive_resolve::How::Signature || line.how == drive_resolve::How::FunctionExtent ||
            line.how == drive_resolve::How::FunctionStart)
            std::snprintf(matches, sizeof(matches), "%u%s", line.matches,
                          line.matches >= drive_resolve::kCountCap ? "+" : "");
        std::printf("     %-30s  +0x%07X %11s %7s  %-26s %s%s%s\n", line.name, line.recorded, found, matches,
                    drive_resolve::HowText(line.how), drive_resolve::VerdictText(line.verdict), line.note ? ": " : "",
                    line.note ? line.note : "");
    }
    if (report.dropped)
        std::printf("     (%u more lines than the report holds)\n", report.dropped);
}

bool Lost(const Drive& drive, const char* name)
{
    for (const char* lost : drive.lostToOffsetFault)
        if (lost && !std::strcmp(lost, name))
            return true;
    return false;
}

struct FaultCase
{
    uint32_t faults;
    const char* what;
    const char* expect;
};

constexpr FaultCase kFaultCases[] = {
    {drive_resolve::kFaultSignatureMissing, "a signature finds nothing", "no drive is hooked"},
    {drive_resolve::kFaultSignatureAmbiguous, "every signature is ambiguous",
     "every drive is hooked at the same addresses, each taken at its recorded address among several"},
    {drive_resolve::kFaultRecordWrong, "every recorded RVA is wrong",
     "every drive is hooked at the same addresses, found by search and reported MOVED"},
    {drive_resolve::kFaultSignatureAmbiguous | drive_resolve::kFaultRecordWrong,
     "ambiguous signatures and wrong records together", "no drive is hooked"},
    {drive_resolve::kFaultTlsDirectory, "the PE TLS directory disagrees",
     "no drive that reads the engine TLS index is hooked; the rest are unchanged"},
    {drive_resolve::kFaultOffsetMoved, "no in-function anchor is at its recorded offset",
     "a long one is found as the one match in its function; a short one, or one with two matches there, is not found, "
     "and a drive that needs it is not hooked"},
    {drive_resolve::kFaultHookSilent, "a drive's watchdog is told its hook never ran",
     "nothing about the addresses changes (the watchdog is the DLL's)"},
    {drive_resolve::kFaultSavedStale, "the addresses saved from an earlier launch are stale",
     "verifying them fails, and every drive is hooked at the same addresses, found by search"},
    {drive_resolve::kFaultSearchToo, "self-test: the search is run beside the verified addresses",
     "every drive stays on rung 1, and the search chooses the same address for every line"},
};

// What a forced fault must do to one drive, judged against its healthy resolution. Returns the failure, or null.
// `rungs`: what the rungs went through under the fault (the module is the recorded build).
const char* JudgeFault(const Drive& drive, const FaultCase& test, const Outcome& healthy, const Outcome& faulted,
                       const drive_known::Outcome& rungs)
{
    const uint32_t f = test.faults;
    // WHICH RUNG. A fault of the search, of the record or of an offset must fail the known addresses first, so
    // that the search runs and shows it; the watchdog's bit and the self-test leave rung 1 standing.
    bool hasOffsetAnchor = false;   // a drive with no anchor at an offset in a function is untouched by that fault
    for (uint32_t i = 0; i < healthy.report.count; ++i)
        hasOffsetAnchor = hasOffsetAnchor || healthy.report.lines[i].how == drive_resolve::How::FunctionOffset;
    const bool leavesRungOne = f == drive_resolve::kFaultHookSilent || f == drive_resolve::kFaultSearchToo ||
                               (f == drive_resolve::kFaultTlsDirectory && !drive.readsTlsIndex) ||
                               (f == drive_resolve::kFaultOffsetMoved && !hasOffsetAnchor);
    if (leavesRungOne ? rungs.rung != drive_known::Rung::Recorded : rungs.rung != drive_known::Rung::Search)
        return leavesRungOne ? "the drive left rung 1" : "the known addresses were still taken under the fault";
    if (f == drive_resolve::kFaultSavedStale && !(rungs.savedTried && rungs.savedRefused && !rungs.recordedTried))
        return "the stale saved addresses were not what was tried";
    if (f == drive_resolve::kFaultSearchToo && !(rungs.searchedToo && rungs.searchAgrees))
        return "the search beside rung 1 did not agree with it";
    if (f != drive_resolve::kFaultSearchToo && rungs.searchedToo)
        return "the search was run beside rung 1 without being asked";
    const bool missing = f == drive_resolve::kFaultSignatureMissing;
    const bool refused = f == (drive_resolve::kFaultSignatureAmbiguous | drive_resolve::kFaultRecordWrong);
    if (missing || refused)
    {
        if (faulted.ok)
            return "the drive would still be hooked";
        for (uint32_t i = 0; i < faulted.report.count; ++i)
            if (faulted.report.lines[i].how == drive_resolve::How::Signature && faulted.report.lines[i].found)
                return "a signature search still found something";
        return nullptr;
    }
    if (f == drive_resolve::kFaultTlsDirectory)
    {
        if (drive.readsTlsIndex ? faulted.ok : !faulted.ok)
            return drive.readsTlsIndex ? "the drive would still be hooked" : "a drive that reads no TLS index was lost";
        return nullptr;
    }
    const bool expectOk = healthy.ok && (f != drive_resolve::kFaultOffsetMoved || drive.survivesOffsetFault);
    if (faulted.ok != expectOk)
        return expectOk ? "the drive was lost" : "the drive would still be hooked";
    if (faulted.report.count != healthy.report.count && f != drive_resolve::kFaultOffsetMoved)
        return "the resolution took another path";
    for (uint32_t i = 0; i < faulted.report.count && i < healthy.report.count; ++i)
    {
        const drive_resolve::Line& was = healthy.report.lines[i];
        const drive_resolve::Line& now = faulted.report.lines[i];
        if (std::strcmp(was.name, now.name))
            return f == drive_resolve::kFaultOffsetMoved ? nullptr : "the resolution took another path";
        // A short signature denied its offset is refused, as it must be: it is never searched for.
        if (f == drive_resolve::kFaultOffsetMoved && now.verdict == Verdict::Refused &&
            now.note == drive_resolve::kOffsetOnly)
            continue;
        if (f == drive_resolve::kFaultOffsetMoved && Lost(drive, now.name))
        {
            if (now.found)
                return "an anchor with two matches in its function was still taken";
            continue;
        }
        if (f == drive_resolve::kFaultOffsetMoved && !now.found && !was.found)
            continue;
        if (now.found != was.found)
            return "an address changed";
        if (f == drive_resolve::kFaultSignatureAmbiguous && now.how == drive_resolve::How::Signature &&
            now.verdict != Verdict::RecordAmongSeveral)
            return "an ambiguous signature was not settled by the recorded address";
        if (f == drive_resolve::kFaultRecordWrong && now.found && now.verdict != Verdict::Moved)
            return "an address found against a wrong record was not reported moved";
        if (f == drive_resolve::kFaultOffsetMoved && was.how == drive_resolve::How::FunctionOffset && now.found &&
            now.how != drive_resolve::How::FunctionExtent)
            return "an in-function anchor was still taken at its offset";
    }
    return nullptr;
}

// Whether two resolutions of a drive came to the same thing: hookable or not, the same hook, the same address for
// every line.
bool SameResult(const Outcome& a, const Outcome& b)
{
    return a.ok == b.ok && a.hook == b.hook && drive_known::SameAddresses(a.report, b.report);
}

std::wstring Widen(const char* text)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    std::wstring wide(n > 0 ? static_cast<size_t>(n - 1) : 0, L'\0');
    if (n > 1)
        MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), n);
    return wide;
}

// --loaded: a copy of the DLL file, mapped as the Windows loader maps an image. DONT_RESOLVE_DLL_REFERENCES places
// the sections and applies the relocations, and stops there: no import is resolved and nothing of the DLL runs.
HMODULE MapCopy(const std::wstring& source, const std::wstring& folder, const char* title, std::wstring& copy,
                std::string& why)
{
    CreateDirectoryW(folder.c_str(), nullptr);
    copy = folder + L"\\" + Widen(title) + L".drivecheck.dll";
    if (!CopyFileW(source.c_str(), copy.c_str(), FALSE))
    {
        why = "the copy could not be made";
        return nullptr;
    }
    const HMODULE module = LoadLibraryExW(copy.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!module)
        why = "the copy could not be mapped";
    return module;
}

// One drive's resolution on the mapped copy, through drive_cascade::RunPlan.
struct LoadedRun
{
    const Drive* drive = nullptr;
    uintptr_t base = 0;
    Outcome aside{};
    Outcome out{};
    unsigned tableChecked = 0, tableAgreed = 0;
};

void LoadedKeep(void* raw)
{
    auto* run = static_cast<LoadedRun*>(raw);
    run->out = run->aside;
}

void LoadedThunk(Resolver& r, void* raw)
{
    auto* run = static_cast<LoadedRun*>(raw);
    run->aside = Outcome{};
    run->drive->run(r, run->aside);
    run->tableChecked = run->tableAgreed = 0;
    // At every site found: the .pdata entry the resolution reads is the operating system's own answer.
    const Report& report = r.report();
    for (uint32_t i = 0; i < report.count; ++i)
    {
        const uint32_t at = report.lines[i].found;
        if (!at)
            continue;
        DWORD64 imageBase = 0;
        const PRUNTIME_FUNCTION os = RtlLookupFunctionEntry(run->base + at, &imageBase, nullptr);
        const drive_resolve::RuntimeFunction* ours = drive_resolve::EntryAt(r.image(), at);
        ++run->tableChecked;
        if (os && imageBase == run->base ? (ours && ours->begin == os->BeginAddress && ours->end == os->EndAddress)
                                         : ours == nullptr)
            ++run->tableAgreed;
    }
}

// The drive on the mapped copy against the same drive on the file: null when they are the same, else what differs.
const char* CompareLoaded(const Outcome& file, const LoadedRun& run, const Report& report, bool opened)
{
    if (!opened)
        return "the mapped module did not open";
    if (run.out.ok != file.ok || run.out.hook != file.hook || report.count != file.report.count)
        return "the drive did not resolve the same way";
    for (uint32_t i = 0; i < report.count; ++i)
        if (report.lines[i].found != file.report.lines[i].found || report.lines[i].verdict != file.report.lines[i].verdict)
            return report.lines[i].name;
    if (run.tableAgreed != run.tableChecked)
        return "the operating system's function table disagrees with the .pdata that was read";
    return nullptr;
}
// What one build's plans came to, over every title.
struct Totals
{
    unsigned drives = 0, sites = 0, notAsRecorded = 0, unhooked = 0, faultChecks = 0, faultFailures = 0;
    unsigned movedSites = 0, movedFollowed = 0, movedFailures = 0;
    unsigned loadedDrives = 0, loadedSites = 0, loadedFailures = 0;
    unsigned rungOne = 0, rungFailures = 0, rungScans = 0, savedLaunches = 0;
    unsigned installDrives = 0, installHooks = 0;
    unsigned fileDrives = 0, fileLaunches = 0, fileFailures = 0;
    unsigned recordedOpens = 0;   // opens of the cache's file while the recorded build's drives were resolved
    // Each mapped DLL resolved as a build that is not the recorded one: searched once, then installed again.
    unsigned unrecordedDrives = 0, unrecordedAsMust = 0, unrecordedScans = 0;
    double unrecordedSearchMs = 0.0, unrecordedRepeatMs = 0.0;
    double rungOneMs = 0.0, searchMs = 0.0;
    std::vector<std::string> lines;   // what an install logs for each mapped DLL, word for word
};

constexpr const char* kPlansOf[2] = {
    "THE PLAYER BUILD'S PLANS (what ships)",
    "THE DEV BUILD'S PLANS (two tick anchors more in each control update, and the two pilot observers)"};
} // namespace

int main(int argc, char** argv)
{
    std::string mcc, loadedFolder;
    const char* only = nullptr;
    bool faults = false, brief = false, anyBuild = false, moved = false;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--mcc") && i + 1 < argc)
            mcc = argv[++i];
        else if (!std::strcmp(argv[i], "--title") && i + 1 < argc)
            only = argv[++i];
        else if (!std::strcmp(argv[i], "--faults"))
            faults = true;
        else if (!std::strcmp(argv[i], "--moved"))
            moved = true;
        else if (!std::strcmp(argv[i], "--brief"))
            brief = true;
        else if (!std::strcmp(argv[i], "--any-build"))
            anyBuild = true;
        else if (!std::strcmp(argv[i], "--loaded") && i + 1 < argc)
            loadedFolder = argv[++i];
        else
        {
            std::printf("usage: %s --mcc <MCC install folder> [--faults] [--moved] [--title <dll name>] [--brief] "
                        "[--any-build] [--loaded <scratch folder>]\n", argv[0]);
            return 2;
        }
    }
    if (mcc.empty())
    {
        char env[1024];
        const DWORD n = GetEnvironmentVariableA("HALOMCCVR_MCC_DIR", env, sizeof(env));
        if (n && n < sizeof(env))
            mcc = env;
    }
    if (mcc.empty())
    {
        std::printf("drive address check: SKIPPED (no MCC folder: pass --mcc <folder> or set HALOMCCVR_MCC_DIR). "
                    "NOTHING was checked against a game DLL\n");
        return kExitSkipped;
    }
    addrcascade::set_logger([](const char* message) { std::printf("%s\n", message); });

    Totals totals[2];
    unsigned modules = 0, titlesWanted = 0, titlesNotChecked = 0, unreadable = 0;
    constexpr uint32_t kMovedShift = 0x40;
    // THE ADDRESS CACHE'S FILES, in the scratch folder. For each build's plans: one named while the recorded build's
    // DLLs are resolved, which must never come to exist, and one the moved build's two launches go through. (The
    // two builds' plans are two versions of a drive's tables, so each has its own.)
    const bool withFiles = !loadedFolder.empty();
    std::wstring recordedCache[2], movedCache[2];
    if (withFiles)
    {
        const std::wstring folder = Widen(loadedFolder.c_str());
        CreateDirectoryW(folder.c_str(), nullptr);
        for (int v = 0; v < 2; ++v)
        {
            const std::wstring stem = folder + L"\\drivecheck-" + (v ? L"dev" : L"player");
            recordedCache[v] = stem + L"-recorded-build-addresses.txt";
            movedCache[v] = stem + L"-moved-build-addresses.txt";
            DeleteFileW(recordedCache[v].c_str());
            DeleteFileW(movedCache[v].c_str());
        }
    }
    g_config.drive_address_cache = 1;
    g_config.drive_fault = 0;
    drive_cascade::SetCacheFile(nullptr);
    for (const TitleDll& entry : kTitles)
    {
        const char* const title = entry.dll;
        if (only && std::strcmp(only, title))
            continue;
        ++titlesWanted;
        const std::string path = mcc + "\\" + title + "\\" + title + ".dll";
        Loaded module;
        std::string why;
        if (!LoadAsImage(Widen(path.c_str()), module, why) || !OpenImage(module, why))
        {
            // A title that is not installed is not a failure; a DLL that is there and does not read is.
            const bool absent = GetFileAttributesW(Widen(path.c_str()).c_str()) == INVALID_FILE_ATTRIBUTES;
            std::printf("== %s.dll: NOT CHECKED (%s: %s)\n", title, absent ? "not installed" : why.c_str(), path.c_str());
            unreadable += absent ? 0u : 1u;
            ++titlesNotChecked;
            continue;
        }
        ++modules;
        const title_build::Match match =
            title_build::Compare(entry.title, module.timestamp, static_cast<uint32_t>(module.size));
        const bool recordedBuild = match == title_build::Match::Known;
        std::printf("== %s.dll: PE timestamp 0x%08X, image 0x%08zX: %s; TLS index at +0x%X (PE TLS directory)\n", title,
                    module.timestamp, module.size,
                    recordedBuild ? "the build the tables were measured on (MCC 2025.08.16.178512.1)"
                                  : "NOT the build the tables were measured on",
                    module.image.tlsIndexRva);
        Loaded shifted;
        if (moved && !MoveImage(module, kMovedShift, shifted, why))
        {
            std::printf("   the moved image could not be built (%s)\n", why.c_str());
            ++totals[0].movedFailures;
            ++totals[1].movedFailures;
        }
        HMODULE mapped = nullptr;
        std::wstring mappedCopy;
        if (withFiles)
        {
            mapped = MapCopy(Widen(path.c_str()), Widen(loadedFolder.c_str()), title, mappedCopy, why);
            if (!mapped)
                std::printf("   as the loader maps it: NOT CHECKED (%s)\n", why.c_str());
        }
        bool any = false;
        for (int variant = 0; variant < 2; ++variant)
        {
            g_devPlans = variant == 1;
            Totals& t = totals[variant];
            uintptr_t legacyTls = 0;   // Halo 4: where its control update drive reads the TLS index, on the mapped copy
            bool headed = false;
            for (const Drive& drive : kDrives)
            {
                if (std::strcmp(drive.dll, title) || (drive.devOnly && !g_devPlans))
                    continue;
                if (!headed)
                    std::printf("== %s.dll, %s\n", title, kPlansOf[variant]);
                headed = any = true;
                ++t.drives;
                char logName[64];
                LogName(drive, logName, sizeof(logName));
                const Outcome healthy = Run(module, drive, 0);
                unsigned off = 0;
                for (uint32_t i = 0; i < healthy.report.count; ++i)
                    off += healthy.report.lines[i].verdict == Verdict::AsRecorded ? 0u : 1u;
                t.sites += healthy.report.count;
                t.notAsRecorded += off;
                t.unhooked += healthy.ok ? 0u : 1u;
                std::printf("-- %s\n", drive.name);
                if (!brief || off || !healthy.ok)
                    PrintReport(healthy.report);
                if (healthy.hook)
                    std::printf("   %s +0x%X; %s\n", healthy.ok ? "HOOK at" : "NO HOOK, though its entry was found at",
                                healthy.hook, healthy.detail);
                else
                    std::printf("   %s; %s\n", healthy.ok ? "READY" : "NOT READY", healthy.detail);
                std::printf("   %u sites, %u as recorded, %u moved, %u taken at the recorded address among several, %u "
                            "not found; this version of its tables (the plan) is %016llX\n",
                            healthy.report.count, healthy.report.Count(Verdict::AsRecorded),
                            healthy.report.Count(Verdict::Moved), healthy.report.Count(Verdict::RecordAmongSeveral),
                            healthy.report.NotFound(), static_cast<unsigned long long>(healthy.report.plan));
                {
                    // AS THE DLL RESOLVES IT: the known addresses first. On the recorded build that is rung 1 with
                    // no scan, and it must choose what the search above chose. Each is timed (the search three
                    // times).
                    ByRungs fast;
                    RunRungs(module, drive, 0, recordedBuild, nullptr, fast);
                    double slowest = 0.0, quickest = 1.0e9;
                    unsigned scans = 0;
                    for (int pass = 0; pass < 3; ++pass)
                    {
                        ByRungs searched;
                        RunRungs(module, drive, 0, false, nullptr, searched);
                        slowest = searched.ms > slowest ? searched.ms : slowest;
                        quickest = searched.ms < quickest ? searched.ms : quickest;
                        scans = searched.rungs.scans;
                    }
                    const bool same = SameResult(fast.out, healthy);
                    const bool asMust = same && (!recordedBuild || !healthy.report.Complete() ||
                                                 (fast.rungs.rung == drive_known::Rung::Recorded && fast.rungs.scans == 0));
                    t.rungOne += fast.rungs.rung == drive_known::Rung::Recorded ? 1u : 0u;
                    t.rungScans += fast.rungs.scans;
                    t.rungFailures += asMust ? 0u : 1u;
                    t.rungOneMs += fast.ms;
                    t.searchMs += quickest;
                    std::printf("   by the rungs: %s, %u scans of the module, %.3f ms; the search alone: %u scans, %.1f "
                                "ms (%.1f at most); the same addresses: %s%s\n",
                                RungText(fast.rungs.rung), fast.rungs.scans, fast.ms, scans, quickest, slowest,
                                same ? "yes" : "NO", asMust ? "" : " -- NOT AS IT MUST BE");
                }
                if (moved && shifted.base)
                {
                    // The same drive on the image with every section kMovedShift higher: it must follow, site by
                    // site. Resolved as two launches of the DLL would: the stamp is the recorded build's, so the
                    // recorded addresses are tried and fail, the search finds everything and its addresses are
                    // saved; then the saved addresses are verified with no scan. The entry travels through the
                    // cache's own text.
                    ByRungs launchOne, launchTwo;
                    RunRungs(shifted, drive, 0, recordedBuild, nullptr, launchOne);
                    const Outcome there = launchOne.out;
                    static drive_known::File file, back;
                    static char text[drive_known::kMaxFileText];
                    drive_known::Entry learned;
                    file = drive_known::File{};
                    const char* const moduleName = drive_known::ModuleName(entry.title);
                    const uint64_t stamp = drive_known::Stamp(shifted.image);
                    const char* cacheFailure = nullptr;
                    if (healthy.report.Complete())
                    {
                        if (launchOne.rungs.rung != drive_known::Rung::Search || !launchOne.rungs.learn)
                            cacheFailure = "the first launch did not search and learn";
                        else if (!drive_known::MakeEntry(learned, moduleName, drive.id, stamp, launchOne.out.report))
                            cacheFailure = "what the search found did not make an entry";
                        else
                        {
                            drive_known::Put(file, learned);
                            const size_t length = drive_known::Format(file, text, sizeof(text));
                            drive_known::Parse(text, length, back);
                            const char* refused = nullptr;
                            const drive_known::Entry* const held =
                                drive_known::Usable(back, moduleName, drive.id, stamp, refused);
                            if (!length || !held)
                                cacheFailure = "the entry did not come back out of the cache's text";
                            else
                            {
                                RunRungs(shifted, drive, 0, recordedBuild, held, launchTwo);
                                if (launchTwo.rungs.rung != drive_known::Rung::Saved || launchTwo.rungs.scans != 0 ||
                                    !SameResult(launchTwo.out, launchOne.out))
                                    cacheFailure = "the second launch did not verify the saved addresses with no scan";
                                else
                                    ++t.savedLaunches;
                            }
                        }
                        std::printf("   moved build, two launches: first %s (%u scans, %.1f ms) and saved; second %s "
                                    "(%u scans, %.3f ms)%s%s\n",
                                    RungText(launchOne.rungs.rung), launchOne.rungs.scans, launchOne.ms,
                                    RungText(launchTwo.rungs.rung), launchTwo.rungs.scans, launchTwo.ms,
                                    cacheFailure ? " -- FAILED: " : "", cacheFailure ? cacheFailure : "");
                        t.movedFailures += cacheFailure ? 1u : 0u;
                    }
                    if (healthy.report.Complete() && withFiles)
                    {
                        // THE SAME TWO LAUNCHES THROUGH THE DLL'S OWN SIDE AND A REAL FILE (src/dll/drive_cascade.cpp):
                        // the moved image opened as an install opens a module, its entry written to a file in the
                        // scratch folder by the first launch and read back from that file by the second.
                        ++t.fileDrives;
                        drive_cascade::SetCacheFile(movedCache[variant].c_str());
                        const FileRun one = RunThroughFile(shifted, entry.title, drive);
                        const std::string written = ReadText(movedCache[variant]);
                        const FileRun two = RunThroughFile(shifted, entry.title, drive);
                        const std::string after = ReadText(movedCache[variant]);
                        // And the same process installing the drive again, as at the next level: what it resolved
                        // is remembered, so nothing is searched and the file is not opened.
                        const uint32_t opensBeforeThird = drive_cascade::CacheFileOpens();
                        const FileRun three = RunThroughFile(shifted, entry.title, drive, false);
                        const uint32_t thirdOpens = drive_cascade::CacheFileOpens() - opensBeforeThird;
                        drive_cascade::SetCacheFile(nullptr);
                        drive_known::Parse(written.data(), written.size(), back);
                        const char* refused = nullptr;
                        const bool held = drive_known::Usable(back, moduleName, drive.id, stamp, refused) != nullptr;
                        const char* const fileFailure =
                            !one.opened || !two.opened ? "the moved image did not open through the DLL's own side"
                            : one.rungs.outcome.rung != drive_known::Rung::Search || !one.rungs.outcome.learn
                                  ? "the first launch did not search and learn"
                            : !one.rungs.saved ? (one.rungs.notSaved ? one.rungs.notSaved : "the first launch wrote no file")
                            : !held ? "the file the first launch wrote does not hold the entry"
                            : two.rungs.outcome.rung != drive_known::Rung::Saved || two.rungs.outcome.scans != 0
                                  ? "the second launch did not verify the file's addresses with no scan"
                            : two.rungs.saved || two.rungs.outcome.learn || after != written
                                  ? "the second launch wrote the file again"
                            : !SameResult(one.out, launchOne.out) || !SameResult(two.out, launchOne.out)
                                  ? "the launches through the file did not resolve as the launches without it"
                            : !three.opened || three.rungs.outcome.rung != drive_known::Rung::Remembered ||
                                      three.rungs.outcome.scans != 0 || thirdOpens || !SameResult(three.out, launchOne.out)
                                  ? "installed again in the same launch, it was not verified from memory with no scan and "
                                    "no file"
                            : nullptr;
                        t.fileLaunches += fileFailure ? 0u : 1u;
                        t.fileFailures += fileFailure ? 1u : 0u;
                        std::printf("   moved build, the same two launches through the DLL's own side and a real file: "
                                    "first %s (%u scans, %.1f ms), %s; second %s (%u scans, %.3f ms), the file left as "
                                    "it was: %s%s%s\n",
                                    RungText(one.rungs.outcome.rung), one.rungs.outcome.scans, one.ms,
                                    one.rungs.saved ? "its entry written to the file" : "NOTHING written",
                                    RungText(two.rungs.outcome.rung), two.rungs.outcome.scans, two.ms,
                                    after == written ? "yes" : "NO", fileFailure ? " -- FAILED: " : "",
                                    fileFailure ? fileFailure : "");
                        std::printf("   moved build, installed again in that second launch (the next level): %s (%u "
                                    "scans, %.3f ms; the file opened %u times)\n",
                                    RungText(three.rungs.outcome.rung), three.rungs.outcome.scans, three.ms, thirdOpens);
                        if (!brief || fileFailure)
                        {
                            // What the DLL logs for each of the three.
                            drive_cascade::Announce(logName, one.out.report, one.rungs);
                            drive_cascade::Announce(logName, two.out.report, two.rungs);
                            drive_cascade::Announce(logName, three.out.report, three.rungs);
                        }
                    }
                    unsigned followed = 0, total = 0;
                    const char* failure = there.ok == healthy.ok && there.report.count == healthy.report.count
                        ? nullptr : "the drive did not resolve the same way";
                    for (uint32_t i = 0; !failure && i < there.report.count; ++i)
                    {
                        const drive_resolve::Line& was = healthy.report.lines[i];
                        const drive_resolve::Line& now = there.report.lines[i];
                        if (!was.found)
                            continue;
                        ++total;
                        // A proof is not an address: on the moved image it must have come to the same thing.
                        if (was.how == drive_resolve::How::Proof ? now.found == was.found
                                                                 : now.found == was.found + kMovedShift &&
                                                                       now.verdict == Verdict::Moved)
                            ++followed;
                        else
                            failure = now.name;
                    }
                    t.movedSites += total;
                    t.movedFollowed += followed;
                    t.movedFailures += failure ? 1u : 0u;
                    char where[48];
                    if (there.hook)
                        std::snprintf(where, sizeof(where), "%s +0x%X", there.ok ? "HOOK at" : "NO HOOK at", there.hook);
                    else
                        std::snprintf(where, sizeof(where), "%s", there.ok ? "READY" : "NOT READY");
                    std::printf("   moved build (every section 0x%X higher): %s; %u of %u sites followed and reported "
                                "MOVED%s%s\n",
                                kMovedShift, where, followed, total, failure ? " -- NOT FOLLOWED: " : "",
                                failure ? failure : "");
                }
                if (withFiles)
                {
                    // The same drive on a copy of the DLL as the Windows loader maps it, through the DLL's own side.
                    ++t.loadedDrives;
                    if (!mapped)
                    {
                        ++t.loadedFailures;
                    }
                    else
                    {
                        const uintptr_t base = reinterpret_cast<uintptr_t>(mapped);
                        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
                        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
                        // By the rungs, as an install does it, with a cache file named: on the recorded build
                        // nothing is worth saving, and the file must never come to exist. Then again with the
                        // self-test bit (this tool is built as a dev build of the DLL's side), so the search runs
                        // beside the verified addresses.
                        const drive_cascade::Drive key{entry.title, drive.id};
                        drive_cascade::Rungs rungs{}, tested{};
                        LoadedRun run, again;
                        run.drive = again.drive = &drive;
                        run.base = again.base = base;
                        Report report{}, second{};
                        const char* notOpened = nullptr;
                        g_config.drive_fault = 0;
                        drive_cascade::ForgetResolutions();
                        drive_cascade::SetCacheFile(recordedCache[variant].c_str());
                        const uint32_t opensBefore = drive_cascade::CacheFileOpens();
                        LARGE_INTEGER from, to;
                        QueryPerformanceCounter(&from);
                        const bool opened = drive_cascade::RunPlan(base, nt->OptionalHeader.SizeOfImage, key, &LoadedThunk,
                                                                   &LoadedKeep, &run, report, rungs, notOpened);
                        QueryPerformanceCounter(&to);
                        g_config.drive_fault = static_cast<int>(drive_resolve::kFaultSearchToo);
                        const char* ignored = nullptr;
                        (void)drive_cascade::RunPlan(base, nt->OptionalHeader.SizeOfImage, key, &LoadedThunk, &LoadedKeep,
                                                     &again, second, tested, ignored);
                        g_config.drive_fault = 0;
                        addrcascade::set_fault_mask(0);
                        drive_cascade::SetCacheFile(nullptr);
                        const uint32_t fileOpens = drive_cascade::CacheFileOpens() - opensBefore;
                        t.recordedOpens += recordedBuild ? fileOpens : 0u;
                        const char* failure = CompareLoaded(healthy, run, report, opened);
                        if (!failure && healthy.report.Complete() &&
                            !(rungs.recordedBuild && rungs.outcome.rung == drive_known::Rung::Recorded &&
                              rungs.outcome.scans == 0))
                            failure = "it was not verified at the recorded addresses with no scan";
                        if (!failure && healthy.report.Complete() &&
                            !(tested.outcome.searchedToo && tested.outcome.searchAgrees && SameResult(again.out, run.out)))
                            failure = "the search beside the verified addresses did not agree with them";
                        // Nothing of the recorded build is worth saving, and nothing was: the rung's own word, and
                        // the file itself.
                        if (!failure && recordedBuild && healthy.report.Complete() &&
                            (rungs.outcome.learn || rungs.saved || tested.outcome.learn || tested.saved))
                            failure = "the recorded build's own addresses were held worth saving";
                        if (!failure && recordedBuild && Exists(recordedCache[variant]))
                            failure = "a cache file was written for the recorded build";
                        // ... and not even opened: asking for saved addresses is for a drive rung 1 (a) left open.
                        if (!failure && recordedBuild && healthy.report.Complete() && fileOpens)
                            failure = "the cache's file was opened for a drive that verified at the recorded addresses";
                        // THE INSTALL'S OWN DECISION on this module (drive_install.h: the code the DLL's install
                        // runs once its plan has): what is printed below as its hook is what the install would do.
                        char decided[420] = "its readiness is its plan's (a dev observer rides the drive's hook)";
                        InstallVerdict verdict{run.out.ok, true};
                        if (drive.install)
                        {
                            verdict = drive.install(base, nt->OptionalHeader.SizeOfImage, run.out, legacyTls, decided,
                                                    sizeof(decided));
                            ++t.installDrives;
                            t.installHooks += verdict.hook && verdict.whole ? 1u : 0u;
                        }
                        if (!std::strcmp(title, "halo4") && !std::strcmp(drive.id, "control-update"))
                            legacyTls = base + run.out.legacy.tlsIndex;
                        if (!failure && healthy.report.Complete() && !(verdict.hook && verdict.whole))
                            failure = "the install's own decision is not a hook with everything proven";
                        t.loadedSites += report.count;
                        t.loadedFailures += failure ? 1u : 0u;
                        std::printf("   as the loader maps it (base %p, through drive_cascade): %s; %s, %u scans, %.3f "
                                    "ms; %u of %u sites as from the file; the system's function table agrees at %u of "
                                    "%u; self-test: the search beside it (%u scans) %s%s%s%s%s\n",
                                    reinterpret_cast<void*>(base),
                                    run.out.hook ? (run.out.ok ? "HOOK at the same address" : "NO HOOK")
                                                 : (run.out.ok ? "READY" : "NOT READY"),
                                    RungText(rungs.outcome.rung), rungs.outcome.scans, Milliseconds(from, to),
                                    failure ? 0u : report.count, report.count, run.tableAgreed, run.tableChecked,
                                    tested.outcome.scans,
                                    tested.outcome.searchedToo ? (tested.outcome.searchAgrees ? "agrees" : "DISAGREES")
                                                               : "did not run",
                                    failure ? " -- DIFFERS: " : "", failure ? failure : "",
                                    notOpened ? ": " : "", notOpened ? notOpened : "");
                        std::printf("      THE INSTALL'S OWN DECISION there: %s (%s)\n",
                                    !drive.install ? (verdict.hook ? "READY" : "NOT READY")
                                                   : verdict.hook ? (verdict.whole ? "HOOK, with everything it proves"
                                                                                   : "HOOK, but its caller map is NOT proven")
                                                                  : "NO HOOK",
                                    decided);
                        // WHAT THE INSTALL LOGS for this drive on this DLL: the DLL's own function, the DLL's own
                        // name for the drive, the result just made.
                        if (opened)
                        {
                            std::printf("      what its install logs:\n");
                            g_capture = &t.lines;
                            drive_cascade::Announce(logName, report, rungs);
                            g_capture = nullptr;
                        }
                        // AS A BUILD THAT IS NOT THE RECORDED ONE WOULD GO, on this same mapped DLL: the drive is
                        // resolved for no title, so nothing is recorded for the module and no cache file is
                        // named. The first install searches it, as the loader maps it; the same process
                        // installing the drive again must verify what it remembered, with no scan. (A drive
                        // the search did not find whole is not believed from one search: its next install
                        // searches again, drive_known::kFreshSearches times, and that is as it must be too.)
                        {
                            const drive_cascade::Drive unrecorded{GameTitle::None, drive.id};
                            drive_cascade::Rungs searched{}, repeated{};
                            LoadedRun firstInstall, nextInstall;
                            firstInstall.drive = nextInstall.drive = &drive;
                            firstInstall.base = nextInstall.base = base;
                            Report searchedReport{}, repeatedReport{};
                            const char* unused = nullptr;
                            drive_cascade::ForgetResolutions();
                            const bool first = drive_cascade::RunPlan(base, nt->OptionalHeader.SizeOfImage, unrecorded,
                                                                      &LoadedThunk, &LoadedKeep, &firstInstall,
                                                                      searchedReport, searched, unused);
                            const bool next = drive_cascade::RunPlan(base, nt->OptionalHeader.SizeOfImage, unrecorded,
                                                                     &LoadedThunk, &LoadedKeep, &nextInstall,
                                                                     repeatedReport, repeated, unused);
                            drive_cascade::ForgetResolutions();
                            const bool asMust =
                                first && next && searched.outcome.rung == drive_known::Rung::Search &&
                                repeated.outcome.rung == drive_known::Rung::Remembered && repeated.outcome.scans == 0 &&
                                firstInstall.out.ok == run.out.ok && firstInstall.out.hook == run.out.hook &&
                                nextInstall.out.ok == run.out.ok && nextInstall.out.hook == run.out.hook &&
                                drive_known::SameAddresses(searchedReport, report) &&
                                drive_known::SameAddresses(repeatedReport, report);
                            const bool searchedAgain = first && next && !searchedReport.Complete() &&
                                                       repeated.outcome.rung == drive_known::Rung::Search &&
                                                       repeated.searchedAgain == 1;
                            t.unrecordedDrives += 1;
                            t.unrecordedAsMust += asMust ? 1u : 0u;
                            t.unrecordedScans += searched.outcome.moduleScans;
                            t.unrecordedSearchMs += searched.ms;
                            t.unrecordedRepeatMs += repeated.ms;
                            t.loadedFailures += asMust || searchedAgain || !healthy.report.Complete() ? 0u : 1u;
                            std::printf("      as a build that is not the recorded one would go, on this mapped DLL: "
                                        "first install %s (%u scans of the module, %.2f ms); installed again in the "
                                        "same process %s (%u scans, %.2f ms)%s\n",
                                        RungText(searched.outcome.rung), searched.outcome.moduleScans, searched.ms,
                                        RungText(repeated.outcome.rung), repeated.outcome.moduleScans, repeated.ms,
                                        asMust ? ""
                                        : searchedAgain ? " -- searched again: the first search did not find the whole "
                                                          "drive, and one search is not believed"
                                                        : " -- NOT AS IT MUST BE");
                        }
                    }
                }
                if (!faults)
                    continue;
                for (const FaultCase& test : kFaultCases)
                {
                    ByRungs underFault;
                    RunRungs(module, drive, test.faults, recordedBuild, nullptr, underFault);
                    const Outcome& faulted = underFault.out;
                    const char* failure = recordedBuild ? JudgeFault(drive, test, healthy, faulted, underFault.rungs)
                                                        : "the faults are judged on the recorded build only";
                    ++t.faultChecks;
                    t.faultFailures += failure ? 1u : 0u;
                    // What the fault made of the drive, in one line: its first site that is not as recorded.
                    const drive_resolve::Line* first = nullptr;
                    for (uint32_t i = 0; i < faulted.report.count && !first; ++i)
                        if (faulted.report.lines[i].verdict != Verdict::AsRecorded)
                            first = &faulted.report.lines[i];
                    char example[200] = "every site as recorded";
                    if (first)
                        std::snprintf(example, sizeof(example), "e.g. %s: recorded +0x%X, found +0x%X, %u match(es), %s%s%s",
                                      first->name, first->recorded, first->found, first->matches,
                                      drive_resolve::VerdictText(first->verdict), first->note ? ": " : "",
                                      first->note ? first->note : "");
                    if (!brief || failure)
                        std::printf("   fault 0x%02X (%s): %s; %u as recorded, %u moved, %u at the record among several, "
                                    "%u not found; %s -- %s%s\n",
                                    test.faults, test.what, faulted.ok ? "HOOKED" : "NOT hooked",
                                    faulted.report.Count(Verdict::AsRecorded), faulted.report.Count(Verdict::Moved),
                                    faulted.report.Count(Verdict::RecordAmongSeveral), faulted.report.NotFound(), example,
                                    failure ? "UNEXPECTED: " : "as it must", failure ? failure : "");
                }
            }
        }
        g_devPlans = false;
        if (mapped)
            FreeLibrary(mapped);
        if (!mappedCopy.empty())
            DeleteFileW(mappedCopy.c_str());
        if (!any)
            std::printf("-- no direct drive: nothing in this title's game DLL is hooked or resolved for one\n");
        if (shifted.base)
            VirtualFree(shifted.base, 0, MEM_RELEASE);
        VirtualFree(module.base, 0, MEM_RELEASE);
    }

    // ------------------------------------------------------------------------------------------------ the summary
    if (faults)
    {
        std::printf("\nFAULTS (each forced on every drive):\n");
        for (const FaultCase& test : kFaultCases)
            std::printf("   0x%02X %s -> %s\n", test.faults, test.what, test.expect);
    }
    unsigned failures = 0, notAsRecorded = unreadable, unhooked = 0;
    for (int v = 0; v < 2; ++v)
    {
        Totals& t = totals[v];
        std::printf("\n%s: %u drives, %u sites: %u not as recorded, %u drives that could not be hooked\n", kPlansOf[v],
                    t.drives, t.sites, t.notAsRecorded, t.unhooked);
        std::printf("   THE RUNGS: %u of %u drives were verified at the recorded addresses with %u scans of the module "
                    "in all (%.2f ms together; the search alone takes %.0f ms for the same drives); %u did not resolve "
                    "as they must\n",
                    t.rungOne, t.drives, t.rungScans, t.rungOneMs, t.searchMs, t.rungFailures);
        if (faults)
            std::printf("   FAULTS: %u checks, %u unexpected\n", t.faultChecks, t.faultFailures);
        if (moved)
        {
            std::printf("   MOVED BUILD (every section 0x%X higher, so no recorded RVA is right): %u of %u sites "
                        "followed; %u drives did not follow\n", kMovedShift, t.movedFollowed, t.movedSites, t.movedFailures);
            std::printf("   THE ADDRESS CACHE, its text: on the moved build %u of %u drives were searched and saved by "
                        "the first launch and verified with no scan by the second\n", t.savedLaunches, t.drives);
        }
        if (moved && withFiles)
        {
            // What the moved build's launches left on disk: one entry for each drive, no more.
            const std::string text = ReadText(movedCache[v]);
            static drive_known::File left;
            drive_known::Parse(text.data(), text.size(), left);
            const bool oneEach = left.count == t.fileLaunches && !Exists(movedCache[v] + L".tmp");
            t.fileFailures += oneEach ? 0u : 1u;
            std::printf("   THE ADDRESS CACHE, its file: through the DLL's own side %u of %u drives were searched and "
                        "written to a real file by the first launch, and read back from it and verified with no scan "
                        "by the second; the file holds %u entries in %zu bytes%s\n",
                        t.fileLaunches, t.fileDrives, left.count, text.size(),
                        oneEach ? "" : " -- NOT one for each drive, or a temporary file was left");
            const size_t line = text.find("\r\nentry ");
            if (line != std::string::npos)
                std::printf("      its first entry: %.*s\n",
                            static_cast<int>((std::min)(text.find("\r\n", line + 2) - line - 2, static_cast<size_t>(400))),
                            text.c_str() + line + 2);
        }
        else
        {
            std::printf("   THE ADDRESS CACHE, its file: NOT written or read (that needs --moved and --loaded <folder>)\n");
        }
        if (withFiles)
        {
            const bool appeared = Exists(recordedCache[v]);
            t.fileFailures += appeared ? 1u : 0u;
            std::printf("   AS THE LOADER MAPS THEM (a copy of each DLL mapped with its code not run, resolved through "
                        "the DLL's own side): %u drives, %u sites; %u drives differed from the file\n",
                        t.loadedDrives, t.loadedSites, t.loadedFailures);
            std::printf("   THE INSTALLS' OWN DECISIONS (src/dll/drive_install.h, the code each install runs after its "
                        "plan, on the mapped copies): %u of %u drives would be hooked with everything they prove\n",
                        t.installHooks, t.installDrives);
            std::printf("   ON THE RECORDED BUILD NOTHING IS SAVED: with a cache file named for every one of those "
                        "resolutions, %s, and the file was opened %u times (it must be 0: nothing asks for saved "
                        "addresses while the recorded ones verify)\n",
                        appeared ? "A FILE APPEARED -- it must not" : "no file came to exist", t.recordedOpens);
            std::printf("   AS A BUILD THAT IS NOT THE RECORDED ONE, on the same mapped DLLs and through the DLL's "
                        "own side: the first install of the %u drives searched (%u scans of a module, %.1f ms "
                        "together); installed again in the same process, %u of %u were verified from what it "
                        "remembered with no scan (%.2f ms together)\n",
                        t.unrecordedDrives, t.unrecordedScans, t.unrecordedSearchMs, t.unrecordedAsMust,
                        t.unrecordedDrives, t.unrecordedRepeatMs);
            std::printf("   WHAT AN INSTALL LOGS FOR THESE DLLS (drive_cascade::Announce, the DLL's own function and "
                        "names, word for word):\n");
            for (const std::string& said : t.lines)
                std::printf("      %s\n", said.c_str());
        }
        else
        {
            std::printf("   THE INSTALLS' OWN DECISIONS were NOT made, and what an install logs was NOT said: both need "
                        "a module as the loader maps it (--loaded <folder>). What is printed above as HOOK is the "
                        "plan's word alone\n");
        }
        failures += t.faultFailures + t.movedFailures + t.loadedFailures + t.rungFailures + t.fileFailures;
        notAsRecorded += t.notAsRecorded;
        unhooked += t.unhooked;
    }
    if (withFiles)
        for (int v = 0; v < 2; ++v)
        {
            DeleteFileW(recordedCache[v].c_str());
            DeleteFileW(movedCache[v].c_str());
        }
    std::printf("\nMODES RUN: the search's table and the rungs, for both builds' plans (always); the moved build: %s; "
                "the forced faults: %s; each DLL as the loader maps it, through the DLL's own side, with the installs' "
                "own decisions, the cache's real file and the lines an install logs: %s\n",
                moved ? "yes" : "NO (--moved)", faults ? "yes" : "NO (--faults)",
                withFiles ? "yes" : "NO (--loaded <folder>)");
    std::printf("CTEST AND THIS CHECK: ctest runs it as `--moved --faults --brief`, and ONLY where the environment "
                "variable HALOMCCVR_MCC_DIR names an MCC install; without it ctest reports the check SKIPPED and "
                "counts it among the tests that passed, having read no game DLL. ctest never passes --loaded: the "
                "DLL's own side on the mapped copies, the installs' own decisions, the cache's real file and the "
                "lines an install logs are checked only by a run of this tool with --loaded <folder>\n");
    std::printf("\n%u game DLLs; the player build's plans: %u drives, %u sites; the dev build's: %u drives, %u sites; "
                "%u not as recorded, %u drives that could not be hooked\n",
                modules, totals[0].drives, totals[0].sites, totals[1].drives, totals[1].sites, notAsRecorded, unhooked);
    if (!modules)
        return 2;
    // A title that was asked for and not checked is not a pass: the result below would cover only the rest.
    if (titlesNotChecked)
    {
        std::printf("INCOMPLETE: %u of the %u titles asked for were NOT checked (no DLL, or one that does not read); what "
                    "is printed above covers only the other %u\n",
                    titlesNotChecked, titlesWanted, titlesWanted - titlesNotChecked);
        return 3;
    }
    if (failures)
    {
        std::printf("FAILED: %u checks above did not come out as they must\n", failures);
        return 1;
    }
    if (anyBuild)
        return 0;
    std::printf("%s\n", notAsRecorded || unhooked
                            ? "ACCEPTANCE FAILED: on the recorded build every site must resolve as recorded"
                            : "ACCEPTANCE: every site of every drive resolves where the tables record it, for the "
                              "player build's plans and for the dev build's");
    return notAsRecorded || unhooked ? 1 : 0;
}
