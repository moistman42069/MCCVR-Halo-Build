// THE INSTALL'S SEARCHES SAVED FOR THE NEXT LAUNCH (src/dll/install_search_cache.h, install_search_cache_logic.h).
//
// What is held to account: the file reads back only whole and only for the build it was written for; the build
// identity tells builds apart and does not move when this mod hooks the code; every saved answer is checked as the
// scanner checks one it remembers (a saved match that no longer stands is searched for again and replaced); and a
// file that cannot be read is never written over.
//
// A "launch" here is a fresh memo for the same image, as a new process has. No MCC process, module, config,
// installation or game data is touched: the images are synthetic, or this test's own executable.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <MinHook.h>
#include "../src/common/minhook_lifecycle.h"
#include "../src/dll/install_search_cache.h"
#include "../src/dll/sigscan.h"

namespace
{
int failures = 0;
void Check(bool condition, const char* description)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}

using namespace install_search_cache;

// ------------------------------------------------------------------------------------------------ the file
Identity MakeIdentity(uint64_t codeHash)
{
    Identity id;
    std::memcpy(id.title, "Halo 3", 6);
    id.timeDateStamp = 0x68A0EDFC;
    id.sizeOfImage = 0x4768000;
    id.checkSum = 0xA9D814;
    id.pdbAge = 1;
    id.pdbGuid[0] = 0x3A;
    id.codeHash = codeHash;
    id.codeBytes = 0x6F811C;
    return id;
}

void FileTests()
{
    Build a{MakeIdentity(1), {{0x1111, 0x1000, 0x100, 0x1010}, {0x2222, 0x0, 0x4768000, kNoMatch}}};
    Build b{MakeIdentity(2), {{0x3333, 0x2000, 0x20, 0x2000}}};
    const std::vector<uint8_t> bytes = Serialize({a, b});
    std::vector<Build> back;
    Check(Parse(bytes.data(), bytes.size(), back) && back.size() == 2, "file: two builds read back");
    Check(Find(back, a.identity) && Find(back, a.identity)->entries.size() == 2 &&
              Find(back, a.identity)->entries[0].match == 0x1010 &&
              Find(back, a.identity)->entries[1].match == kNoMatch,
          "file: each build's answers as written");
    Identity other = a.identity;
    other.rules = kSearchRules + 1;
    Check(!Find(back, other), "file: answers made under other search rules are not this mod's");
    other = a.identity;
    other.pdbGuid[15] ^= 1;
    Check(!Find(back, other), "file: another link of the DLL finds nothing");

    for (size_t i = 0; i < bytes.size(); i += 7)
    {
        std::vector<uint8_t> flipped = bytes;
        flipped[i] ^= 0x40;
        if (Parse(flipped.data(), flipped.size(), back))
        {
            Check(false, "file: a changed byte anywhere makes it not whole");
            break;
        }
    }
    Check(!Parse(bytes.data(), bytes.size() - 1, back) && back.empty(), "file: a short file is not whole");
    std::vector<uint8_t> longer = bytes;
    longer.insert(longer.end() - 8, 0);
    Check(!Parse(longer.data(), longer.size(), back), "file: a longer one is not either");
    Check(!Parse(nullptr, 0, back) && !Parse(bytes.data(), 4, back), "file: nothing is not a file");

    // An entry outside its image, written by something else with a right checksum, is refused whole.
    Build bad{MakeIdentity(3), {{0x4444, 0x4767FF0, 0x100, kNoMatch}}};
    const std::vector<uint8_t> badBytes = Serialize({bad});
    Check(!Parse(badBytes.data(), badBytes.size(), back), "file: an answer outside its image is not used");
    Build badMatch{MakeIdentity(3), {{0x4444, 0x1000, 0x100, 0x0FFF}}};
    const std::vector<uint8_t> badMatchBytes = Serialize({badMatch});
    Check(!Parse(badMatchBytes.data(), badMatchBytes.size(), back), "file: a match outside its range is not used");

    // Put: a build replaces its own entry and becomes the newest; the oldest gives way past kMaxBuilds.
    std::vector<Build> builds{a, b};
    Build a2 = a;
    a2.entries.resize(1);
    Put(builds, a2);
    Check(builds.size() == 2 && builds.back().identity == a.identity && builds.back().entries.size() == 1,
          "put: a build's answers replace its own");
    for (uint64_t h = 10; h < 10 + kMaxBuilds; ++h)
        Put(builds, Build{MakeIdentity(h), {}});
    Check(builds.size() == kMaxBuilds && !Find(builds, a.identity) && Find(builds, MakeIdentity(10)),
          "put: the oldest builds give way");
}

// ------------------------------------------------------------------------------------ a synthetic DLL image
constexpr size_t kImageSize = 0x6000;
constexpr uint32_t kText = 0x1000, kTextSize = 0x3000, kData = 0x4000, kDataSize = 0x2000;

const char* const kOne = "4D 43 43 56 52 ?? 6F 6E 65 ? 01 02";        // once, in the data section
const uint8_t kOneBytes[12] = {0x4D, 0x43, 0x43, 0x56, 0x52, 0xAA, 0x6F, 0x6E, 0x65, 0xBB, 0x01, 0x02};
const char* const kTwo = "74 77 6F 2D 74 69 6D 65 73 11 22 33";        // twice, in the code
const uint8_t kTwoBytes[12] = {0x74, 0x77, 0x6F, 0x2D, 0x74, 0x69, 0x6D, 0x65, 0x73, 0x11, 0x22, 0x33};
const char* const kThree = "6F 6E 63 65 2D 63 6F 64 65 44 55 66";      // once, in the code
const uint8_t kThreeBytes[12] = {0x6F, 0x6E, 0x63, 0x65, 0x2D, 0x63, 0x6F, 0x64, 0x65, 0x44, 0x55, 0x66};
const char* const kAbsent = "61 62 73 65 6E 74 2D 70 61 74 FE FD";
constexpr uint32_t kOneAt = kData + 0x100, kOneMovedAt = kData + 0x900;
constexpr uint32_t kTwoAt = kText + 0x200, kTwoAgainAt = kText + 0x1200, kThreeAt = kText + 0x800;

void BuildImage(uint8_t* image, uint32_t stamp)
{
    std::memset(image, 0, kImageSize);
    uint32_t state = 0x1234567u;
    for (size_t i = 0x1000; i < kImageSize; ++i)
    {
        state = state * 1664525u + 1013904223u;
        image[i] = static_cast<uint8_t>(0x80u | ((state >> 24) & 0x0Fu));
    }
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image + 0x80);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.NumberOfSections = 2;
    nt->FileHeader.TimeDateStamp = stamp;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.SizeOfImage = static_cast<DWORD>(kImageSize);
    nt->OptionalHeader.SizeOfHeaders = 0x1000;
    nt->OptionalHeader.CheckSum = 0xC0FFEE;
    auto* sections = reinterpret_cast<IMAGE_SECTION_HEADER*>(reinterpret_cast<uint8_t*>(&nt->OptionalHeader) +
                                                             sizeof(IMAGE_OPTIONAL_HEADER64));
    std::memcpy(sections[0].Name, ".text", 5);
    sections[0].VirtualAddress = kText;
    sections[0].Misc.VirtualSize = kTextSize;
    sections[0].Characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    std::memcpy(sections[1].Name, ".data", 5);
    sections[1].VirtualAddress = kData;
    sections[1].Misc.VirtualSize = kDataSize;
    sections[1].Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
    std::memcpy(image + kOneAt, kOneBytes, sizeof(kOneBytes));
    std::memcpy(image + kTwoAt, kTwoBytes, sizeof(kTwoBytes));
    std::memcpy(image + kTwoAgainAt, kTwoBytes, sizeof(kTwoBytes));
    std::memcpy(image + kThreeAt, kThreeBytes, sizeof(kThreeBytes));
}

struct Asked
{
    uintptr_t first = 0;
    uintptr_t second = 0;
};
Asked Ask(uintptr_t base, size_t size, const char* pattern)
{
    Asked a;
    a.first = sig::Find(base, size, pattern);
    if (a.first)
        a.second = sig::Find(a.first + 1, base + size - a.first - 1, pattern);
    return a;
}

// Every question an install asks of the image, as one launch asks them.
struct Launch
{
    Asked one, two, three, absent;
    sig::ImageMemoCounts counts;
    LoadReport loaded;
    bool identified = false;
    bool saved = false;
    Identity identity;
};
Launch RunLaunch(sig::ImageMemo& memo, uintptr_t base, uint32_t generation, bool save = true)
{
    Launch l;
    sig::ImageMemoScope scope(memo, base, kImageSize, generation, true);
    const char* why = nullptr;
    l.identified = Identify("Halo 3", base, kImageSize, l.identity, why);
    if (l.identified && !memo.savedLoaded)
    {
        memo.savedLoaded = true;
        l.loaded = Load(l.identity, memo);
    }
    l.one = Ask(base, kImageSize, kOne);
    l.two = Ask(base, kImageSize, kTwo);
    l.three = Ask(base, kImageSize, kThree);
    l.absent = Ask(base, kImageSize, kAbsent);
    l.counts = scope.Counts();
    double ms = 0.0;
    if (save && l.identified && memo.changes)
        l.saved = Save(l.identity, memo, why, ms);
    return l;
}

std::wstring TempFile()
{
    wchar_t folder[MAX_PATH]{};
    GetTempPathW(MAX_PATH, folder);
    wchar_t path[MAX_PATH]{};
    _snwprintf_s(path, _TRUNCATE, L"%lshalomccvr-install-searches-test-%lu.bin", folder, GetCurrentProcessId());
    return path;
}

void LaunchTests()
{
    auto* image = static_cast<uint8_t*>(VirtualAlloc(nullptr, kImageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!image)
    {
        Check(false, "launch: synthetic image");
        return;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(image);
    BuildImage(image, 0x5EED0001);
    const std::wstring path = TempFile();
    DeleteFileW(path.c_str());
    SetFile(path.c_str());

    // The truth: the search itself.
    const Asked one = Ask(base, kImageSize, kOne), two = Ask(base, kImageSize, kTwo),
                three = Ask(base, kImageSize, kThree);
    Check(one.first == base + kOneAt && !one.second && two.first == base + kTwoAt &&
              two.second == base + kTwoAgainAt && three.first == base + kThreeAt && !three.second,
          "image: the signatures are where they were put");

    // The first launch: no file, every search made, and saved.
    static sig::ImageMemo first;
    const Launch l1 = RunLaunch(first, base, 1);
    Check(l1.identified, "identity: a synthetic PE image is identified");
    Check(l1.loaded.result == LoadResult::NoFile, "first launch: no file yet");
    Check(l1.counts.fromSaved == 0 && l1.counts.remembered == 0 && l1.counts.searched == 7,
          "first launch: all seven searches made");
    Check(l1.saved, "first launch: what was found is saved");

    // The next launch: a fresh memo; every answer but the absent one comes from the file, checked in place.
    static sig::ImageMemo second;
    const Launch l2 = RunLaunch(second, base, 1);
    Check(l2.loaded.result == LoadResult::Loaded && l2.loaded.taken == 7 && l2.loaded.builds == 1,
          "next launch: this build's seven answers are loaded");
    Check(l2.one.first == one.first && !l2.one.second && l2.two.first == two.first &&
              l2.two.second == two.second && l2.three.first == three.first && !l2.three.second && !l2.absent.first,
          "next launch: the same answers as the search");
    Check(l2.counts.fromSaved == 6 && l2.counts.searched == 1 && l2.counts.remembered == 0,
          "next launch: six taken from the saved answers, the absent one searched again");
    Check(!l2.saved && second.changes == 0, "next launch: nothing new, nothing written");

    // A saved match that no longer stands under the same build (data the identity does not cover moved): the
    // compare in place refuses it, the search finds the new place, and that is what is saved.
    std::memset(image + kOneAt, 0x81, sizeof(kOneBytes));
    std::memcpy(image + kOneMovedAt, kOneBytes, sizeof(kOneBytes));
    static sig::ImageMemo third;
    const Launch l3 = RunLaunch(third, base, 1);
    Check(l3.loaded.result == LoadResult::Loaded, "moved: the same build's answers are loaded");
    Check(l3.one.first == base + kOneMovedAt && !l3.one.second, "moved: the search's answer, not the saved one");
    Check(l3.counts.savedStale == 1 && l3.counts.searchedAgain == 1, "moved: counted as a saved place that no longer stood");
    Check(l3.saved, "moved: the corrected answer is saved");
    static sig::ImageMemo fourth;
    const Launch l4 = RunLaunch(fourth, base, 1);
    Check(l4.one.first == base + kOneMovedAt && l4.counts.savedStale == 0 && l4.counts.fromSaved == 6,
          "moved: the launch after takes the corrected answer");

    // Another build (a byte of code changed): nothing of the first build's is taken, and both are kept.
    image[kText + 0x2F00] ^= 0x01;
    static sig::ImageMemo fifth;
    const Launch l5 = RunLaunch(fifth, base, 1);
    Check(l5.identified && l5.identity != l4.identity, "another build: a changed code byte is another build");
    Check(l5.loaded.result == LoadResult::NoEntry && l5.loaded.builds == 1 && l5.counts.fromSaved == 0,
          "another build: none of the other build's answers is taken");
    Check(l5.saved, "another build: its own answers are saved");
    static sig::ImageMemo sixth;
    const Launch l6 = RunLaunch(sixth, base, 1);
    Check(l6.loaded.result == LoadResult::Loaded && l6.loaded.builds == 2, "another build: the file keeps both");
    image[kText + 0x2F00] ^= 0x01;

    // Another stamp, same code: another build.
    {
        Identity a{}, b{};
        const char* why = nullptr;
        Check(Identify("Halo 3", base, kImageSize, a, why), "stamp: identified");
        reinterpret_cast<IMAGE_NT_HEADERS64*>(image + 0x80)->FileHeader.TimeDateStamp ^= 1;
        Check(Identify("Halo 3", base, kImageSize, b, why) && a != b, "stamp: another timestamp is another build");
        reinterpret_cast<IMAGE_NT_HEADERS64*>(image + 0x80)->FileHeader.TimeDateStamp ^= 1;
        Check(!Identify("Halo 3", base, kImageSize - 0x1000, b, why) && why,
              "identity: a size that is not the image's own is refused");
        Identity c{};
        Check(Identify("ODST", base, kImageSize, c, why) && c != a, "identity: another title is another entry");
    }

    // A file that is not whole: not used, and the next save replaces it.
    {
        HANDLE f = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        Check(f != INVALID_HANDLE_VALUE, "corrupt: open the file");
        if (f != INVALID_HANDLE_VALUE)
        {
            SetFilePointer(f, 40, nullptr, FILE_BEGIN);
            const uint8_t junk = 0x5A;
            DWORD written = 0;
            WriteFile(f, &junk, 1, &written, nullptr);
            CloseHandle(f);
        }
        static sig::ImageMemo seventh;
        const Launch l7 = RunLaunch(seventh, base, 1);
        Check(l7.loaded.result == LoadResult::NotWhole && l7.counts.fromSaved == 0 && l7.counts.searched == 7,
              "corrupt: nothing is taken from it, every search is made");
        Check(l7.saved, "corrupt: and the save replaces it");
        static sig::ImageMemo eighth;
        const Launch l8 = RunLaunch(eighth, base, 1);
        Check(l8.loaded.result == LoadResult::Loaded && l8.counts.fromSaved == 6, "corrupt: whole again after");
    }

    // Forgetting a build (a hook of it was never called): the next launch searches for everything and saves it
    // again; another build's answers stay.
    {
        static sig::ImageMemo mine, others, after, again;
        const Launch kept = RunLaunch(mine, base, 1);
        image[kText + 0x2F00] ^= 0x01;
        const Launch other = RunLaunch(others, base, 1);
        image[kText + 0x2F00] ^= 0x01;
        Check(kept.identified && other.identified && other.identity != kept.identity, "forget: two builds");
        const char* why = nullptr;
        Check(Forget(kept.identity, why) && !why, "forget: done");
        const Launch l = RunLaunch(after, base, 1);
        Check(l.loaded.result == LoadResult::NoEntry && l.loaded.builds == 1, "forget: only the other build is left");
        Check(l.counts.fromSaved == 0 && l.counts.searched == 7 && l.saved, "forget: every search made, and saved");
        const Launch m = RunLaunch(again, base, 1);
        Check(m.loaded.result == LoadResult::Loaded && m.counts.fromSaved == 6 && m.loaded.builds == 2,
              "forget: the launch after has it back");
    }

    // A file that cannot be read (another program holds it): nothing taken, nothing written over it.
    {
        HANDLE held = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        Check(held != INVALID_HANDLE_VALUE, "held: another program holds the file");
        static sig::ImageMemo ninth;
        sig::ImageMemoScope scope(ninth, base, kImageSize, 1, true);
        Identity id{};
        const char* why = nullptr;
        Check(Identify("Halo 3", base, kImageSize, id, why), "held: identified");
        const LoadReport report = Load(id, ninth);
        Check(report.result == LoadResult::Unreadable && report.taken == 0, "held: nothing is taken from it");
        (void)Ask(base, kImageSize, kThree);
        double ms = 0.0;
        Check(ninth.changes > 0 && !Save(id, ninth, why, ms) && why, "held: and nothing is written over it");
        if (held != INVALID_HANDLE_VALUE)
            CloseHandle(held);
    }

    // No folder: nothing read, nothing saved.
    SetFile(nullptr);
    {
        static sig::ImageMemo tenth;
        const Launch l10 = RunLaunch(tenth, base, 1);
        Check(l10.loaded.result == LoadResult::NoFolder && !l10.saved && l10.counts.searched == 7,
              "no folder: every search made, nothing saved");
    }
    DeleteFileW(path.c_str());
    VirtualFree(image, 0, MEM_RELEASE);
}

// ----------------------------------------------------- this test's own executable, as the loader mapped it
__declspec(noinline) int HookedHere(int x) { return x * 3 + 1; }
__declspec(noinline) int Detour(int) { return -1; }

uint32_t Patches(uintptr_t base, size_t size, sig::HookPatch* out, uint32_t capacity)
{
    MCCVR_HOOK_PATCH local[16]{};
    const UINT total = MCCVR_CopyEnabledHookPatches(reinterpret_cast<const void*>(base), size, local, 16);
    for (UINT i = 0; i < total && i < 16 && i < capacity; ++i)
    {
        out[i].address = reinterpret_cast<uintptr_t>(local[i].address);
        out[i].size = local[i].size;
        std::memcpy(out[i].original, local[i].original, sizeof(out[i].original));
    }
    return total;
}

void OwnModuleTests()
{
    uintptr_t base = 0;
    size_t size = 0;
    Check(sig::SelfModuleRange(base, size), "own module: its range");
    Identity before{}, again{}, hooked{}, hookedRaw{};
    const char* why = nullptr;
    LARGE_INTEGER began{}, ended{}, frequency{};
    QueryPerformanceCounter(&began);
    Check(Identify("test", base, size, before, why), "own module: identified as the loader mapped it");
    QueryPerformanceCounter(&ended);
    QueryPerformanceFrequency(&frequency);
    std::printf("own module: %llu bytes of code hashed in %.2f ms\n",
                static_cast<unsigned long long>(before.codeBytes),
                static_cast<double>(ended.QuadPart - began.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart));
    Check(before.codeBytes > 0 && before.timeDateStamp != 0, "own module: code and stamp read");
    Check(Identify("test", base, size, again, why) && again == before, "own module: the same build twice");

    // Hooked by this mod: the identity is the same, because the hook's bytes are undone.
    Check(MH_Initialize() == MH_OK, "own module: MinHook");
    void* original = nullptr;
    Check(MH_CreateHook(reinterpret_cast<void*>(&HookedHere), reinterpret_cast<void*>(&Detour), &original) == MH_OK &&
              MH_EnableHook(reinterpret_cast<void*>(&HookedHere)) == MH_OK,
          "own module: a function of it hooked");
    volatile int seven = 7;
    Check(HookedHere(seven) == -1, "own module: the hook is live");
    Check(Identify("test", base, size, hookedRaw, why) && hookedRaw != before,
          "own module: read raw, a hook changes the code");
    sig::SetHookPatchSource(&Patches);
    Check(Identify("test", base, size, hooked, why) && hooked == before,
          "own module: with this mod's hooks undone, the same build");
    sig::SetHookPatchSource(nullptr);
    Check(MH_DisableHook(reinterpret_cast<void*>(&HookedHere)) == MH_OK &&
              MH_RemoveHook(reinterpret_cast<void*>(&HookedHere)) == MH_OK && MH_Uninitialize() == MH_OK,
          "own module: unhooked");
}
} // namespace

int main()
{
    FileTests();
    LaunchTests();
    OwnModuleTests();
    if (!failures)
        std::printf("install search cache: all checks passed\n");
    return failures ? 1 : 0;
}
