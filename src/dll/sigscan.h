#pragma once
#include <cstdint>
#include <cstddef>

// AOB (array-of-bytes) signature scanning. We locate game code/data by the
// bytes of the surrounding instructions rather than hardcoded addresses, so
// the mod keeps working when an MCC update shifts everything around. When a
// pattern stops matching after a patch, we detect it and warn instead of
// crashing (that's the whole reason we scan).

namespace sig
{
    // Pattern is space-separated hex bytes with "??" as a wildcard, e.g.
    //   "48 8B 05 ?? ?? ?? ?? 48 85 C0"
    // Returns the address of the first match inside [base, base+size), or 0.
    //
    // THE IMAGE THAT IS MATCHED IS THE GAME'S, NOT THE ONE WE HAVE REWRITTEN.
    // Where this mod has an enabled hook (SetHookPatchSource) the bytes compared
    // are the ones that stood there before the hook, so a proof does not depend
    // on whether it runs before or after another lane hooked the same function.
    // Two muzzle lanes were lost to that order: Halo 4's (+0x3B1B4C, the avatar
    // lane's producer hook) and Reach's (+0x484F24, the unit-adjust hook), each
    // logging "missing/ambiguous binding" on a healthy game, for every player.
    // A match that had to read through one of our hooks is reported once
    // (SetProofThroughHookObserver): it still wants moving ahead of the hook,
    // because a direct byte compare at the same address has no such net.
    uintptr_t Find(uintptr_t base, size_t size, const char* pattern);

    // One enabled hook of this mod: the bytes it overwrote, and what stood
    // there before.
    struct HookPatch
    {
        uintptr_t address = 0;
        uint8_t size = 0;
        uint8_t original[8]{};
    };
    // Copies up to `capacity` enabled patches that intersect [base, base+size)
    // and returns how many there are in all.
    using HookPatchSource = uint32_t (*)(uintptr_t base, size_t size,
                                         HookPatch* out, uint32_t capacity);
    // `match` is where a pattern of `length` bytes matched only because the
    // hook at `patch` was read as the bytes it replaced.
    using ProofThroughHookObserver = void (*)(uintptr_t match, size_t length,
                                              uintptr_t patch);
    // Set once at startup, before any title is scanned. Null (the default)
    // scans the live bytes exactly as before.
    void SetHookPatchSource(HookPatchSource source);
    void SetProofThroughHookObserver(ProofThroughHookObserver observer);

    // For a scanner that walks an image itself and counts matches (Halo 2's
    // guarded region walker, CountPatternMatches). Given the result of its LIVE
    // scan of [base, base+size) - the first match, and the count, counted at
    // least up to 2 - makes it the result of the same scan over the image with
    // this mod's enabled hooks undone. Exact while the live count is below 2; a
    // live count of 2 or more stays ambiguous. `wild[i]` != 0 is a wildcard.
    // Reads only bytes within n-1 of an enabled hook, inside [base, base+size);
    // allocates nothing. Reports a match read through a hook like Find does.
    void CorrectLiveCountForHooks(uintptr_t base, size_t size,
                                  const uint8_t* bytes, const uint8_t* wild,
                                  size_t n, uintptr_t& first, uint32_t& count);

    // The bytes at [address, address+size) with this mod's enabled hooks
    // undone, for a proof that compares bytes at a fixed place instead of
    // scanning. Returns how many hooks it read through. The range must be
    // readable.
    uint32_t CopyUnhooked(uintptr_t address, void* out, size_t size);

    // Same, but over a loaded module's image (by name, e.g. L"halo3.dll").
    // Returns 0 if the module isn't loaded or the pattern isn't found.
    uintptr_t FindInModule(const wchar_t* moduleName, const char* pattern);

    // Resolve a 32-bit RIP-relative reference (the addressing mode x64 uses for
    // things like `mov rax,[rip+disp32]`). dispAddr points at the 4-byte
    // displacement; nextInstr is the address of the instruction that follows
    // (RIP's value when the displacement is applied). Returns the target.
    uintptr_t RipTarget(uintptr_t dispAddr, uintptr_t nextInstr);

    // Base address and size of a loaded module's image, for bounding a scan.
    bool ModuleRange(const wchar_t* moduleName, uintptr_t& base, size_t& size);

    // Base address and size of the module containing this code, resolved by
    // address rather than file name, so it survives the DLL being renamed.
    bool SelfModuleRange(uintptr_t& base, size_t& size);

    // SEARCHES OF ONE LOADED IMAGE, REMEMBERED WHILE IT STAYS LOADED.
    //
    // A title's hooks are installed more than once within one load of its DLL
    // (Halo 3: at each new level), and every install asks the same
    // questions of the same bytes: where is this signature, and is there a
    // second match after it. The second question has no early way out: it
    // reads the image to its end, every time. The answers cannot change while
    // that image stays loaded, because Find answers for the game's own bytes
    // (this mod's enabled hooks undone, above). So inside an ImageMemoScope a
    // Find over a range of the scope's image is answered from what the same
    // search of the same range found earlier in this load of the image:
    //   * a match is compared again where it was found, by the compare a
    //     search makes there. If it no longer stands (a patch of this mod's
    //     that is not a hook), the range is searched again;
    //   * "no second match" is taken as found: the answer to a search that
    //     begins right after a remembered match of the same pattern and runs
    //     to the image's end. Only the search itself could check it again, and
    //     nothing this mod writes makes a second copy of a signature;
    //   * any other "no match" is searched for again. A pattern can name bytes
    //     this mod itself writes (a patch that is not a hook), and those come
    //     to match later.
    // A search of anything else (another module, a range that is not inside
    // the image) is made as always.
    //
    // One memo is one load of one image: a scope opened for another base,
    // size or generation empties it first. It holds offsets and a hash of each
    // pattern's text, no pointers. It is used by one thread at a time (a scope
    // is the calling thread's own). When it is full, the searches it cannot
    // hold are simply made.
    //
    // ANSWERS SAVED BY AN EARLIER LAUNCH (install_search_cache.h) are put in an
    // emptied memo as entries marked `saved`, and are answered by exactly the
    // rules above: a match is compared again in place, "no second match" is
    // taken, any other "no match" is searched for again. They are counted apart
    // (fromSaved, savedStale). `changes` counts what this load's searches added
    // or corrected, so the caller knows whether there is anything to save.
    struct ImageMemo
    {
        static constexpr uint32_t kCapacity = 384;
        static constexpr uint32_t kNoMatch = 0xFFFFFFFFu;
        struct Entry
        {
            uint64_t pattern = 0;   // a hash of the pattern's text
            uint32_t start = 0;     // the searched range, as offsets in the image
            uint32_t length = 0;
            uint32_t match = 0;     // the offset of the first match, or kNoMatch
            uint8_t saved = 0;      // 1: put here from an earlier launch's saved answers, not searched in this load
        };
        uintptr_t base = 0;
        size_t size = 0;
        uint32_t generation = 0;
        uint32_t count = 0;
        uint32_t changes = 0;        // entries added or corrected by a search since the memo was emptied or saved
        bool savedLoaded = false;    // an earlier launch's answers were looked for in this load of the image
        uint32_t lastKnown = 0;      // answers the last install took without a search (the caller keeps this)
        Entry entries[kCapacity]{};
    };
    struct ImageMemoCounts
    {
        uint32_t searched = 0;       // searches made: not asked before in this load of the image, or a
                                     // "no match" of the kind that is asked again
        uint32_t remembered = 0;     // answers taken from an earlier search
        uint32_t searchedAgain = 0;  // a remembered match no longer stood, so its range was searched again
        uint32_t notHeld = 0;        // searches the memo had no room to keep
        uint32_t disagreed = 0;      // checking only: remembered answers the search did not confirm
        uint32_t fromSaved = 0;      // answers taken from an earlier launch's saved answers (not in `remembered`)
        uint32_t savedStale = 0;     // of `searchedAgain`: a saved match that no longer stood
        double ms = 0.0;             // the time spent answering, searches and compares together
    };
    class ImageMemoScope
    {
    public:
        // `generation` is the image's load count from whoever tracks its
        // loads; 0 (not known) and `use` false both mean every search is made,
        // as with no scope, and still counted. `check`: every remembered
        // answer is searched for as well, the search's answer is the one
        // returned, and a difference is counted. That proves the memo against
        // a running game and costs every search.
        ImageMemoScope(ImageMemo& memo, uintptr_t base, size_t size, uint32_t generation, bool use,
                       bool check = false);
        ~ImageMemoScope();
        ImageMemoScope(const ImageMemoScope&) = delete;
        ImageMemoScope& operator=(const ImageMemoScope&) = delete;
        const ImageMemoCounts& Counts() const { return counts_; }
        // Find, for a range inside this scope's image.
        uintptr_t Answer(uintptr_t base, size_t size, const char* pattern);
        bool Covers(uintptr_t base, size_t size) const;

    private:
        uintptr_t AnswerUntimed(uintptr_t base, size_t size, const char* pattern);
        ImageMemo* memo_ = nullptr;      // null: this scope only counts
        ImageMemoScope* outer_ = nullptr;
        uintptr_t base_ = 0;
        size_t size_ = 0;
        bool registered_ = false;
        bool check_ = false;
        ImageMemoCounts counts_{};
    };
}
