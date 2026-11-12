#pragma once
// DEV BUILD ONLY (automated tests): the command language of the synthetic gamepad in src/dll/dev_pad.cpp.
//
// The harness (tools/Send-MccPad.ps1) drops a script into the mod folder; the DLL runs it and the pad it
// describes is merged into MCC's own XInput poll as if a physical controller had pressed it. That puts
// menus, Forge and vehicles within reach of an automated run with no controller hardware and no VR runtime
// input injection. Pure parsing only: no Windows headers, so it is testable.
//
// One command per line; '#' starts a comment; words are case-insensitive. The pad keeps its state until
// a command changes it:
//   hold <buttons>              press and keep pressed
//   release <buttons>|all       let go
//   press <buttons> <ms>        hold for ms, then let go
//   stick <left|right> <x> <y>  set a stick, each axis -1..1 (0 0 = centred)
//   trigger <left|right> <v>    set a trigger, 0..1
//   wait <ms>                   pause the script
//   clear                       centre the sticks, zero the triggers, let go of every button
//   seq <n>                     report n in the status file when the script reaches it
//   set <setting> <int>         change a live setting exactly as F1 does (the DLL names which ones), so a
//                               automated test can flip the A/B switches mid-session
// Buttons: a b x y lb rb up down left right start back ls rs, several joined by '+' (e.g. "lb+rb").

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace dev_pad
{
// XINPUT_GAMEPAD button bits, spelled out so this header needs no Windows headers.
constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008;
constexpr uint16_t kStart = 0x0010, kBack = 0x0020, kLeftThumb = 0x0040, kRightThumb = 0x0080;
constexpr uint16_t kLeftShoulder = 0x0100, kRightShoulder = 0x0200;
constexpr uint16_t kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;
constexpr uint32_t kMaxMs = 60000;  // one step never holds the pad longer than a minute

enum class Op : uint8_t { Hold, Release, ReleaseAll, Press, Stick, Trigger, Wait, Clear, Seq, Set };

struct Step
{
    Op op = Op::Clear;
    uint16_t buttons = 0;  // Hold / Release / Press
    int side = 0;          // Stick / Trigger: 0 left, 1 right
    float x = 0.0f;        // Stick x, or the Trigger value
    float y = 0.0f;        // Stick y
    uint32_t ms = 0;       // Press / Wait
    uint32_t seq = 0;      // Seq
    std::string name;      // Set: the setting
    int value = 0;         // Set: its new value
};

inline std::string Lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline bool ParseButton(const std::string& name, uint16_t& bit)
{
    struct Entry { const char* name; uint16_t bit; };
    static const Entry kButtons[] = {
        {"a", kA}, {"b", kB}, {"x", kX}, {"y", kY}, {"lb", kLeftShoulder}, {"rb", kRightShoulder},
        {"up", kUp}, {"down", kDown}, {"left", kLeft}, {"right", kRight}, {"start", kStart},
        {"back", kBack}, {"ls", kLeftThumb}, {"rs", kRightThumb}};
    for (const Entry& e : kButtons)
        if (name == e.name) { bit = e.bit; return true; }
    return false;
}

// "a", "lb+rb": every name must be known and the set must not be empty.
inline bool ParseButtons(const std::string& token, uint16_t& bits)
{
    bits = 0;
    size_t start = 0;
    while (start <= token.size())
    {
        const size_t plus = token.find('+', start);
        const std::string name = token.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
        uint16_t bit = 0;
        if (name.empty() || !ParseButton(name, bit)) return false;
        bits = static_cast<uint16_t>(bits | bit);
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    return bits != 0;
}

inline bool ParseFloat(const std::string& s, float lo, float hi, float& out)
{
    if (s.empty()) return false;
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    if (!end || *end != '\0' || !(v >= lo && v <= hi)) return false;
    out = v;
    return true;
}

inline bool ParseUint(const std::string& s, uint32_t hi, uint32_t& out)
{
    if (s.empty() || s.size() > 10) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    const unsigned long long v = std::strtoull(s.c_str(), nullptr, 10);
    if (v > hi) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

inline bool ParseSide(const std::string& s, int& side)
{
    if (s == "left" || s == "l") { side = 0; return true; }
    if (s == "right" || s == "r") { side = 1; return true; }
    return false;
}

// The whole script, or nothing: a script with one bad line is refused, so a typo cannot leave a button
// held. On failure `error` names the line.
inline bool Parse(const std::string& text, std::vector<Step>& steps, std::string& error)
{
    steps.clear();
    error.clear();
    size_t pos = 0;
    int lineNo = 0;
    while (pos < text.size())
    {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        ++lineNo;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        std::vector<std::string> w;
        {
            size_t i = 0;
            while (i < line.size())
            {
                while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
                size_t j = i;
                while (j < line.size() && !std::isspace(static_cast<unsigned char>(line[j]))) ++j;
                if (j > i) w.push_back(Lower(line.substr(i, j - i)));
                i = j;
            }
        }
        if (w.empty()) continue;
        Step s;
        bool ok = false;
        const std::string& cmd = w[0];
        if (cmd == "hold" && w.size() == 2) { s.op = Op::Hold; ok = ParseButtons(w[1], s.buttons); }
        else if (cmd == "release" && w.size() == 2)
        {
            if (w[1] == "all") { s.op = Op::ReleaseAll; ok = true; }
            else { s.op = Op::Release; ok = ParseButtons(w[1], s.buttons); }
        }
        else if (cmd == "press" && w.size() == 3)
        {
            s.op = Op::Press;
            ok = ParseButtons(w[1], s.buttons) && ParseUint(w[2], kMaxMs, s.ms);
        }
        else if (cmd == "stick" && w.size() == 4)
        {
            s.op = Op::Stick;
            ok = ParseSide(w[1], s.side) && ParseFloat(w[2], -1.0f, 1.0f, s.x) && ParseFloat(w[3], -1.0f, 1.0f, s.y);
        }
        else if (cmd == "trigger" && w.size() == 3)
        {
            s.op = Op::Trigger;
            ok = ParseSide(w[1], s.side) && ParseFloat(w[2], 0.0f, 1.0f, s.x);
        }
        else if (cmd == "wait" && w.size() == 2) { s.op = Op::Wait; ok = ParseUint(w[1], kMaxMs, s.ms); }
        else if (cmd == "clear" && w.size() == 1) { s.op = Op::Clear; ok = true; }
        else if (cmd == "seq" && w.size() == 2) { s.op = Op::Seq; ok = ParseUint(w[1], 0xFFFFFFFFu, s.seq); }
        else if (cmd == "set" && w.size() == 3)
        {
            s.op = Op::Set;
            s.name = w[1];
            // A setting's name: lower-case letters, underscores, and digits after the first character.
            ok = !s.name.empty() && !(s.name[0] >= '0' && s.name[0] <= '9');
            for (char c : s.name)
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) ok = false;
            uint32_t v = 0;
            ok = ok && ParseUint(w[2], 1000000, v);
            s.value = static_cast<int>(v);
        }
        if (!ok)
        {
            error = "line " + std::to_string(lineNo) + ": cannot read \"" + line + "\"";
            steps.clear();
            return false;
        }
        steps.push_back(s);
    }
    return true;
}

// -1..1 to a raw XInput stick value.
inline int16_t StickRaw(float v)
{
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return static_cast<int16_t>(v * 32767.0f);
}

inline uint8_t TriggerRaw(float v)
{
    if (v > 1.0f) v = 1.0f;
    if (v < 0.0f) v = 0.0f;
    return static_cast<uint8_t>(v * 255.0f + 0.5f);
}
}  // namespace dev_pad
