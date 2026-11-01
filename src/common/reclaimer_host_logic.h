#pragma once

// Project Reclaimer (projectreclaimer.dev) is a standalone client that loads MCC's own halo3.dll without MCC's
// shell. Its executable is one file run in several roles (measured on 0.9.8):
//
// (0.9.23 names it project-reclaimer-client-v0.9.23.exe; IsClientExeName takes both names.)
//
//   project-reclaimer-v0.9.8.exe                       what the player starts; it starts itself again and exits
//     project-reclaimer-v0.9.8.exe                     the supervisor: menus' owner, no engine
//       project-reclaimer-v0.9.8.exe probe-engine-worker "<...>\halo3\halo3.dll" --guardian-slayer
//           --client-window --startup ...              the engine worker: halo3.dll, the window, the D3D11 device
//
// So the process the launcher starts is two generations away from the one the mod belongs in. These are the pure
// decisions the launcher and the relay DLL (src/relay/reclaimer_relay.cpp) share: which file is a client, what
// role a command line starts, and which of several client files is the newest. No Windows calls here; the unit
// test is tests/reclaimer_host_logic_tests.cpp.

#include <cstdint>
#include <cstring>
#include <cwctype>
#include <string>
#include <string_view>
#include <vector>

namespace reclaimer_host
{
inline std::wstring Lower(std::wstring_view text)
{
    std::wstring out(text);
    for (wchar_t& c : out)
        c = static_cast<wchar_t>(std::towlower(c));
    return out;
}

inline std::wstring_view Leaf(std::wstring_view path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring_view::npos ? path : path.substr(slash + 1);
}

// The game client's file name: project-reclaimer-client-v<version>.exe (0.9.23 on), project-reclaimer-v<version>.exe
// (up to 0.9.12 at least), or either without its version. The dedicated server (project-reclaimer-dedicated-v*.exe)
// and the remote console (reclaimer-rcon-v*.exe) are other programs and never get the mod.
inline bool IsClientExeName(std::wstring_view leaf)
{
    const std::wstring name = Lower(leaf);
    static constexpr std::wstring_view kPrefix = L"project-reclaimer";
    static constexpr std::wstring_view kClient = L"-client";
    static constexpr std::wstring_view kSuffix = L".exe";
    if (name.size() < kPrefix.size() + kSuffix.size())
        return false;
    if (name.compare(0, kPrefix.size(), kPrefix) != 0)
        return false;
    if (name.compare(name.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0)
        return false;
    std::wstring_view middle(name.data() + kPrefix.size(), name.size() - kPrefix.size() - kSuffix.size());
    if (middle.substr(0, kClient.size()) == kClient)
        middle.remove_prefix(kClient.size());
    if (middle.empty())
        return true;
    // "-v0.9.8" and nothing else: a name with another word in it is another program.
    if (middle.size() < 3 || middle[0] != L'-' || middle[1] != L'v')
        return false;
    for (size_t i = 2; i < middle.size(); ++i)
        if (!(middle[i] >= L'0' && middle[i] <= L'9') && middle[i] != L'.')
            return false;
    return true;
}

// A command line's arguments, by the rule that matters here: spaces separate, double quotes group. Enough to
// tell an argument from the same text inside a quoted path.
inline std::vector<std::wstring> Arguments(std::wstring_view commandLine)
{
    std::vector<std::wstring> out;
    std::wstring current;
    bool quoted = false, any = false;
    for (const wchar_t c : commandLine)
    {
        if (c == L'"')
        {
            quoted = !quoted;
            any = true;
        }
        else if (!quoted && (c == L' ' || c == L'\t'))
        {
            if (any)
                out.push_back(current);
            current.clear();
            any = false;
        }
        else
        {
            current.push_back(c);
            any = true;
        }
    }
    if (any)
        out.push_back(current);
    return out;
}

// THE CLIENT'S CENTERED CROSSHAIR. Project Reclaimer draws its crosshair below the screen's centre unless
// "centered_crosshair" is true in its settings.json (Settings > HUD), and then pitches the camera above the aim; the
// mod's direct drive waits for the camera and the aim to agree, so with it off the hand never takes over the
// game's aim. The launcher turns it on before it starts the client, changing nothing else in the file.
enum class CenteredCrosshairEdit
{
    AlreadyOn,  // nothing to write
    TurnedOn,   // `out` is the file with the key set to true
    Created,    // there was no file: `out` is a new one with only the version and the key
    Unreadable, // the key's value is not a JSON boolean, or the file is not one object: left alone
};
inline CenteredCrosshairEdit WithCenteredCrosshair(const std::string& text, std::string& out)
{
    constexpr std::string_view kKey = "\"centered_crosshair\"";
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    size_t first = 0;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
        first = 3;
    while (first < text.size() && space(text[first]))
        ++first;
    if (first == text.size())
    {
        out = "{\n  \"version\": 1,\n  \"centered_crosshair\": true\n}\n";
        return CenteredCrosshairEdit::Created;
    }
    if (text[first] != '{')
        return CenteredCrosshairEdit::Unreadable;
    const size_t key = text.find(kKey);
    if (key != std::string::npos)
    {
        size_t at = key + kKey.size();
        while (at < text.size() && space(text[at]))
            ++at;
        if (at >= text.size() || text[at] != ':')
            return CenteredCrosshairEdit::Unreadable;
        ++at;
        while (at < text.size() && space(text[at]))
            ++at;
        if (text.compare(at, 4, "true") == 0)
            return CenteredCrosshairEdit::AlreadyOn;
        if (text.compare(at, 5, "false") != 0)
            return CenteredCrosshairEdit::Unreadable;
        out = text.substr(0, at) + "true" + text.substr(at + 5);
        return CenteredCrosshairEdit::TurnedOn;
    }
    size_t next = first + 1;
    while (next < text.size() && space(text[next]))
        ++next;
    const bool empty = next < text.size() && text[next] == '}';
    out = text.substr(0, first + 1) + "\n  \"centered_crosshair\": true" + (empty ? "\n" : ",") +
          text.substr(first + 1);
    return CenteredCrosshairEdit::TurnedOn;
}

// THE CLIENT'S DISPLAY MODE. Started in exclusive fullscreen ("display_mode": "exclusive_fullscreen"), the client
// has run at about 10 fps beside the headset's session from its first frame, and a few seconds later the graphics
// card stopped answering and was reset (0x887A0006); started windowed or borderless it runs normally. The desktop
// window is only a mirror of the headset's picture, so the launcher starts the client borderless instead, changing
// nothing else in the file. A file without the key is left alone: borderless is the client's default.
enum class DisplayModeEdit
{
    Kept,           // not exclusive fullscreen: nothing to write
    MadeBorderless, // `out` is the file with the value set to "borderless"
    Unreadable,     // the key's value is not a JSON string: left alone
};
// Where "display_mode"'s value starts in the file: its opening quote. Missing when the file has no such key,
// NotAString when the value is anything but a JSON string.
enum class DisplayModeValue
{
    Missing,
    Found,
    NotAString,
};
inline DisplayModeValue FindDisplayModeValue(const std::string& text, size_t& quote)
{
    constexpr std::string_view kKey = "\"display_mode\"";
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    const size_t key = text.find(kKey);
    if (key == std::string::npos)
        return DisplayModeValue::Missing;
    size_t at = key + kKey.size();
    while (at < text.size() && space(text[at]))
        ++at;
    if (at >= text.size() || text[at] != ':')
        return DisplayModeValue::NotAString;
    ++at;
    while (at < text.size() && space(text[at]))
        ++at;
    if (at >= text.size() || text[at] != '"')
        return DisplayModeValue::NotAString;
    quote = at;
    return DisplayModeValue::Found;
}

inline DisplayModeEdit WithoutExclusiveFullscreen(const std::string& text, std::string& out)
{
    constexpr std::string_view kExclusive = "\"exclusive_fullscreen\"";
    size_t at = 0;
    const DisplayModeValue value = FindDisplayModeValue(text, at);
    if (value == DisplayModeValue::Missing)
        return DisplayModeEdit::Kept;
    if (value == DisplayModeValue::NotAString)
        return DisplayModeEdit::Unreadable;
    if (text.compare(at, kExclusive.size(), kExclusive) != 0)
        return DisplayModeEdit::Kept;
    out = text.substr(0, at) + "\"borderless\"" + text.substr(at + kExclusive.size());
    return DisplayModeEdit::MadeBorderless;
}

// The client's display mode as its settings.json states it. A file without the key is borderless, the client's
// default; a value that is not one of the three is Unknown.
enum class ClientDisplayMode
{
    Unknown,
    Windowed,
    Borderless,
    ExclusiveFullscreen,
};
inline ClientDisplayMode ReadClientDisplayMode(const std::string& text)
{
    size_t at = 0;
    const DisplayModeValue value = FindDisplayModeValue(text, at);
    if (value == DisplayModeValue::Missing)
        return ClientDisplayMode::Borderless;
    if (value == DisplayModeValue::NotAString)
        return ClientDisplayMode::Unknown;
    const auto is = [&](std::string_view quoted) { return text.compare(at, quoted.size(), quoted) == 0; };
    if (is("\"windowed\""))
        return ClientDisplayMode::Windowed;
    if (is("\"borderless\""))
        return ClientDisplayMode::Borderless;
    if (is("\"exclusive_fullscreen\""))
        return ClientDisplayMode::ExclusiveFullscreen;
    return ClientDisplayMode::Unknown;
}

// A path as a person gives it: pasted with quotes round it, with spaces or a line end after it.
inline std::wstring TidyPath(std::wstring_view text)
{
    const auto stray = [](wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'"' || c == 0xFEFF;
    };
    while (!text.empty() && stray(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && stray(text.back()))
        text.remove_suffix(1);
    return std::wstring(text);
}

// The game client a shell "open" command starts, as the client registers for its own join links:
// "D:\Games\Project Reclaimer\0.9.8\project-reclaimer-v0.9.8.exe" "%1". Quoted or not (an unquoted path may hold
// spaces: it is then taken up to its ".exe"). Empty when the command does not start a game client.
inline std::wstring ClientOfOpenCommand(std::wstring_view command)
{
    const std::vector<std::wstring> arguments = Arguments(command);
    if (!arguments.empty() && IsClientExeName(Leaf(arguments[0])))
        return arguments[0];
    const std::wstring lower = Lower(command);
    const size_t end = lower.find(L".exe");
    if (end == std::wstring::npos)
        return {};
    const std::wstring program = TidyPath(command.substr(0, end + 4));
    return IsClientExeName(Leaf(program)) ? program : std::wstring();
}

enum class Role
{
    Supervisor,   // no engine: only passes the relay on to what it starts
    ClientWorker, // the engine with the player's window: the mod belongs here
    OtherWorker,  // an engine without the player's window (a local server): never the mod
};

inline const char* RoleName(Role role)
{
    switch (role)
    {
    case Role::ClientWorker: return "client worker";
    case Role::OtherWorker: return "other worker";
    default: return "supervisor";
    }
}

inline Role RoleOf(std::wstring_view commandLine)
{
    bool worker = false, window = false;
    const std::vector<std::wstring> arguments = Arguments(commandLine);
    for (size_t i = 1; i < arguments.size(); ++i) // [0] is the program
    {
        if (arguments[i] == L"probe-engine-worker")
            worker = true;
        else if (arguments[i] == L"--client-window")
            window = true;
    }
    if (!worker)
        return Role::Supervisor;
    return window ? Role::ClientWorker : Role::OtherWorker;
}

struct Version
{
    int part[3]{};
    bool known = false;
};

// project-reclaimer-v0.9.8.exe -> 0.9.8. A name without a version is "unknown" and loses to any known one.
inline Version VersionOf(std::wstring_view leaf)
{
    Version version;
    const std::wstring name = Lower(leaf);
    const size_t at = name.rfind(L"-v");
    if (at == std::wstring::npos)
        return version;
    size_t i = at + 2;
    int parts = 0;
    while (parts < 3 && i < name.size() && name[i] >= L'0' && name[i] <= L'9')
    {
        int value = 0;
        while (i < name.size() && name[i] >= L'0' && name[i] <= L'9')
        {
            if (value > 100000)
                return Version{};
            value = value * 10 + (name[i++] - L'0');
        }
        version.part[parts++] = value;
        if (i < name.size() && name[i] == L'.' && i + 1 < name.size() && name[i + 1] >= L'0' && name[i + 1] <= L'9')
            ++i;
        else
            break;
    }
    version.known = parts > 0;
    return version;
}

// True when `candidate` should replace `best`.
inline bool Newer(const Version& candidate, const Version& best)
{
    if (candidate.known != best.known)
        return candidate.known;
    for (int i = 0; i < 3; ++i)
        if (candidate.part[i] != best.part[i])
            return candidate.part[i] > best.part[i];
    return false;
}

// THE CLIENT VERSIONS THE MOD HAS BEEN RUN WITH. What the mod reads of the host -- which of its processes holds
// the engine, that the host's own drawing is bound from its executable, the hooks it places in the game -- was
// measured on these and no others. So of several clients the launcher starts one of these before any other,
// whatever newer client sits beside it; a client outside the list is started only when there is no other, and the
// launcher's log says so either way. A client named in reclaimer_client.txt is the player's own choice and is
// started whatever its version.
inline constexpr Version kProvenVersions[] = {{{0, 9, 8}, true}, {{0, 9, 10}, true}, {{0, 9, 11}, true},
                                              {{0, 9, 12}, true}};
inline bool Proven(const Version& version)
{
    if (!version.known)
        return false;
    for (const Version& proven : kProvenVersions)
        if (proven.part[0] == version.part[0] && proven.part[1] == version.part[1] &&
            proven.part[2] == version.part[2])
            return true;
    return false;
}

// One client found on disk. `time` is its file time in any unit that orders (the newer file is the larger).
struct ClientCandidate
{
    std::wstring path;
    Version version;
    int64_t time = 0;
};
// Which of them to start: a proven version before an unproven one, then the higher version, then the newer file.
// `passedOver` is the newest client that is NEWER than the chosen one and was not chosen (so the log can name it);
// -1 when there is none. Both are indexes into `candidates`; `chosen` is -1 for an empty list.
struct ClientChoice
{
    int chosen = -1;
    int passedOver = -1;
};
inline ClientChoice ChooseClient(const std::vector<ClientCandidate>& candidates)
{
    ClientChoice choice;
    const auto better = [&candidates](int a, int b) {   // true when a should replace b
        const ClientCandidate& x = candidates[static_cast<size_t>(a)];
        const ClientCandidate& y = candidates[static_cast<size_t>(b)];
        if (Proven(x.version) != Proven(y.version))
            return Proven(x.version);
        if (Newer(x.version, y.version))
            return true;
        if (Newer(y.version, x.version))
            return false;
        return x.time > y.time;
    };
    for (int i = 0; i < static_cast<int>(candidates.size()); ++i)
        if (choice.chosen < 0 || better(i, choice.chosen))
            choice.chosen = i;
    for (int i = 0; i < static_cast<int>(candidates.size()); ++i)
    {
        if (i == choice.chosen ||
            !Newer(candidates[static_cast<size_t>(i)].version, candidates[static_cast<size_t>(choice.chosen)].version))
            continue;
        if (choice.passedOver < 0 || Newer(candidates[static_cast<size_t>(i)].version,
                                           candidates[static_cast<size_t>(choice.passedOver)].version))
            choice.passedOver = i;
    }
    return choice;
}

// The event the relay creates for an engine worker and the mod sets once its render-size and D3D11 hooks are
// in: until then the relay keeps the worker suspended. One name per worker process.
inline void EarlyHooksEventName(wchar_t* out, size_t capacity, unsigned long pid)
{
    if (!capacity)
        return;
    static constexpr wchar_t kPrefix[] = L"Local\\HaloMCCVR-EarlyHooks-";
    size_t n = 0;
    for (; kPrefix[n] && n + 1 < capacity; ++n)
        out[n] = kPrefix[n];
    wchar_t digits[16];
    int count = 0;
    do
    {
        digits[count++] = static_cast<wchar_t>(L'0' + pid % 10);
        pid /= 10;
    } while (pid && count < 16);
    while (count && n + 1 < capacity)
        out[n++] = digits[--count];
    out[n] = 0;
}

// IS WHAT THE HOST DRAWS OVER THE FRAME A SCREEN? The host draws its menus into the game's frame, and also
// small things during play: the name of whoever is talking, in a corner; chat lines; a hint. Only a screen may
// take the stereo picture away: taken for one, a talker's name flattens the picture every time a player speaks.
//
// THE MEASUREMENT DECIDES WHERE THERE IS ONE: how much of the picture the host's drawing changes (ChangedTexels
// below, in parts of a thousand). Measured on 0.9.8: a screen of the host's (the main menu, a loading screen, the
// in-game menu with its scrim) changes all of the frame; the Forge menu, a panel beside the play, 142; Forge's
// on-screen hint 7.
//   - A SMALL drawing (under kSmallPermille; it has to reach kNotSmallPermille to stop being one) is an overlay
//     whatever else is true: a name while the player is dead, or one that outlasts a closed menu, does not flatten
//     the picture.
//   - ANYTHING MORE IS A SCREEN, in play too and whoever opened it. A menu does not have to cover the picture, and
//     the key that opens it is the host's to choose (Forge's is neither Menu nor Esc): size is the one sign every
//     menu shares. The cost of the other mistake is small: an overlay taken for a screen is a flat picture for as
//     long as it is drawn, and it can still be seen.
//   - WHAT A SCREEN COSTS THE PICTURE depends on whether the host's UI can be shown as a panel (below): over a
//     frame of the game it can, and the picture stays stereo; the rule then only says whether the menu pointer
//     may work on the panel.
//   - A reading missed for a moment does not undo the last one (kMenuOpensMs).
// THE MEASUREMENT COUNTS PIXELS THAT CHANGED, NOT PIXELS DRAWN, so it means what it says only over a frame of the
// game (In::gameFrame). With none under it -- no level: the host's own menus and loading screens -- the host
// redraws its picture over its own last frame, and a screen that fills the display can read as nothing at all.
// There the measurement is not consulted.
//
// THE RULE WITHOUT A MEASUREMENT (the picture cannot be read back, no frame of the game under it, or the first
// frames of a drawing): while the player is not in play (no level, or no character of theirs) whatever the host
// draws is the picture; in play it is a screen only when the player's own press opened it (the controller's Menu,
// the pause chord, Esc): the host began or went on drawing within kMenuOpensMs of the press, held until the host
// stops drawing or the player presses again. A press with a menu up may also be a step back inside it; unmeasured
// there is no telling, and it is taken as the close.
//
// A SHOWN MOUSE POINTER IS NOT A SIGN OF A SCREEN: in a match the pointer counts as shown for one to four seconds
// at a time with no menu open.
struct ScreenRule
{
    static constexpr uint64_t kMenuOpensMs = 1500;
    static constexpr uint32_t kSmallPermille = 50, kNotSmallPermille = 80;
    // Below this a press's screen has gone, whatever is left: the host's menus read about 1000 and its scoreboard 700 or
    // more, and a vote's popup left under a closed menu reads 56 to 82, around the two thresholds above.
    static constexpr uint32_t kOpenedFloorPermille = 300;
    struct In
    {
        bool hostDraws = false;
        bool inPlay = false;
        uint32_t menuPressSerial = 0; // counts the player's Menu and Esc presses
        uint64_t nowMs = 0;
        bool coverageKnown = false;
        uint32_t coveragePermille = 0;
        bool gameFrame = false;       // a frame of the game lies under the host's drawing (the engine is rendering)
    };
    bool Step(const In& in)
    {
        if (in.menuPressSerial != seenPress_)
        {
            seenPress_ = in.menuPressSerial;
            if (byMenu_)
            {
                // With a menu up, the same press either closes it or only steps back inside it (Esc out of a
                // settings page). Measured, the drawing's size says which; unmeasured, it is the close.
                closeAsked_ = true;
                pending_ = false;
            }
            else
            {
                pending_ = true;
                pendingAtMs_ = in.nowMs;
            }
        }
        if (pending_ && in.nowMs - pendingAtMs_ > kMenuOpensMs)
            pending_ = false;
        if (!in.hostDraws)
        {
            byMenu_ = sized_ = small_ = closeAsked_ = false;
            return false;
        }
        if (pending_)
        {
            byMenu_ = true;
            pending_ = closeAsked_ = false;
            byMenuAtMs_ = in.nowMs;
        }
        if (!in.coverageKnown || !in.gameFrame)
        {
            // The last reading of this drawing stands for a moment when one is missed.
            if (in.gameFrame && sized_ && in.nowMs - sizedAtMs_ <= kMenuOpensMs)
                return !small_;
            // Unmeasured, nothing tells a menu that stayed up from a name that outlasts a closed one. The press is
            // taken as the close: of the two mistakes, a picture kept flat for as long as people talk is the worse.
            if (closeAsked_)
                byMenu_ = closeAsked_ = false;
            return !in.inPlay || byMenu_;
        }
        // The first reading of a drawing sizes it by the upper threshold; after that it has to cross one to change.
        if (!sized_)
            small_ = in.coveragePermille < kNotSmallPermille;
        else if (in.coveragePermille < kSmallPermille)
            small_ = true;
        else if (in.coveragePermille >= kNotSmallPermille)
            small_ = false;
        sized_ = true;
        sizedAtMs_ = in.nowMs;
        // What the press latch says must stay true for a later unmeasured moment: the menu is gone once the
        // drawing is small -- at once after a press that may have closed it, otherwise after the time a menu takes
        // to come (a press whose menu never came, or went leaving a name behind, must not flatten the picture
        // later). And a drawing that is more than an overlay is a menu that is up, whoever opened it.
        if (in.coveragePermille < kOpenedFloorPermille && byMenu_ &&
            (closeAsked_ || in.nowMs - byMenuAtMs_ > kMenuOpensMs))
            byMenu_ = closeAsked_ = false;
        if (!small_)
            closeAsked_ = false;
        return !small_;
    }
    // Whether the player's own press opened what is up. In play, only then may it take the controller: a popup
    // nobody opened (a vote to kick can read 100% of the picture) is as large as a menu, and the player must go on
    // walking under it.
    bool OpenedByPress() const { return byMenu_; }

private:
    uint32_t seenPress_ = 0;
    bool pending_ = false, byMenu_ = false, sized_ = false, small_ = false, closeAsked_ = false;
    uint64_t pendingAtMs_ = 0, byMenuAtMs_ = 0, sizedAtMs_ = 0;
};

// THE HOST'S UI AS A PANEL OVER THE STEREO PICTURE. The host draws its UI into the frame after the mod has taken
// each eye's picture, so in stereo none of it reaches the headset; and showing the flat frame instead costs the
// player the stereo picture for as long as a menu is up. So each of the host's draws over the frame is drawn once
// more into a transparent picture of the frame's own size (d3d11_hook.cpp, HostUiLayer), and that picture is shown
// as one layer in front of the eyes (vr_host_ui_panel.inl): the UI where it sits on the frame, the game in stereo
// behind it.
//   - The picture has the frame's size and layout whatever the host draws from. Measured on 0.9.10: one quad of 4
//     vertices a frame, sampling a texture the size of the widget (720x92 for a line of chat, 944x968 for a menu,
//     the frame's own size for a full screen). So a point on the panel is the same point on the frame, and the
//     menu pointer works on it unchanged.
//   - Its colour is premultiplied and its alpha is coverage (host_ui_layer_blend.h says why).
//   - A draw with blending off writes its texture's own alpha, which nothing vouches for. PanelAlphaFor: a picture
//     made only of such draws that fills the frame is shown opaque; anything else is taken as premultiplied.
// The flat picture remains for what has no frame of the game under it (the host's own menus and loading
// screens), and wherever the panel cannot be made.
inline constexpr uint32_t kPanelMaxWidth = 2560;
// A frame the picture can be made for.
inline bool PanelFrameUsable(uint32_t frameW, uint32_t frameH, uint32_t samples)
{
    return samples == 1 && frameW >= 640 && frameH >= 360 && frameW <= 16384 && frameH <= 16384;
}
// The size of the panel's image for a frame of this size: the frame's own, halved until it is no wider than
// kPanelMaxWidth (a panel a few metres away shows no more than that, and the copy is made every frame).
inline void PanelImageSize(uint32_t frameW, uint32_t frameH, uint32_t& w, uint32_t& h)
{
    w = frameW;
    h = frameH;
    while (w > kPanelMaxWidth && w >= 2 && h >= 2)
    {
        w /= 2;
        h /= 2;
    }
}
enum class PanelAlpha
{
    Premultiplied,
    Opaque,
};
// `draws` / `unblended`: the host's draws into this frame's picture, and how many of them had blending off.
// `coveragePermille`: how much of the frame the host's drawing changed (ChangedTexels), when known.
inline PanelAlpha PanelAlphaFor(uint32_t draws, uint32_t unblended, bool coverageKnown, uint32_t coveragePermille)
{
    return draws && unblended == draws && coverageKnown && coveragePermille >= 900 ? PanelAlpha::Opaque
                                                                                    : PanelAlpha::Premultiplied;
}
// Does a screen of the host's leave the picture stereo? Only over a frame of the game (there is no stereo picture
// without one), and only while the panel is being made.
inline bool ScreenShownAsPanel(bool screen, bool gameFrame, bool panelUsable)
{
    return screen && gameFrame && panelUsable;
}

// How many texels differ between two readbacks of the same size (rows `beforePitch` / `afterPitch` bytes apart,
// `bytesPerTexel` each). The two are the same rows of the frame just before the host drew into it and just
// before it is presented; nothing else draws in between, so a texel differs only where the host drew.
inline uint32_t ChangedTexels(const uint8_t* before, size_t beforePitch, const uint8_t* after, size_t afterPitch,
                              uint32_t width, uint32_t height, uint32_t bytesPerTexel)
{
    if (!before || !after || !width || !height || !bytesPerTexel ||
        beforePitch < static_cast<size_t>(width) * bytesPerTexel ||
        afterPitch < static_cast<size_t>(width) * bytesPerTexel)
        return 0;
    uint32_t changed = 0;
    for (uint32_t y = 0; y < height; ++y)
    {
        const uint8_t* a = before + static_cast<size_t>(y) * beforePitch;
        const uint8_t* b = after + static_cast<size_t>(y) * afterPitch;
        for (uint32_t x = 0; x < width; ++x, a += bytesPerTexel, b += bytesPerTexel)
            if (std::memcmp(a, b, bytesPerTexel) != 0)
                ++changed;
    }
    return changed;
}

// THE SIZE PROJECT RECLAIMER RENDERS AT. Its Render resolution is a preset (720p to 2160p, all 16:9) or Native: the
// current display mode of the window's monitor, read with EnumDisplaySettingsW(ENUM_CURRENT_SETTINGS) and accepted
// from 640 x 480 to 7680 x 4320 (outside that it renders 1920 x 1080; measured on 0.9.8). The mod answers that
// read with the size the Picture panel asks for, so with Native the game renders at the size it would in MCC.
// `width` x `height` is fitted into the accepted range: scaled down to fit, its shape kept, both even. False when
// no size in the range has that shape or a side is 0.
inline constexpr unsigned kNativeRenderMinWidth = 640, kNativeRenderMinHeight = 480;
inline constexpr unsigned kNativeRenderMaxWidth = 7680, kNativeRenderMaxHeight = 4320;
inline bool FitNativeRenderSize(unsigned& width, unsigned& height) noexcept
{
    if (!width || !height)
        return false;
    double scale = 1.0;
    if (width > kNativeRenderMaxWidth)
        scale = static_cast<double>(kNativeRenderMaxWidth) / width;
    if (height * scale > kNativeRenderMaxHeight)
        scale = static_cast<double>(kNativeRenderMaxHeight) / height;
    const unsigned w = static_cast<unsigned>(width * scale) & ~1u;
    const unsigned h = static_cast<unsigned>(height * scale) & ~1u;
    if (w < kNativeRenderMinWidth || h < kNativeRenderMinHeight || w > kNativeRenderMaxWidth ||
        h > kNativeRenderMaxHeight)
        return false;
    width = w;
    height = h;
    return true;
}

// WHAT THE NATIVE READ IS ANSWERED. The one size Native reads becomes the client's render size AND its swapchain's
// buffers, and in exclusive fullscreen the display mode DXGI switches to (the client asks for no mode of its own).
// So a size larger than the real display is a buffer larger than the screen: in exclusive fullscreen a 2912 x 2100
// buffer on a 2560 x 1440 display ran at about 10 fps from the first frame until the graphics card was reset, and
// borderless also failed on that PC. Exclusive fullscreen is therefore told the real display mode. Any other mode
// is given the Picture panel's size, scaled down with its shape kept until it fits inside the display when
// `fitToDisplay` (the player's choice: a smaller picture is a softer one in the headset). Windowed ignores the
// answer (it renders 1920 x 1080 on Native), so it is answered the same way as borderless.
enum class NativeAnswer
{
    Panel,              // the Picture panel's size, as asked
    PanelWithinDisplay, // the Picture panel's shape, scaled down to fit inside the display
    Display,            // the real display mode, unchanged
};
inline NativeAnswer AnswerNativeRenderSize(unsigned wantWidth, unsigned wantHeight, unsigned displayWidth,
                                           unsigned displayHeight, ClientDisplayMode mode, bool fitToDisplay,
                                           unsigned& width, unsigned& height) noexcept
{
    width = displayWidth;
    height = displayHeight;
    if (mode == ClientDisplayMode::ExclusiveFullscreen || !wantWidth || !wantHeight || !displayWidth ||
        !displayHeight)
        return NativeAnswer::Display;
    if (!fitToDisplay)
    {
        unsigned w = wantWidth, h = wantHeight;
        if (!FitNativeRenderSize(w, h))
            return NativeAnswer::Display;
        width = w;
        height = h;
        return NativeAnswer::Panel;
    }
    // Whole numbers, so the side that meets the display meets it exactly.
    uint64_t sw = wantWidth, sh = wantHeight;
    if (sw > displayWidth || sh > displayHeight)
    {
        if (sw * displayHeight > sh * displayWidth)
        {
            sh = sh * displayWidth / sw;
            sw = displayWidth;
        }
        else
        {
            sw = sw * displayHeight / sh;
            sh = displayHeight;
        }
    }
    unsigned w = static_cast<unsigned>(sw) & ~1u;
    unsigned h = static_cast<unsigned>(sh) & ~1u;
    if (w > displayWidth || h > displayHeight || !FitNativeRenderSize(w, h))
        return NativeAnswer::Display;
    width = w;
    height = h;
    return w == wantWidth && h == wantHeight ? NativeAnswer::Panel : NativeAnswer::PanelWithinDisplay;
}
} // namespace reclaimer_host
