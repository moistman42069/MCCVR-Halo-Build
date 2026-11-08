#pragma once

#include <cstddef>
#include <cstdint>

#include "sigscan.h"
#include "../common/install_search_cache_logic.h"

// THE SEARCHES OF A TITLE'S INSTALL, SAVED FOR THE NEXT LAUNCH: the DLL's side (the rules and the file's format are
// in install_search_cache_logic.h).
//
// The order is the address cascade's: what is known is tried first and verified in place, the search is made only
// where that fails, and what the search found is saved for the next launch.
//   1. Identify: the loaded game DLL's build (install_search_cache::Identity), read from its headers and its
//      executable sections in memory with this mod's hooks undone.
//   2. Load: that build's saved answers go into the scanner's memo of this load of the DLL (sigscan.h, ImageMemo),
//      marked saved. The scanner then answers each as it answers one it remembers: a match only after the compare
//      a search makes at that place has passed again, "no match" only after searching again.
//   3. Save: when this load's searches added or corrected anything, the memo is written back for this build.
//
// HaloMCCVR-install-searches.bin in the mod's own folder, beside its settings and its log. It is a cache, not a
// setting: written by the mod, never trusted without the checks above, safe to delete. A file that cannot be read
// (another program has it open) is left alone: nothing is taken from it and nothing is saved over it. A file that
// is not whole is not used, and the next save replaces it. install_search_cache = 0 turns reading and writing off.
//
// Every call is made from the worker thread that installs the titles, one at a time; nothing here is for a game,
// render or present thread. Each opens the file at most once.
namespace install_search_cache
{
    // `folder` ends with a separator. Until this is called there is no file: nothing is read or saved.
    void SetFolder(const wchar_t* folder);
    // The file named outright (tests).
    void SetFile(const wchar_t* path);

    // The build of the DLL loaded at [base, base + size). False, with the reason, when its headers cannot be read
    // as this mod reads a PE image, or its code could not be read whole.
    bool Identify(const char* title, uintptr_t base, size_t size, Identity& out, const char*& why);

    enum class LoadResult : uint8_t
    {
        NoFolder,       // SetFolder was never called
        NoFile,         // no file: the first launch, or it was deleted
        Unreadable,     // the file is there and could not be read
        NotWhole,       // read, and not a whole file of this format: not used
        NoEntry,        // a whole file, with nothing for this build (it may hold other builds)
        Loaded,         // this build's answers are in the memo
    };
    struct LoadReport
    {
        LoadResult result = LoadResult::NoFolder;
        uint32_t taken = 0;       // answers put in the memo
        uint32_t builds = 0;      // builds the file holds
        double ms = 0.0;          // reading and parsing the file
    };
    // Puts this build's saved answers in `memo`, which must already be this load's (an ImageMemoScope for the
    // image was opened). An answer the memo already holds for the same search is left as the memo has it.
    LoadReport Load(const Identity& identity, sig::ImageMemo& memo);

    // Writes the memo's answers as this build's, keeping every other build the file holds, and on success sets
    // memo.changes to 0. False, with the reason, when nothing was written.
    bool Save(const Identity& identity, sig::ImageMemo& memo, const char*& why, double& ms);

    // Takes this build's answers out of the file, keeping every other build: the next launch searches for them.
    // True when the file no longer holds them (it may never have). False, with the reason, when it still does.
    bool Forget(const Identity& identity, const char*& why);

    const char* Describe(LoadResult result);
}
