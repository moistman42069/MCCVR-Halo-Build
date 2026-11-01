#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// WHICH OPENXR API LAYERS SIT BETWEEN THE MOD AND THE RUNTIME (vr.cpp, LogOpenXrApiLayers).
//
// An implicit API layer is loaded into every OpenXR program by the loader, unasked: it is registered under
// SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit and its manifest names the DLL. A layer that rewrites the
// frame (frame generation, an upscaler, a compatibility shim) changes what our swapchains and layers go
// through, and a crash inside it is a crash on our call. OFXR Bridge's frame generation has been seen to
// crash the mod in Project Reclaimer a few seconds in. So the log says, once per session, which layers are
// registered, which ones the loader actually loaded, and why any other one stayed out.
//
// The loader does not tell a program which implicit layers it loaded. The witness is the layer's DLL being
// in the process after xrCreateInstance. Pure logic here (manifest fields, the verdict, the known-problem
// table), unit-tested offline; the registry, files and module list are read in vr.cpp.
namespace openxr_layer_report
{
// The string value of `"key": "..."` in a manifest, escapes resolved (\" \\ \/ and \uXXXX for ASCII).
// Empty when the key is absent or its value is not a string. Manifests nest the fields one level down
// ("api_layer": {...}); the keys we read occur once, so the first match is the field.
inline std::string JsonStringField(std::string_view json, std::string_view key)
{
    std::string quoted;
    quoted.reserve(key.size() + 2);
    quoted += '"';
    quoted += key;
    quoted += '"';
    size_t at = 0;
    while ((at = json.find(quoted, at)) != std::string_view::npos)
    {
        size_t i = at + quoted.size();
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n'))
            ++i;
        if (i >= json.size() || json[i] != ':')
        {
            at += quoted.size();
            continue;
        }
        ++i;
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n'))
            ++i;
        if (i >= json.size() || json[i] != '"')
            return {};
        ++i;
        std::string value;
        while (i < json.size() && json[i] != '"')
        {
            char c = json[i++];
            if (c == '\\' && i < json.size())
            {
                const char e = json[i++];
                switch (e)
                {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'u':
                {
                    unsigned code = 0;
                    int digits = 0;
                    for (; digits < 4 && i < json.size(); ++digits, ++i)
                    {
                        const char h = json[i];
                        const unsigned v = (h >= '0' && h <= '9')   ? unsigned(h - '0')
                                           : (h >= 'a' && h <= 'f') ? unsigned(h - 'a' + 10)
                                           : (h >= 'A' && h <= 'F') ? unsigned(h - 'A' + 10)
                                                                    : 16u;
                        if (v == 16u)
                            break;
                        code = code * 16 + v;
                    }
                    c = (digits == 4 && code < 0x80) ? char(code) : '?';
                    break;
                }
                default: c = e; break; // \" \\ \/
                }
            }
            value += c;
        }
        return i < json.size() ? value : std::string{};
    }
    return {};
}

inline bool IsSeparator(char c) { return c == '\\' || c == '/'; }

// The file name at the end of a path.
inline std::string_view Leaf(std::string_view path)
{
    for (size_t i = path.size(); i > 0; --i)
        if (IsSeparator(path[i - 1]))
            return path.substr(i);
    return path;
}

// Where the loader looks for a manifest's library: an absolute path as written, a relative one against the
// manifest's folder, a bare file name through the DLL search order (returned as is: match it by leaf).
inline std::string ResolveLibraryPath(std::string_view manifestPath, std::string_view libraryPath)
{
    const bool absolute = (libraryPath.size() >= 2 && libraryPath[1] == ':') ||
                          (libraryPath.size() >= 2 && IsSeparator(libraryPath[0]) && IsSeparator(libraryPath[1]));
    bool bare = true;
    for (char c : libraryPath)
        if (IsSeparator(c))
            bare = false;
    if (absolute || bare)
        return std::string(libraryPath);
    const std::string_view folder = manifestPath.substr(0, manifestPath.size() - Leaf(manifestPath).size());
    std::string out(folder);
    std::string_view rest = libraryPath;
    if (rest.size() >= 2 && rest[0] == '.' && IsSeparator(rest[1]))
        rest.remove_prefix(2);
    out += rest;
    for (char& c : out)
        if (c == '/')
            c = '\\';
    return out;
}

inline char Lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

inline bool EqualNoCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (Lower(a[i]) != Lower(b[i]))
            return false;
    return true;
}

inline bool ContainsNoCase(std::string_view text, std::string_view part)
{
    if (part.empty() || part.size() > text.size())
        return part.empty();
    for (size_t i = 0; i + part.size() <= text.size(); ++i)
        if (EqualNoCase(text.substr(i, part.size()), part))
            return true;
    return false;
}

// Whether a loaded module is the manifest's library: the same full path when the manifest gives one, the
// same file name when it gives a bare name.
inline bool ModuleIsLibrary(std::string_view modulePath, std::string_view resolvedLibrary)
{
    if (resolvedLibrary.empty())
        return false;
    if (Leaf(resolvedLibrary).size() == resolvedLibrary.size())
        return EqualNoCase(Leaf(modulePath), resolvedLibrary);
    return EqualNoCase(modulePath, resolvedLibrary);
}

enum class State : uint8_t
{
    Loaded = 0,       // its DLL is in the process: it is in the call chain
    RegistryOff,      // the registry value is not 0
    DisabledByEnv,    // its disable_environment variable is set
    NotEnabledByEnv,  // it asks for an enable_environment variable that is not set
    NoManifest,       // the manifest could not be read
    NotLoaded,        // nothing said to skip it, yet its DLL is not here (another bitness, a failed load)
};

constexpr State Classify(uint32_t registryValue, bool manifestRead, bool disableSet, bool needsEnable,
                         bool enableSet, bool libraryLoaded)
{
    if (libraryLoaded)
        return State::Loaded;
    if (registryValue != 0)
        return State::RegistryOff;
    if (!manifestRead)
        return State::NoManifest;
    if (disableSet)
        return State::DisabledByEnv;
    if (needsEnable && !enableSet)
        return State::NotEnabledByEnv;
    return State::NotLoaded;
}

constexpr const char* Describe(State state)
{
    switch (state)
    {
    case State::Loaded: return "LOADED";
    case State::RegistryOff: return "off in the registry";
    case State::DisabledByEnv: return "off (its disable variable is set)";
    case State::NotEnabledByEnv: return "off (it needs an enable variable that is not set)";
    case State::NoManifest: return "manifest not readable";
    case State::NotLoaded: return "registered but not loaded";
    }
    return "?";
}

// Layers known to break the mod, matched by part of the layer's name or its DLL's file name.
struct KnownProblem
{
    const char* match;
    const char* advice;
};

inline constexpr KnownProblem kKnownProblems[] = {
    {"ofxr", "OFXR Bridge frame generation has crashed the mod a few seconds into Project Reclaimer; turning "
             "OFXR's frame generation off stopped the crashes"},
};

// The advice for a layer, or nullptr when it is not a known problem.
inline const char* KnownProblemAdvice(std::string_view layerName, std::string_view libraryPath)
{
    for (const KnownProblem& problem : kKnownProblems)
        if (ContainsNoCase(layerName, problem.match) || ContainsNoCase(Leaf(libraryPath), problem.match))
            return problem.advice;
    return nullptr;
}
} // namespace openxr_layer_report
