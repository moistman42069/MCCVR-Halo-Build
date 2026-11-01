// SEARCHES OF ONE LOADED IMAGE, REMEMBERED WHILE IT STAYS LOADED (src/dll/sigscan.h, ImageMemoScope).
//
// A title's hooks are installed more than once within one load of its DLL, and every install asks the same searches
// of the same bytes. Inside a scope the scanner answers a search it has made before from what it found, and this
// test holds it to that: the same answers as the search itself, and no search where the header says there is none.
//
// "No search" is shown the only way a caller could tell: the image is changed behind the memo's back in a way the
// game's own code never changes, and the remembered answer is the one that comes back. Each such change is also
// the stated limit of the memo; the checking mode and a new load of the image see through it.
//
// No MCC process, module, config, installation or game data is touched.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../src/dll/sigscan.h"

namespace
{
int failures = 0;
unsigned checks = 0;
void Check(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}

// A signature of twelve bytes with two wildcards, and the bytes that match it.
const char* const kOne = "4D 43 43 56 52 ?? 6F 6E 65 ? 01 02";
const uint8_t kOneBytes[12] = {0x4D, 0x43, 0x43, 0x56, 0x52, 0xAA, 0x6F, 0x6E, 0x65, 0xBB, 0x01, 0x02};
const char* const kTwo = "74 77 6F 2D 74 69 6D 65 73 11 22 33";
const uint8_t kTwoBytes[12] = {0x74, 0x77, 0x6F, 0x2D, 0x74, 0x69, 0x6D, 0x65, 0x73, 0x11, 0x22, 0x33};
const char* const kAbsent = "61 62 73 65 6E 74 2D 70 61 74 FE FD";
const uint8_t kAbsentBytes[12] = {0x61, 0x62, 0x73, 0x65, 0x6E, 0x74, 0x2D, 0x70, 0x61, 0x74, 0xFE, 0xFD};

constexpr size_t kSize = 0x40000;
constexpr size_t kOneAt = 0x12345;
constexpr size_t kTwoAt = 0x08000, kTwoAgainAt = 0x30000;

// The image as it is "loaded": bytes none of the signatures can match by accident (0x80 to 0x8F), with the
// once-only signature in it once and the other twice. Written into the same storage each time, so the image keeps
// its address, as a loaded module does.
void Fill(std::vector<uint8_t>& image)
{
    uint32_t state = 0x1234567u;
    for (uint8_t& b : image)
    {
        state = state * 1664525u + 1013904223u;
        b = static_cast<uint8_t>(0x80u | ((state >> 24) & 0x0Fu));
    }
    std::memcpy(image.data() + kOneAt, kOneBytes, sizeof(kOneBytes));
    std::memcpy(image.data() + kTwoAt, kTwoBytes, sizeof(kTwoBytes));
    std::memcpy(image.data() + kTwoAgainAt, kTwoBytes, sizeof(kTwoBytes));
}

// What a lane asks of a signature: where it is, and whether it is there once.
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

// One enabled hook of "this mod", for the scanner's hook-patch source.
sig::HookPatch g_patch{};
bool g_patchEnabled = false;
uint32_t Patches(uintptr_t base, size_t size, sig::HookPatch* out, uint32_t capacity)
{
    if (!g_patchEnabled || g_patch.address + g_patch.size <= base || g_patch.address >= base + size)
        return 0;
    if (capacity)
        out[0] = g_patch;
    return 1;
}
} // namespace

int main()
{
    std::vector<uint8_t> image(kSize);
    Fill(image);
    const uintptr_t base = reinterpret_cast<uintptr_t>(image.data());

    // The search itself, with no scope.
    const Asked one = Ask(base, kSize, kOne);
    const Asked two = Ask(base, kSize, kTwo);
    const Asked absent = Ask(base, kSize, kAbsent);
    Check(one.first == base + kOneAt && one.second == 0, "a signature that is in the image once is found, once");
    Check(two.first == base + kTwoAt && two.second == base + kTwoAgainAt, "one that is there twice is found twice");
    Check(absent.first == 0, "one that is not there is not found");

    static sig::ImageMemo memo;

    // The first install of this load: every search is made, and answered as the search answers.
    {
        sig::ImageMemoScope scope(memo, base, kSize, 1, true);
        const Asked a = Ask(base, kSize, kOne), b = Ask(base, kSize, kTwo), c = Ask(base, kSize, kAbsent);
        Check(a.first == one.first && a.second == one.second && b.first == two.first && b.second == two.second &&
              c.first == 0, "inside a scope the first answers are the search's");
        Check(scope.Counts().searched == 5 && scope.Counts().remembered == 0 && scope.Counts().searchedAgain == 0,
              "and all five searches were made");
    }

    // The next install in the same load: the same answers, and no search for a match or for "no second match".
    {
        sig::ImageMemoScope scope(memo, base, kSize, 1, true);
        const Asked a = Ask(base, kSize, kOne), b = Ask(base, kSize, kTwo), c = Ask(base, kSize, kAbsent);
        Check(a.first == one.first && a.second == one.second && b.first == two.first && b.second == two.second &&
              c.first == 0, "a second install gets the same answers");
        Check(scope.Counts().remembered == 4, "four of them from the first install's searches");
        Check(scope.Counts().searched == 1, "and the one that was not found is searched for again");
    }

    // "No search", shown: bytes the game's code never changes are changed behind the memo.
    {
        // A second copy of the once-only signature, after the first.
        std::memcpy(image.data() + 0x38000, kOneBytes, sizeof(kOneBytes));
        // A copy of the twice signature BEFORE its first match.
        std::memcpy(image.data() + 0x00400, kTwoBytes, sizeof(kTwoBytes));
        Check(Ask(base, kSize, kOne).second == base + 0x38000 && Ask(base, kSize, kTwo).first == base + 0x00400,
              "the search itself sees both changes");
        {
            sig::ImageMemoScope scope(memo, base, kSize, 1, true);
            const Asked a = Ask(base, kSize, kOne), b = Ask(base, kSize, kTwo);
            Check(a.first == one.first && a.second == 0, "'no second match' is taken as it was found");
            Check(b.first == two.first, "a match that still stands is taken where it was found");
            Check(scope.Counts().searched == 0 && scope.Counts().remembered == 4, "with no search at all");
        }
        // The checking mode makes every search and returns its answers, and counts what the memo had wrong.
        {
            sig::ImageMemoScope scope(memo, base, kSize, 1, true, true);
            const Asked a = Ask(base, kSize, kOne), b = Ask(base, kSize, kTwo);
            Check(a.second == base + 0x38000 && b.first == base + 0x00400, "checking returns the search's answers");
            Check(scope.Counts().disagreed >= 2, "and counts the remembered answers it did not confirm");
        }
        // Another load of the image starts from nothing.
        {
            sig::ImageMemoScope scope(memo, base, kSize, 2, true);
            const Asked a = Ask(base, kSize, kOne), b = Ask(base, kSize, kTwo);
            Check(a.second == base + 0x38000 && b.first == base + 0x00400, "a new load of the image is searched");
            Check(scope.Counts().remembered == 0, "with nothing remembered from the load before");
        }
        Fill(image);
    }
    Check(reinterpret_cast<uintptr_t>(image.data()) == base, "the image is rebuilt in place");

    // A remembered match that no longer stands is searched for again.
    {
        { sig::ImageMemoScope scope(memo, base, kSize, 3, true); (void)Ask(base, kSize, kOne); }
        image[kOneAt + 3] ^= 0xFF;                                                    // the match is gone ...
        std::memcpy(image.data() + 0x20000, kOneBytes, sizeof(kOneBytes));           // ... and stands elsewhere
        sig::ImageMemoScope scope(memo, base, kSize, 3, true);
        const Asked a = Ask(base, kSize, kOne);
        Check(a.first == base + 0x20000 && a.second == 0, "a match that no longer stands is found again by search");
        Check(scope.Counts().searchedAgain == 1, "and the scope says so");
        Fill(image);
    }

    // A signature this mod itself writes: not found before, found after.
    {
        { sig::ImageMemoScope scope(memo, base, kSize, 4, true); Check(!sig::Find(base, kSize, kAbsent), "absent"); }
        std::memcpy(image.data() + 0x11000, kAbsentBytes, sizeof(kAbsentBytes));
        sig::ImageMemoScope scope(memo, base, kSize, 4, true);
        Check(sig::Find(base, kSize, kAbsent) == base + 0x11000,
              "a first search that found nothing is made again, so bytes written since are found");
        Fill(image);
    }

    // The game's bytes under one of this mod's enabled hooks: the memo answers as the search does, hooked or not.
    {
        sig::SetHookPatchSource(Patches);
        g_patch.address = base + kOneAt;
        g_patch.size = 5;
        std::memcpy(g_patch.original, kOneBytes, 5);
        const auto hook = [&](bool enable) {
            g_patchEnabled = enable;
            if (enable)
            {
                const uint8_t jump[5] = {0xE9, 0x10, 0x20, 0x30, 0x40};
                std::memcpy(image.data() + kOneAt, jump, 5);
            }
            else
            {
                std::memcpy(image.data() + kOneAt, kOneBytes, 5);
            }
        };
        // Remembered with the hook in, asked again with it out, and the other way round.
        hook(true);
        { sig::ImageMemoScope scope(memo, base, kSize, 5, true);
          Check(Ask(base, kSize, kOne).first == one.first, "searched through this mod's hook"); }
        hook(false);
        { sig::ImageMemoScope scope(memo, base, kSize, 5, true);
          const Asked a = Ask(base, kSize, kOne);
          Check(a.first == one.first && a.second == 0 && scope.Counts().searched == 0,
                "remembered with the hook in, taken again with it out"); }
        hook(true);
        { sig::ImageMemoScope scope(memo, base, kSize, 5, true);
          const Asked a = Ask(base, kSize, kOne);
          Check(a.first == one.first && a.second == 0 && scope.Counts().searched == 0 &&
                scope.Counts().searchedAgain == 0, "and with it in again: the compare reads the game's bytes"); }
        hook(false);
        sig::SetHookPatchSource(nullptr);
    }

    // What a scope does not touch.
    {
        std::vector<uint8_t> other(kSize);
        Fill(other);
        const uintptr_t otherBase = reinterpret_cast<uintptr_t>(other.data());
        { sig::ImageMemoScope scope(memo, base, kSize, 6, true); (void)Ask(base, kSize, kOne); }
        sig::ImageMemoScope scope(memo, base, kSize, 6, true);
        std::memcpy(other.data() + 0x00100, kOneBytes, sizeof(kOneBytes));
        Check(sig::Find(otherBase, kSize, kOne) == otherBase + 0x00100, "a search of another image is always made");
        Check(scope.Covers(base, kSize) && scope.Covers(base + 0x100, 0x200) && !scope.Covers(base + 0x10, kSize) &&
              !scope.Covers(base - 1, 0x10) && !scope.Covers(base, 0),
              "a range is the scope's only when all of it lies inside the scope's image");
        Check(scope.Counts().searched == 0 && scope.Counts().remembered == 0, "a search elsewhere is not the memo's");
        // A sub-range of the image is its own search.
        Check(sig::Find(base + 0x20000, 0x10000, kOne) == 0 && scope.Counts().searched == 1,
              "a part of the image is searched as its own range");
    }

    // A scope that is not to be used, and a load nobody could number, search every time.
    {
        { sig::ImageMemoScope scope(memo, base, kSize, 7, true); (void)Ask(base, kSize, kOne); }
        std::memcpy(image.data() + 0x38000, kOneBytes, sizeof(kOneBytes));
        { sig::ImageMemoScope scope(memo, base, kSize, 7, false);
          Check(Ask(base, kSize, kOne).second == base + 0x38000 && scope.Counts().remembered == 0 &&
                scope.Counts().searched == 2, "a scope switched off searches, and still counts its searches"); }
        { sig::ImageMemoScope scope(memo, base, kSize, 0, true);
          Check(Ask(base, kSize, kOne).second == base + 0x38000 && scope.Counts().remembered == 0 &&
                scope.Counts().searched == 2, "a load with no number searches"); }
        // The switched-off scopes left the memo of load 7 as it was.
        { sig::ImageMemoScope scope(memo, base, kSize, 7, true);
          Check(Ask(base, kSize, kOne).second == 0 && scope.Counts().remembered == 2,
                "and the memo of that load is still what it was"); }
        Fill(image);
    }

    // More searches than the memo holds: the rest are made, every time, and answered rightly.
    {
        static sig::ImageMemo full;
        const auto askMany = [&](sig::ImageMemoScope& scope) {
            bool right = true;
            for (uint32_t i = 0; i < sig::ImageMemo::kCapacity + 40; ++i)
            {
                // Each a different range (so a different search) that still holds the signature.
                const uintptr_t found = sig::Find(base + i, kSize - i, kOne);
                right = right && found == base + kOneAt;
            }
            (void)scope;
            return right;
        };
        { sig::ImageMemoScope scope(full, base, kSize, 1, true);
          Check(askMany(scope) && scope.Counts().notHeld == 40, "a full memo keeps what it can"); }
        { sig::ImageMemoScope scope(full, base, kSize, 1, true);
          Check(askMany(scope) && scope.Counts().remembered == sig::ImageMemo::kCapacity &&
                scope.Counts().searched == 40, "and the searches it could not keep are made again"); }
    }

    // A scope inside a scope: the inner one answers while it lives, the outer one after.
    {
        static sig::ImageMemo outerMemo, innerMemo;
        std::vector<uint8_t> other(kSize);
        Fill(other);
        const uintptr_t otherBase = reinterpret_cast<uintptr_t>(other.data());
        sig::ImageMemoScope outer(outerMemo, base, kSize, 1, true);
        (void)sig::Find(base, kSize, kOne);
        {
            sig::ImageMemoScope inner(innerMemo, otherBase, kSize, 1, true);
            (void)sig::Find(otherBase, kSize, kOne);
            (void)sig::Find(otherBase, kSize, kOne);
            Check(inner.Counts().searched == 1 && inner.Counts().remembered == 1, "the inner scope answers for its image");
        }
        (void)sig::Find(base, kSize, kOne);
        Check(outer.Counts().searched == 1 && outer.Counts().remembered == 1, "and the outer one again after it");
    }

    if (failures)
        std::fprintf(stderr, "%d of %u checks failed\n", failures, checks);
    else
        std::printf("sigscan memo: %u checks passed\n", checks);
    return failures ? 1 : 0;
}
