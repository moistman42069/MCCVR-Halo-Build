#pragma once
#include "installer_policy.h"
#include <charconv>
#include <map>
#include <stdexcept>

namespace mcc_installer {
// Small strict JSON reader for the public GitHub release document. It never
// evaluates JSON as script. Size/depth bounds also apply to skipped fields.
struct ReleaseJson {
    enum class Kind { Null, String, Number, Boolean, Object, Array } kind = Kind::Null;
    std::string text;
    std::map<std::string, ReleaseJson> object;
    std::vector<ReleaseJson> array;
    const ReleaseJson& At(const char* key) const { static const ReleaseJson empty; const auto it = object.find(key); return it == object.end() ? empty : it->second; }
};
class ReleaseJsonReader {
    std::string_view input; size_t at = 0;
    [[noreturn]] void Fail() const { throw std::runtime_error("Invalid GitHub release response."); }
    void Space() { while (at < input.size() && (input[at] == ' ' || input[at] == '\r' || input[at] == '\n' || input[at] == '\t')) ++at; }
    bool Take(char c) { Space(); if (at < input.size() && input[at] == c) { ++at; return true; } return false; }
    unsigned Hex4() {
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            if (at >= input.size()) Fail();
            const char c = input[at++]; unsigned digit;
            if (c >= '0' && c <= '9') digit = c - '0'; else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10; else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10; else { Fail(); }
            value = value * 16 + digit;
        }
        return value;
    }
    std::string String() {
        if (!Take('"')) Fail();
        std::string value;
        while (at < input.size()) {
            const unsigned char c = input[at++];
            if (c == '"') return value;
            if (c < 32) Fail();
            if (c != '\\') { value += static_cast<char>(c); continue; }
            if (at == input.size()) Fail();
            switch (input[at++]) {
            case '"': value += '"'; break; case '\\': value += '\\'; break; case '/': value += '/'; break;
            case 'b': value += '\b'; break; case 'f': value += '\f'; break; case 'n': value += '\n'; break; case 'r': value += '\r'; break; case 't': value += '\t'; break;
            case 'u': {
                unsigned code = Hex4();
                if (code >= 0xD800 && code <= 0xDBFF) {
                    if (at + 2 > input.size() || input[at] != '\\' || input[at + 1] != 'u') Fail();
                    at += 2; const unsigned low = Hex4(); if (low < 0xDC00 || low > 0xDFFF) Fail();
                    code = 0x10000 + ((code - 0xD800) << 10) + low - 0xDC00;
                } else if (code >= 0xDC00 && code <= 0xDFFF) Fail();
                if (code < 0x80) value += static_cast<char>(code);
                else if (code < 0x800) { value += static_cast<char>(0xC0 | (code >> 6)); value += static_cast<char>(0x80 | (code & 63)); }
                else if (code < 0x10000) { value += static_cast<char>(0xE0 | (code >> 12)); value += static_cast<char>(0x80 | ((code >> 6) & 63)); value += static_cast<char>(0x80 | (code & 63)); }
                else { value += static_cast<char>(0xF0 | (code >> 18)); value += static_cast<char>(0x80 | ((code >> 12) & 63)); value += static_cast<char>(0x80 | ((code >> 6) & 63)); value += static_cast<char>(0x80 | (code & 63)); }
                break;
            }
            default: Fail();
            }
        }
        Fail();
    }
    ReleaseJson Value(unsigned depth) {
        if (depth > 32) Fail(); Space(); if (at >= input.size()) Fail();
        ReleaseJson value;
        if (input[at] == '"') { value.kind = ReleaseJson::Kind::String; value.text = String(); return value; }
        if (Take('{')) {
            value.kind = ReleaseJson::Kind::Object;
            if (Take('}')) return value;
            do { auto key = String(); if (!Take(':')) Fail(); if (!value.object.emplace(std::move(key), Value(depth + 1)).second) Fail(); } while (Take(','));
            if (!Take('}')) Fail(); return value;
        }
        if (Take('[')) {
            value.kind = ReleaseJson::Kind::Array;
            if (Take(']')) return value;
            do { value.array.push_back(Value(depth + 1)); } while (Take(','));
            if (!Take(']')) Fail(); return value;
        }
        for (const auto literal : {"true", "false", "null"}) if (input.substr(at).starts_with(literal)) {
            at += std::char_traits<char>::length(literal); value.text = literal;
            value.kind = value.text == "null" ? ReleaseJson::Kind::Null : ReleaseJson::Kind::Boolean; return value;
        }
        const size_t begin = at;
        if (input[at] == '-') ++at;
        if (at >= input.size() || !std::isdigit(static_cast<unsigned char>(input[at]))) Fail();
        if (input[at] == '0') ++at; else while (at < input.size() && std::isdigit(static_cast<unsigned char>(input[at]))) ++at;
        if (at < input.size() && input[at] == '.') { ++at; const auto before = at; while (at < input.size() && std::isdigit(static_cast<unsigned char>(input[at]))) ++at; if (at == before) Fail(); }
        if (at < input.size() && (input[at] == 'e' || input[at] == 'E')) { ++at; if (at < input.size() && (input[at] == '+' || input[at] == '-')) ++at; const auto before = at; while (at < input.size() && std::isdigit(static_cast<unsigned char>(input[at]))) ++at; if (at == before) Fail(); }
        value.kind = ReleaseJson::Kind::Number; value.text = input.substr(begin, at - begin); return value;
    }
public:
    explicit ReleaseJsonReader(std::string_view text) : input(text) {}
    ReleaseJson Read() { if (input.size() > 2 * 1024 * 1024) Fail(); auto result = Value(0); Space(); if (at != input.size()) Fail(); return result; }
};
struct ReleaseUpdate { std::string tag, assetName, url, sha256; uint64_t bytes = 0; };
inline ReleaseUpdate ParseReleaseUpdate(const std::string& text) {
    const auto json = ReleaseJsonReader(text).Read();
    if (json.kind != ReleaseJson::Kind::Object || json.At("draft").kind != ReleaseJson::Kind::Boolean || json.At("draft").text != "false" || json.At("prerelease").kind != ReleaseJson::Kind::Boolean || json.At("prerelease").text != "false" || json.At("tag_name").kind != ReleaseJson::Kind::String)
        throw std::runtime_error("The latest release is unavailable or is not a stable release.");
    ReleaseUpdate result; size_t candidates = 0;
    for (const auto& asset : json.At("assets").array) {
        const auto name = asset.At("name").text;
        std::string lower = name; std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        // Explicit player archive naming. Never select sources, symbols or logs.
        if (!(lower == "halo-mcc-vr.zip" || lower == "halomccvr.zip" || (lower.starts_with("halomccvr-") && lower.ends_with("-build.zip")))) continue;
        ++candidates;
        const std::string prefix = "https://github.com/moistman42069/MCCVR-Halo-Build/releases/download/";
        const auto url = asset.At("browser_download_url").text;
        const auto digest = asset.At("digest").text;
        if (!url.starts_with(prefix) || url.find_first_of("\r\n\t\\?#") != std::string::npos || digest.size() != 71 || !digest.starts_with("sha256:") ||
            !std::all_of(digest.begin() + 7, digest.end(), [](unsigned char c) { return std::isxdigit(c) != 0; }))
            throw std::runtime_error("This release does not provide a trusted download URL and SHA-256 digest. Use the release page for manual installation.");
        uint64_t bytes = 0; const auto& size = asset.At("size").text;
        auto parsed = std::from_chars(size.data(), size.data() + size.size(), bytes);
        if (asset.At("size").kind != ReleaseJson::Kind::Number || parsed.ec != std::errc{} || parsed.ptr != size.data() + size.size() || !bytes || bytes > 512ull * 1024 * 1024)
            throw std::runtime_error("Release asset size is invalid or exceeds the update limit.");
        result = {json.At("tag_name").text, name, url, digest.substr(7), bytes};
        std::transform(result.sha256.begin(), result.sha256.end(), result.sha256.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }
    if (candidates != 1) throw std::runtime_error("A single compatible player ZIP was not found in the latest release. Open the release page for manual installation.");
    return result;
}
} // namespace mcc_installer
