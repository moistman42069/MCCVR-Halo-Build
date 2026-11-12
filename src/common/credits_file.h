#pragma once
#include <string>
#include <string_view>
#include <vector>

// CREDITS.txt, which the launcher lists: a "name:" line starts a person, the "what:" line after it says what they
// contributed and each "link: Label https://address" line is a badge beside their name; a "section:" line heads the
// people after it and an "intro:" line is a paragraph with no name, an entry whose name is empty. Keys this version
// does not know are skipped, so a file with later additions still reads.
struct CreditLink { std::wstring label, url; };
struct CreditEntry { std::wstring section, name, what; std::vector<CreditLink> links; };

// The launcher hands these addresses to the shell, so only a plain https address counts.
inline bool IsCreditUrl(std::wstring_view url) {
    constexpr std::wstring_view kScheme = L"https://";
    if (url.size() <= kScheme.size() || url.size() > 2048 || url.substr(0, kScheme.size()) != kScheme) return false;
    for (const wchar_t c : url) if (c <= L' ' || c == L'"' || c == L'<' || c == L'>' || c == L'\\' || c == 0x7F) return false;
    return true;
}

inline std::vector<CreditEntry> ParseCredits(std::wstring_view text) {
    constexpr size_t kMostPeople = 500;
    const auto trim = [](std::wstring_view value) {
        const auto first = value.find_first_not_of(L" \t\r\xFEFF");
        if (first == std::wstring_view::npos) return std::wstring_view{};
        return value.substr(first, value.find_last_not_of(L" \t\r") - first + 1);
    };
    std::vector<CreditEntry> credits;
    std::wstring section;
    while (!text.empty()) {
        const auto end = text.find(L'\n');
        const auto line = trim(text.substr(0, end));
        text = end == std::wstring_view::npos ? std::wstring_view{} : text.substr(end + 1);
        const auto colon = line.find(L':');
        if (line.empty() || line.front() == L'#' || colon == std::wstring_view::npos) continue;
        const auto key = line.substr(0, colon), value = trim(line.substr(colon + 1));
        if (key == L"section") section = value;
        else if (key == L"intro" && !value.empty() && credits.size() < kMostPeople) credits.push_back({{}, {}, std::wstring(value)});
        else if (key == L"name" && !value.empty() && credits.size() < kMostPeople) { credits.push_back({std::move(section), std::wstring(value), {}}); section.clear(); }
        else if (key == L"what" && !credits.empty()) credits.back().what = value;
        else if (key == L"link" && !credits.empty()) {
            const auto space = value.find_last_of(L" \t");
            if (space == std::wstring_view::npos) continue;
            const auto label = trim(value.substr(0, space)), url = value.substr(space + 1);
            if (!label.empty() && IsCreditUrl(url)) credits.back().links.push_back({std::wstring(label), std::wstring(url)});
        }
    }
    return credits;
}
