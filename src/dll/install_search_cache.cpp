// THE SEARCHES OF A TITLE'S INSTALL, SAVED FOR THE NEXT LAUNCH: the DLL's side (install_search_cache.h).
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>

#include "install_search_cache.h"

namespace install_search_cache
{
namespace
{
wchar_t g_path[MAX_PATH] = L"";
// The largest file this mod writes, and some: a larger one is not this mod's and is not read.
constexpr size_t kMaxFileBytes = 64 + kMaxBuilds * (128 + static_cast<size_t>(kMaxEntries) * 20) + 64;

double MsSince(const LARGE_INTEGER& began)
{
    LARGE_INTEGER now{}, frequency{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart > 0
        ? static_cast<double>(now.QuadPart - began.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart)
        : 0.0;
}

bool Readable(DWORD protect)
{
    if (protect & (PAGE_GUARD | PAGE_NOACCESS))
        return false;
    const DWORD kind = protect & 0xFFu;
    return kind == PAGE_READONLY || kind == PAGE_READWRITE || kind == PAGE_WRITECOPY || kind == PAGE_EXECUTE_READ ||
           kind == PAGE_EXECUTE_READWRITE || kind == PAGE_EXECUTE_WRITECOPY;
}

// [begin, end) committed and readable throughout.
bool RangeReadable(uintptr_t begin, uintptr_t end)
{
    for (uintptr_t at = begin; at < end;)
    {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<const void*>(at), &region, sizeof(region)) != sizeof(region) ||
            region.State != MEM_COMMIT || !Readable(region.Protect))
            return false;
        const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (regionEnd <= at)
            return false;
        at = regionEnd;
    }
    return true;
}

// Reads that cannot fault the worker: a page that went away between the check and the read ends the identity, not
// the process. No C++ object with a destructor lives in these functions.
bool CopyGuarded(void* out, uintptr_t from, size_t size)
{
    __try
    {
        std::memcpy(out, reinterpret_cast<const void*>(from), size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool CopyUnhookedGuarded(void* out, uintptr_t from, size_t size)
{
    __try
    {
        (void)sig::CopyUnhooked(from, out, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

struct Section
{
    uint32_t va = 0;
    uint32_t size = 0;
};

// Whether any base relocation lands in one of `sections`: then the code's bytes depend on where the DLL was
// loaded, and the identity takes the base in too.
bool RelocationsInCode(uintptr_t base, size_t imageSize, const IMAGE_DATA_DIRECTORY& directory,
                       const std::vector<Section>& sections, bool& inCode)
{
    inCode = false;
    if (!directory.VirtualAddress || !directory.Size)
        return true;
    if (directory.VirtualAddress >= imageSize || directory.Size > imageSize - directory.VirtualAddress ||
        !RangeReadable(base + directory.VirtualAddress, base + directory.VirtualAddress + directory.Size))
        return false;
    uint32_t at = 0;
    while (at + sizeof(IMAGE_BASE_RELOCATION) <= directory.Size)
    {
        IMAGE_BASE_RELOCATION block{};
        if (!CopyGuarded(&block, base + directory.VirtualAddress + at, sizeof(block)) ||
            block.SizeOfBlock < sizeof(block) || block.SizeOfBlock > directory.Size - at)
            return false;
        const uint32_t entries = (block.SizeOfBlock - sizeof(block)) / 2;
        for (uint32_t i = 0; i < entries; ++i)
        {
            uint16_t entry = 0;
            if (!CopyGuarded(&entry, base + directory.VirtualAddress + at + sizeof(block) + i * 2u, 2))
                return false;
            if ((entry >> 12) == IMAGE_REL_BASED_ABSOLUTE)
                continue;
            const uint32_t rva = block.VirtualAddress + (entry & 0x0FFFu);
            for (const Section& s : sections)
                if (rva >= s.va && rva - s.va < s.size)
                {
                    inCode = true;
                    return true;
                }
        }
        at += block.SizeOfBlock;
    }
    return true;
}

bool ReadFileWhole(std::vector<uint8_t>& bytes, bool& absent)
{
    absent = false;
    bytes.clear();
    const HANDLE file = CreateFileW(g_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        absent = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
        return false;
    }
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) != FALSE;
    if (ok && size.QuadPart > static_cast<LONGLONG>(kMaxFileBytes))
    {
        // Larger than any this mod writes: read as nothing whole, so it is not used and a save replaces it.
        CloseHandle(file);
        return true;
    }
    if (ok)
    {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = bytes.empty() ||
             (ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size());
    }
    CloseHandle(file);
    if (!ok)
        bytes.clear();
    return ok;
}

// NEVER IN PLACE: the bytes go to a temporary file beside the cache, which then takes its place in one step. Until
// that step the old file is whole, and a write that fails leaves it so.
bool WriteFileWhole(const std::vector<uint8_t>& bytes)
{
    wchar_t temporary[MAX_PATH + 8];
    if (_snwprintf_s(temporary, _TRUNCATE, L"%ls.tmp", g_path) < 0)
        return false;
    const HANDLE file =
        CreateFileW(temporary, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool whole = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                       written == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (whole && MoveFileExW(temporary, g_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    DeleteFileW(temporary);
    return false;
}
} // namespace

void SetFile(const wchar_t* path)
{
    if (!path || _snwprintf_s(g_path, _TRUNCATE, L"%ls", path) < 0)
        g_path[0] = 0;
}

void SetFolder(const wchar_t* folder)
{
    wchar_t path[MAX_PATH];
    if (!folder || _snwprintf_s(path, _TRUNCATE, L"%lsHaloMCCVR-install-searches.bin", folder) < 0)
        path[0] = 0;
    SetFile(path);
}

bool Identify(const char* title, uintptr_t base, size_t size, Identity& out, const char*& why)
{
    out = Identity{};
    why = nullptr;
    if (!base || size < 0x1000 || size > 0xFFFFFFFFull || !RangeReadable(base, base + 0x1000))
    {
        why = "its headers cannot be read";
        return false;
    }
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!CopyGuarded(&dos, base, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
        static_cast<size_t>(dos.e_lfanew) + sizeof(nt) > 0x1000 ||
        !CopyGuarded(&nt, base + static_cast<uintptr_t>(dos.e_lfanew), sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.OptionalHeader.SizeOfImage != size)
    {
        why = "it is not a 64-bit PE image of its own size";
        return false;
    }
    if (title)
        std::memcpy(out.title, title, strnlen(title, sizeof(out.title) - 1));
    out.timeDateStamp = nt.FileHeader.TimeDateStamp;
    out.sizeOfImage = nt.OptionalHeader.SizeOfImage;
    out.checkSum = nt.OptionalHeader.CheckSum;
    out.rules = kSearchRules;

    // The linker's own name for this link: the CodeView record's GUID and age, where the debug directory is mapped.
    const IMAGE_DATA_DIRECTORY debug = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (debug.VirtualAddress && debug.Size && debug.VirtualAddress < size && debug.Size <= size - debug.VirtualAddress &&
        RangeReadable(base + debug.VirtualAddress, base + debug.VirtualAddress + debug.Size))
        for (uint32_t at = 0; at + sizeof(IMAGE_DEBUG_DIRECTORY) <= debug.Size; at += sizeof(IMAGE_DEBUG_DIRECTORY))
        {
            IMAGE_DEBUG_DIRECTORY entry{};
            if (!CopyGuarded(&entry, base + debug.VirtualAddress + at, sizeof(entry)))
                break;
            if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW || !entry.AddressOfRawData || entry.SizeOfData < 24 ||
                entry.AddressOfRawData >= size || 24 > size - entry.AddressOfRawData ||
                !RangeReadable(base + entry.AddressOfRawData, base + entry.AddressOfRawData + 24))
                continue;
            uint8_t record[24]{};
            if (CopyGuarded(record, base + entry.AddressOfRawData, sizeof(record)) && !std::memcmp(record, "RSDS", 4))
            {
                std::memcpy(out.pdbGuid, record + 4, 16);
                std::memcpy(&out.pdbAge, record + 20, 4);
            }
            break;
        }

    // The executable sections.
    const uintptr_t sectionTable = base + static_cast<uintptr_t>(dos.e_lfanew) +
        offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
    const uint32_t sectionCount = nt.FileHeader.NumberOfSections;
    if (!sectionCount || sectionCount > 96 ||
        sectionTable + static_cast<uintptr_t>(sectionCount) * sizeof(IMAGE_SECTION_HEADER) > base + 0x1000)
    {
        why = "its section table cannot be read";
        return false;
    }
    std::vector<Section> code;
    for (uint32_t i = 0; i < sectionCount; ++i)
    {
        IMAGE_SECTION_HEADER header{};
        if (!CopyGuarded(&header, sectionTable + i * sizeof(IMAGE_SECTION_HEADER), sizeof(header)))
        {
            why = "its section table cannot be read";
            return false;
        }
        if (!(header.Characteristics & IMAGE_SCN_MEM_EXECUTE))
            continue;
        const uint32_t span = header.Misc.VirtualSize ? header.Misc.VirtualSize : header.SizeOfRawData;
        if (!span || header.VirtualAddress >= size || span > size - header.VirtualAddress)
        {
            why = "an executable section lies outside the image";
            return false;
        }
        code.push_back(Section{header.VirtualAddress, span});
    }
    if (code.empty())
    {
        why = "it has no executable section";
        return false;
    }

    // The code as it stands in memory, with this mod's enabled hooks undone (sig::CopyUnhooked), so the identity
    // is the same before and after this mod hooks it.
    CodeHash hash;
    std::vector<uint8_t> chunk(64 * 1024);
    for (const Section& s : code)
    {
        if (!RangeReadable(base + s.va, base + s.va + s.size))
        {
            why = "its code cannot be read whole";
            return false;
        }
        for (uint32_t at = 0; at < s.size;)
        {
            const uint32_t n = s.size - at < chunk.size() ? s.size - at : static_cast<uint32_t>(chunk.size());
            if (!CopyUnhookedGuarded(chunk.data(), base + s.va + at, n))
            {
                why = "its code cannot be read whole";
                return false;
            }
            hash.Add(chunk.data(), n);
            at += n;
        }
    }
    bool relocatedCode = false;
    if (!RelocationsInCode(base, size, nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC], code,
                           relocatedCode))
    {
        why = "its relocations cannot be read";
        return false;
    }
    if (relocatedCode)
    {
        // Code whose bytes the loader adjusts to the base: the same build at another base is other bytes.
        const uint64_t at = base;
        hash.Add(&at, sizeof(at));
    }
    out.codeHash = hash.Final();
    out.codeBytes = hash.bytes;
    return true;
}

LoadReport Load(const Identity& identity, sig::ImageMemo& memo)
{
    LoadReport report;
    if (!g_path[0])
        return report;
    LARGE_INTEGER began{};
    QueryPerformanceCounter(&began);
    std::vector<uint8_t> bytes;
    bool absent = false;
    if (!ReadFileWhole(bytes, absent))
    {
        report.result = absent ? LoadResult::NoFile : LoadResult::Unreadable;
        report.ms = MsSince(began);
        return report;
    }
    std::vector<Build> builds;
    if (!Parse(bytes.data(), bytes.size(), builds))
    {
        report.result = LoadResult::NotWhole;
        report.ms = MsSince(began);
        return report;
    }
    report.builds = static_cast<uint32_t>(builds.size());
    const Build* const build = Find(builds, identity);
    if (!build || identity.sizeOfImage != memo.size)
    {
        report.result = LoadResult::NoEntry;
        report.ms = MsSince(began);
        return report;
    }
    for (const Entry& e : build->entries)
    {
        bool held = false;
        for (uint32_t i = 0; i < memo.count && !held; ++i)
            held = memo.entries[i].pattern == e.pattern && memo.entries[i].start == e.start &&
                   memo.entries[i].length == e.length;
        if (held || memo.count >= sig::ImageMemo::kCapacity)
            continue;
        memo.entries[memo.count++] = sig::ImageMemo::Entry{e.pattern, e.start, e.length, e.match, 1};
        ++report.taken;
    }
    report.result = LoadResult::Loaded;
    report.ms = MsSince(began);
    return report;
}

bool Save(const Identity& identity, sig::ImageMemo& memo, const char*& why, double& ms)
{
    why = nullptr;
    ms = 0.0;
    if (!g_path[0])
    {
        why = "no folder for it";
        return false;
    }
    LARGE_INTEGER began{};
    QueryPerformanceCounter(&began);
    std::vector<uint8_t> bytes;
    bool absent = false;
    std::vector<Build> builds;
    if (ReadFileWhole(bytes, absent))
    {
        // A file that is not whole is replaced; every other build of a whole one is kept.
        if (!Parse(bytes.data(), bytes.size(), builds))
            builds.clear();
    }
    else if (!absent)
    {
        why = "the file is there and could not be read, so it is left as it is";
        ms = MsSince(began);
        return false;
    }
    Build build;
    build.identity = identity;
    for (uint32_t i = 0; i < memo.count && build.entries.size() < kMaxEntries; ++i)
    {
        const sig::ImageMemo::Entry& m = memo.entries[i];
        const Entry e{m.pattern, m.start, m.length, m.match};
        if (EntryFits(e, identity.sizeOfImage))
            build.entries.push_back(e);
    }
    Put(builds, build);
    const bool written = WriteFileWhole(Serialize(builds));
    ms = MsSince(began);
    if (!written)
    {
        why = "it could not be written";
        return false;
    }
    memo.changes = 0;
    return true;
}

bool Forget(const Identity& identity, const char*& why)
{
    why = nullptr;
    if (!g_path[0])
        return true;
    std::vector<uint8_t> bytes;
    bool absent = false;
    if (!ReadFileWhole(bytes, absent))
    {
        if (absent)
            return true;
        why = "the file is there and could not be read, so it is left as it is";
        return false;
    }
    std::vector<Build> builds;
    // A file that is not whole gives nothing to the next launch either.
    if (!Parse(bytes.data(), bytes.size(), builds) || !Drop(builds, identity))
        return true;
    if (!WriteFileWhole(Serialize(builds)))
    {
        why = "it could not be written";
        return false;
    }
    return true;
}

const char* Describe(LoadResult result)
{
    switch (result)
    {
    case LoadResult::NoFolder: return "no folder for the saved searches";
    case LoadResult::NoFile: return "no saved searches yet (the first launch, or the file was deleted)";
    case LoadResult::Unreadable: return "the saved searches' file could not be read, so it is left as it is";
    case LoadResult::NotWhole: return "the saved searches' file is not whole and is not used (the next save replaces it)";
    case LoadResult::NoEntry: return "no saved searches for this build of the DLL";
    case LoadResult::Loaded: return "this build's saved searches";
    }
    return "?";
}
}
