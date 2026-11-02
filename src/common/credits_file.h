#pragma once
#include <string>
#include <string_view>
#include <vector>

// CREDITS.txt, which the launcher lists: a "name:" line starts a person and the "what:" line after it says what they
// contributed; a "section:" line heads the people after it. Keys this version does not know are skipped, so a file
// with later additions (links, say) still reads.
struct CreditEntry { std::wstring section, name, what; };

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
        else if (key == L"name" && !value.empty() && credits.size() < kMostPeople) { credits.push_back({std::move(section), std::wstring(value), {}}); section.clear(); }
        else if (key == L"what" && !credits.empty()) credits.back().what = value;
    }
    return credits;
}
