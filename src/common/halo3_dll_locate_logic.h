#pragma once

// WHERE HALO3.DLL LIES ON DISK, for Project Reclaimer 0.10's recompiled engine (lifted_engine.cpp BuildShadow): the
// client keeps halo3.dll's image with a trap over the start of every function, and the mod's signature searches read a
// copy with halo3.dll's own bytes put back. The first build looked only in the client's mcc-folder.txt, which a fresh
// 0.10.x install does not write: a player's game then ran flat, with no head tracking (2026-11-07, "halo3.dll was not
// found ()"). The other places say where MCC is too: the client's own log names the halo3.dll it lifted, the mod
// normally sits in MCC's own folder, and Steam's library list and the Xbox app's folders hold MCC. Every candidate is
// still checked against the client's build before it is used.
// Pure parsing; unit-tested in tests/core_tests.cpp.

#include <cwctype>
#include <string>
#include <vector>

namespace halo3_dll_locate
{
// A folder named in a text file (mcc-folder.txt): its first line, without a byte-order mark, spaces or trailing
// slashes. Empty when there is none.
inline std::string FolderFromText(std::string text)
{
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
        text.erase(0, 3);
    const size_t end = text.find_first_of("\r\n");
    if (end != std::string::npos)
        text.erase(end);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\\' || text.back() == '/'))
        text.pop_back();
    size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t'))
        ++start;
    return text.substr(start);
}

// The halo3.dll the client's log says it lifted ("[engine] halo3.dll: <path>, the build lifted (...)"): the last such
// line's path, or empty.
inline std::string LiftedDllFromClientLog(const std::string& log)
{
    static constexpr char kKey[] = "[engine] halo3.dll: ";
    const size_t at = log.rfind(kKey);
    if (at == std::string::npos)
        return {};
    const size_t start = at + sizeof(kKey) - 1;
    size_t end = log.find_first_of("\r\n", start);
    if (end == std::string::npos)
        end = log.size();
    std::string line = log.substr(start, end - start);
    const size_t build = line.find(", the build");
    if (build != std::string::npos)
        line.erase(build);
    while (!line.empty() && line.back() == ' ')
        line.pop_back();
    return line;
}

// Every "path" value in Steam's libraryfolders.vdf, its doubled backslashes made single.
inline std::vector<std::string> SteamLibraryPaths(const std::string& vdf)
{
    std::vector<std::string> paths;
    static constexpr char kKey[] = "\"path\"";
    for (size_t at = vdf.find(kKey); at != std::string::npos; at = vdf.find(kKey, at + 1))
    {
        const size_t open = vdf.find('"', at + sizeof(kKey) - 1);
        if (open == std::string::npos)
            break;
        std::string value;
        size_t i = open + 1;
        for (; i < vdf.size() && vdf[i] != '"' && vdf[i] != '\n'; ++i)
        {
            if (vdf[i] == '\\' && i + 1 < vdf.size() && vdf[i + 1] == '\\')
                ++i;
            value += vdf[i];
        }
        if (i < vdf.size() && vdf[i] == '"' && !value.empty())
            paths.push_back(value);
    }
    return paths;
}

// The Xbox app's library folders a drive may hold (XboxGames, or XBOX as on this project's own Store install).
inline constexpr const wchar_t* kXboxLibraryFolders[] = {L"XboxGames", L"XBOX"};
// At most this many entries of one such folder are looked at; nothing below them is searched.
inline constexpr unsigned kXboxLibraryEntries = 512;

// Whether an entry of an Xbox library folder may be MCC's: the app names it "Halo- The Master Chief Collection"
// (docs/MCC-EDITIONS-EVIDENCE.md), its package "Microsoft.Chelan_...", as the launcher's installer looks for.
inline bool MayBeMccFolder(const std::wstring& name)
{
    std::wstring lower = name;
    for (auto& c : lower)
        c = static_cast<wchar_t>(std::towlower(c));
    return lower.find(L"halo") != std::wstring::npos || lower.find(L"chelan") != std::wstring::npos;
}
} // namespace halo3_dll_locate
