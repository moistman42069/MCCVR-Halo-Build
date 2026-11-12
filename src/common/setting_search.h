#pragma once
#include <cctype>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

// Searching the settings menu the way Unreal's Open Asset dialog searches assets (FTextFilterExpressionEvaluator in
// its basic form): words separated by spaces must all match, in any order, each as a case-insensitive part of any of
// the fields searched (a setting's own name, its section and its page). The same operators are understood:
//   "two words"   a phrase, matched as one
//   -word !word   must not match
//   a OR b, a | b either side
//   +word         the whole field, exactly
//   word...       a field that starts with it;  ...word  a field that ends with it
namespace setting_search
{
enum class Compare { Partial, Exact, StartsWith, EndsWith };
struct Term { std::string text; Compare compare = Compare::Partial; bool exclude = false; };
// Any one group matching is a match; within a group, every term must hold.
struct Query { std::vector<std::vector<Term>> groups; bool Empty() const { return groups.empty(); } };

inline std::string Lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

inline Query Parse(std::string_view text)
{
    Query query;
    std::vector<Term> group;
    const auto close = [&] { if (!group.empty()) { query.groups.push_back(std::move(group)); group.clear(); } };
    size_t at = 0;
    while (at < text.size())
    {
        if (std::isspace(static_cast<unsigned char>(text[at]))) { ++at; continue; }
        if (text[at] == '|') { close(); ++at; continue; }
        Term term;
        if (text[at] == '-' || text[at] == '!') { term.exclude = true; ++at; }
        if (at < text.size() && text[at] == '+') { term.compare = Compare::Exact; ++at; }
        std::string word;
        if (at < text.size() && text[at] == '"')
        {
            const size_t end = text.find('"', at + 1);
            word = std::string(text.substr(at + 1, end == std::string_view::npos ? std::string_view::npos : end - at - 1));
            at = end == std::string_view::npos ? text.size() : end + 1;
        }
        else
        {
            const size_t start = at;
            while (at < text.size() && !std::isspace(static_cast<unsigned char>(text[at])) && text[at] != '|') ++at;
            word = std::string(text.substr(start, at - start));
            if (!term.exclude && term.compare == Compare::Partial)
            {
                if (word == "OR") { close(); continue; }
                if (word == "AND" || word == "&&") continue;
            }
            if (term.compare == Compare::Partial && word.size() > 3)
            {
                if (word.rfind("...", 0) == 0) { term.compare = Compare::EndsWith; word.erase(0, 3); }
                else if (word.compare(word.size() - 3, 3, "...") == 0) { term.compare = Compare::StartsWith; word.resize(word.size() - 3); }
            }
        }
        if (word.empty()) continue;
        term.text = Lower(word);
        group.push_back(std::move(term));
    }
    close();
    return query;
}

inline bool TermHolds(const Term& term, const std::string& field)
{
    const std::string& t = term.text;
    switch (term.compare)
    {
    case Compare::Exact: return field == t;
    case Compare::StartsWith: return field.rfind(t, 0) == 0;
    case Compare::EndsWith: return field.size() >= t.size() && field.compare(field.size() - t.size(), t.size(), t) == 0;
    case Compare::Partial: break;
    }
    return field.find(t) != std::string::npos;
}

inline bool Matches(const Query& query, std::initializer_list<std::string_view> fields)
{
    if (query.Empty()) return false;
    std::vector<std::string> lowered;
    for (const auto field : fields) lowered.push_back(Lower(field));
    for (const auto& group : query.groups)
    {
        bool all = true;
        for (const auto& term : group)
        {
            bool any = false;
            for (const auto& field : lowered) any = any || TermHolds(term, field);
            if (any == term.exclude) { all = false; break; }
        }
        if (all) return true;
    }
    return false;
}
} // namespace setting_search
