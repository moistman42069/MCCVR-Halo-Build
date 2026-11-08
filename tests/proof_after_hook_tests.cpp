// A BYTE-PATTERN PROOF READS THE GAME'S CODE, NOT OUR JUMPS.
//
// A lane whose proof of a function's head runs after another lane of this mod has hooked that function is refused
// for every player if it compares live bytes (the muzzle lanes' functions are shared this way: Halo 4 +0x3B1B4C,
// Reach +0x484F24). MinHook rewrites the first five bytes of a hooked function, so a live-bytes pattern reads
// `E9 <rel32>` and does not match.
//
// This test runs the production scanner (src/dll/sigscan.cpp) and the production MinHook extension
// (src/common/minhook_lifecycle.c, MCCVR_CopyEnabledHookPatches) against REAL MinHook hooks on synthetic code in
// this process. No MCC process, module, config, installation or game data is touched.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../src/common/minhook_lifecycle.h"
#include "../src/dll/sigscan.h"

static int failures = 0;
static unsigned checks = 0;
static void Check(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}

__declspec(noinline) static int DetourA() { return 41; }
__declspec(noinline) static int DetourB() { return 42; }

// The adapter the DLL installs (dllmain.cpp, EnabledHookPatches), in its simplest form.
static uint32_t Patches(uintptr_t base, size_t size, sig::HookPatch* out, uint32_t capacity)
{
    std::vector<MCCVR_HOOK_PATCH> local(capacity ? capacity : 1);
    const UINT total = MCCVR_CopyEnabledHookPatches(reinterpret_cast<LPCVOID>(base), size, local.data(), capacity);
    for (uint32_t i = 0; i < total && i < capacity; ++i)
    {
        out[i].address = reinterpret_cast<uintptr_t>(local[i].address);
        out[i].size = local[i].size;
        std::memcpy(out[i].original, local[i].original, sizeof(out[i].original));
    }
    return total;
}

static unsigned observed = 0;
static uintptr_t observedMatch = 0, observedPatch = 0;
static size_t observedLength = 0;
static void Observer(uintptr_t match, size_t length, uintptr_t patch)
{
    ++observed;
    observedMatch = match;
    observedLength = length;
    observedPatch = patch;
}

// What a lane does: the pattern must sit at its pinned place, once.
static bool ProvenUniqueAt(uintptr_t base, size_t size, uintptr_t expected, const char* pattern)
{
    const uintptr_t first = sig::Find(base, size, pattern);
    return first == expected && !sig::Find(first + 1, base + size - first - 1, pattern);
}

int main()
{
    Check(MH_Initialize() == MH_OK, "initialize local test hooks");
    constexpr size_t kSize = 0x4000;
    auto* image = static_cast<uint8_t*>(VirtualAlloc(nullptr, kSize, MEM_RESERVE | MEM_COMMIT,
                                                     PAGE_EXECUTE_READWRITE));
    if (!image)
        return 1;
    const uintptr_t base = reinterpret_cast<uintptr_t>(image);
    std::memset(image, 0xCC, kSize);

    // Two "native functions". A's prologue is the shape of the real ones (mov rax,rsp; push rbp; ...), followed
    // by a body the pattern also covers; B is a second function elsewhere.
    //   A: 48 8B C4 / 55 / 53 / 48 83 EC 28 / B8 07 00 00 00 / 48 83 C4 28 / 5B / 5D / C3
    const uint8_t functionA[]{0x48, 0x8B, 0xC4, 0x55, 0x53, 0x48, 0x83, 0xEC, 0x28, 0xB8, 0x07, 0x00, 0x00, 0x00,
                              0x48, 0x83, 0xC4, 0x28, 0x5B, 0x5D, 0xC3};
    //   B: 48 83 EC 38 / B8 09 00 00 00 / 48 83 C4 38 / C3
    const uint8_t functionB[]{0x48, 0x83, 0xEC, 0x38, 0xB8, 0x09, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x38, 0xC3};
    constexpr size_t kA = 0x1000, kB = 0x2000;
    std::memcpy(image + kA, functionA, sizeof(functionA));
    std::memcpy(image + kB, functionB, sizeof(functionB));
    using Function = int (*)();
    const auto callA = reinterpret_cast<Function>(image + kA);
    const auto callB = reinterpret_cast<Function>(image + kB);
    Check(callA() == 7 && callB() == 9, "synthetic natives run");

    const char* headA = "48 8B C4 55 53 48 83 EC 28 B8 07 00 00 00";        // starts at A's head
    const char* insideA = "53 48 83 EC 28 B8 ?? 00 00 00 48 83 C4 28";      // starts at A+4: the hook's last byte
    const char* bodyA = "B8 07 00 00 00 48 83 C4 28 5B 5D C3";              // A+9: clear of the hook
    const char* headB = "48 83 EC 38 B8 09 00 00 00";
    const char* acrossIntoA = "CC CC CC 48 8B C4 55 53";                    // starts 3 bytes ABOVE A's head

    // ---- before any hook: everything proves, with or without the patch source -----------------------------
    Check(ProvenUniqueAt(base, kSize, base + kA, headA) && ProvenUniqueAt(base, kSize, base + kB, headB),
          "unhooked image: heads prove");
    sig::SetHookPatchSource(&Patches);
    sig::SetProofThroughHookObserver(&Observer);
    Check(ProvenUniqueAt(base, kSize, base + kA, headA) && ProvenUniqueAt(base, kSize, base + kA + 4, insideA) &&
              ProvenUniqueAt(base, kSize, base + kA + 9, bodyA) && observed == 0,
          "patch source set, nothing hooked: same answers and nothing reported");

    // ---- lane 1 hooks A (the "core"); lane 2 then proves A's head (the "muzzle lane") ----------------------
    Function originalA = nullptr;
    Check(MH_CreateHook(image + kA, reinterpret_cast<void*>(&DetourA), reinterpret_cast<void**>(&originalA)) == MH_OK,
          "create hook on A");
    Check(ProvenUniqueAt(base, kSize, base + kA, headA) && observed == 0,
          "a created but not yet enabled hook has rewritten nothing");
    Check(MH_EnableHook(image + kA) == MH_OK && callA() == 41 && originalA() == 7, "A is hooked and live");
    Check(image[kA] == 0xE9, "the head of a hooked function reads our jump (what the lanes used to compare)");

    // CONTROL: a scanner with no patch source reads the jump and refuses the lane. This is the defect guarded against.
    sig::SetHookPatchSource(nullptr);
    Check(sig::Find(base, kSize, headA) == 0, "control: a live-bytes scan no longer finds a hooked head");
    Check(!ProvenUniqueAt(base, kSize, base + kA, headA), "control: the lane's proof fails after the hook");
    Check(sig::Find(base, kSize, bodyA) == base + kA + 9, "control: bytes clear of the hook still match");
    sig::SetHookPatchSource(&Patches);

    // THE FIX: the same proof, taken after the hook, reads the bytes MinHook saved.
    observed = 0;
    Check(ProvenUniqueAt(base, kSize, base + kA, headA), "a proof taken after the hook reads the game's own bytes");
    Check(observed >= 1 && observedMatch == base + kA && observedPatch == base + kA && observedLength == 14,
          "and it is reported: where it matched, how long, which hook it read through");
    observed = 0;
    // (Reported unless the jump's last byte happens to equal the byte it replaced, when the live bytes match too.)
    Check(ProvenUniqueAt(base, kSize, base + kA + 4, insideA) &&
              ((observed >= 1 && observedMatch == base + kA + 4) || image[kA + 4] == functionA[4]),
          "a pattern that starts inside the rewritten bytes proves and is reported");
    observed = 0;
    Check(ProvenUniqueAt(base, kSize, base + kA - 3, acrossIntoA) && observed >= 1,
          "a pattern that starts above the head and runs into it proves and is reported");
    observed = 0;
    Check(ProvenUniqueAt(base, kSize, base + kA + 9, bodyA) && ProvenUniqueAt(base, kSize, base + kB, headB) &&
              observed == 0,
          "proofs that read no hooked byte are not reported");

    // The uniqueness re-scan must not be fooled either way: a second copy of A's head elsewhere IS ambiguous.
    std::memcpy(image + 0x3000, functionA, sizeof(functionA));
    Check(sig::Find(base, kSize, headA) == base + kA &&
              sig::Find(base + kA + 1, kSize - kA - 1, headA) == base + 0x3000 &&
              !ProvenUniqueAt(base, kSize, base + kA, headA),
          "a real second copy is still ambiguous, with the first one hooked");
    std::memset(image + 0x3000, 0xCC, sizeof(functionA));

    // A live match that exists ONLY because of our jump is not a match of the game's code: a pattern made of
    // the jump the hook wrote, then A's next instructions, matches the live bytes and must not be "found".
    uint8_t jump[5];
    std::memcpy(jump, image + kA, 5);
    char jumpPattern[64];
    std::snprintf(jumpPattern, sizeof(jumpPattern), "%02X %02X %02X %02X %02X 48 83 EC 28 B8 07", jump[0], jump[1],
                  jump[2], jump[3], jump[4]);
    sig::SetHookPatchSource(nullptr);
    Check(sig::Find(base, kSize, jumpPattern) == base + kA, "control: the live bytes do hold our jump there");
    sig::SetHookPatchSource(&Patches);
    Check(sig::Find(base, kSize, jumpPattern) == 0,
          "bytes that are only our jump are not the game's code: no match at the hooked head");

    // CopyUnhooked: a fixed-place compare reads the saved bytes too.
    uint8_t copy[sizeof(functionA) + 6];
    Check(sig::CopyUnhooked(base + kA - 3, copy, sizeof(copy)) == 1 &&
              std::memcmp(copy + 3, functionA, sizeof(functionA)) == 0 && copy[0] == 0xCC,
          "CopyUnhooked returns the function as the game shipped it");
    Check(std::memcmp(image + kA, functionA, 5) != 0, "while the live head still differs");

    // ---- a second hook, and scans that start past or end before a hook -------------------------------------
    Function originalB = nullptr;
    Check(MH_CreateHook(image + kB, reinterpret_cast<void*>(&DetourB), reinterpret_cast<void**>(&originalB)) == MH_OK &&
              MH_EnableHook(image + kB) == MH_OK && callB() == 42,
          "B is hooked and live");
    observed = 0;
    Check(ProvenUniqueAt(base, kSize, base + kA, headA) && ProvenUniqueAt(base, kSize, base + kB, headB) &&
              observed >= 2,
          "two hooked heads prove; both are reported");
    Check(sig::Find(base + kA + 1, kSize - kA - 1, headA) == 0, "a scan that starts inside a hook finds no head there");
    Check(sig::Find(base, kA + 4, headA) == 0, "a scan that ends inside the pattern finds nothing");
    MCCVR_HOOK_PATCH listed[4]{};
    Check(MCCVR_CopyEnabledHookPatches(image, kSize, listed, 4) == 2 && listed[0].size == 5 && listed[1].size == 5,
          "MinHook lists both enabled patches in the image");
    Check(MCCVR_CopyEnabledHookPatches(image + kA + 5, 0x100, listed, 4) == 0,
          "and none in a range that touches neither");
    Check(MCCVR_CopyEnabledHookPatches(image + kA + 4, 1, listed, 4) == 1 &&
              listed[0].address == image + kA && std::memcmp(listed[0].original, functionA, 5) == 0,
          "a range that touches one byte of a patch lists it, with the bytes it replaced");

    // ---- disabled and removed hooks are not patches --------------------------------------------------------
    Check(MH_DisableHook(image + kA) == MH_OK && callA() == 7, "A disabled: its head is the game's again");
    observed = 0;
    Check(ProvenUniqueAt(base, kSize, base + kA, headA) && observed == 0 &&
              MCCVR_CopyEnabledHookPatches(image, kSize, listed, 4) == 1,
          "a disabled hook is neither undone nor reported");
    Check(MH_EnableHook(image + kA) == MH_OK, "A enabled again");
    Check(MCCVR_DisableHookForRetirement(image + kA) == MH_OK && MH_RemoveHook(image + kA) == MH_OK &&
              MCCVR_DisableHookForRetirement(image + kB) == MH_OK && MH_RemoveHook(image + kB) == MH_OK,
          "both hooks retired and removed");
    observed = 0;
    Check(MCCVR_CopyEnabledHookPatches(image, kSize, listed, 4) == 0 && ProvenUniqueAt(base, kSize, base + kA, headA) &&
              ProvenUniqueAt(base, kSize, base + kB, headB) && observed == 0,
          "after removal the image is the game's and nothing is reported");

    sig::SetHookPatchSource(nullptr);
    sig::SetProofThroughHookObserver(nullptr);
    VirtualFree(image, 0, MEM_RELEASE);
    Check(MH_Uninitialize() == MH_OK, "shutdown");
    Check(MCCVR_CopyEnabledHookPatches(&failures, 16, listed, 4) == 0, "an uninitialised MinHook lists nothing");
    if (failures)
        return 1;
    std::printf("PASS: %u proof-after-hook checks (real MinHook hooks on synthetic code)\n", checks);
    return 0;
}
