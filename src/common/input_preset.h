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

// A preset's optional description, one line in its file, for saying who the scheme is for.
inline constexpr size_t kDescriptionMost = 240;
std::string DescriptionLine(const std::string& description); // what goes in the file; empty for no description
std::string ParseDescription(const char* text);

int BuiltInCount() noexcept;
const char* BuiltInName(int index) noexcept;
const char* BuiltInDescription(int index) noexcept;
void BuiltIn(int index, Config& out); // `out` starts from Config{}

// Letters, digits, spaces, '-' and '_' only, so a name is always a safe file name. Empty when nothing is left, and
// when it would be a built-in's name.
std::string SafeName(const char* name);
std::vector<std::string> ListSaved();
bool Find(const std::string& name, Config& out); // a built-in or a saved file
std::string DescriptionOf(const std::string& name); // a built-in or a saved file; empty when it has none
bool Save(const std::string& name, const Config& scheme, const std::string& description);
bool Delete(const std::string& name);
}
