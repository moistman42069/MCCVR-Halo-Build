#pragma once
#include "config.h"
#include <string>
#include <vector>

// Input presets: named sets of the settings that make up a control scheme, and nothing else (no aim tuning, no body
// fit). The built-in ones are defined here; a player's own are one small text file each in "Input Presets" beside
// halomccvr.cfg, so they can be copied to someone else. A key a file does not name takes the struct's default, so a
// preset saved by an older build still means the same thing.
namespace input_preset
{
inline constexpr const char* kDefaultName = "Default";

// Copies the control scheme only; every other setting of `to` is left alone.
void CopyScheme(Config& to, const Config& from) noexcept;
bool SameScheme(const Config& a, const Config& b) noexcept;

std::string Serialize(const Config& scheme);
void Parse(const char* text, Config& out); // `out` starts from Config{}

int BuiltInCount() noexcept;
const char* BuiltInName(int index) noexcept;
void BuiltIn(int index, Config& out); // `out` starts from Config{}

// Letters, digits, spaces, '-' and '_' only, so a name is always a safe file name. Empty when nothing is left, and
// when it would be a built-in's name.
std::string SafeName(const char* name);
std::vector<std::string> ListSaved();
bool Find(const std::string& name, Config& out); // a built-in or a saved file
bool Save(const std::string& name, const Config& scheme);
bool Delete(const std::string& name);
}
