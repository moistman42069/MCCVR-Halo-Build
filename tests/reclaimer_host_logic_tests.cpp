// The Project Reclaimer decisions the launcher and the relay share (src/common/reclaimer_host_logic.h).
#include "../src/common/reclaimer_host_logic.h"
#include <cmath>
#include <cstdio>

using namespace reclaimer_host;

static int g_failed = 0;
#define CHECK(condition) \
    do { if (!(condition)) { ++g_failed; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition); } } while (0)

static void PopupInPlayIsNotOpenedByPress()
{
    using reclaimer_host::ScreenRule;
    ScreenRule rule;
    ScreenRule::In in{};
    in.hostDraws = true;in.inPlay = true;in.gameFrame = true;in.coverageKnown = true;in.coveragePermille = 1000;
    in.nowMs = 1000;
    const bool popupIsScreen = rule.Step(in);
    const bool popupOpened = rule.OpenedByPress();
    // Closed, then the player's own Menu press opens one.
    in.hostDraws = false;in.nowMs = 2000;rule.Step(in);
    in.menuPressSerial = 1;in.nowMs = 2100;rule.Step(in);
    in.hostDraws = true;in.nowMs = 2200;
    const bool menuIsScreen = rule.Step(in);
    const bool menuOpened = rule.OpenedByPress();
    // Its close: the drawing goes.
    in.hostDraws = false;in.nowMs = 5000;rule.Step(in);
    const bool stillOpened = rule.OpenedByPress();
    CHECK(popupIsScreen && !popupOpened);
    CHECK(menuIsScreen && menuOpened);
    CHECK(!stillOpened);
}

static void ScoreboardKeyOpensAScreen()
{
    using reclaimer_host::ScreenRule;
    ScreenRule rule;
    ScreenRule::In in{};
    in.hostDraws = true;in.inPlay = true;in.gameFrame = true;in.coverageKnown = true;in.coveragePermille = 20;
    in.nowMs = 1000;
    const bool overlayIsScreen = rule.Step(in);
    // The key goes down and the scoreboard comes up.
    in.menuPressSerial = 1;in.nowMs = 1100;rule.Step(in);
    in.coveragePermille = 723;in.nowMs = 1200;
    const bool boardIsScreen = rule.Step(in);
    const bool boardOpened = rule.OpenedByPress();
    in.nowMs = 9000;rule.Step(in);
    const bool stillOpen = rule.OpenedByPress();
    // Tapped again, it closes back to the overlay.
    in.menuPressSerial = 2;in.coveragePermille = 30;in.nowMs = 9100;
    const bool closedIsScreen = rule.Step(in);
    const bool closedOpened = rule.OpenedByPress();
    CHECK(!overlayIsScreen);
    CHECK(boardIsScreen && boardOpened && stillOpen);
    CHECK(!closedIsScreen && !closedOpened);
}

static void MenuClosedOverAPopupLetsGo()
{
    using reclaimer_host::ScreenRule;
    ScreenRule rule;
    ScreenRule::In in{};
    in.hostDraws = true;in.inPlay = true;in.gameFrame = true;in.coverageKnown = true;in.coveragePermille = 64;
    in.nowMs = 1000;rule.Step(in);
    in.menuPressSerial = 1;in.coveragePermille = 1000;in.nowMs = 1100;rule.Step(in);
    const bool menuOpened = rule.OpenedByPress();
    // Closed by a press: the popup is what is left.
    in.menuPressSerial = 2;in.coveragePermille = 64;in.nowMs = 9000;rule.Step(in);
    const bool afterPress = rule.OpenedByPress();
    // Opened again, then closed by clicking Resume: no press at all.
    in.menuPressSerial = 3;in.coveragePermille = 1000;in.nowMs = 10000;rule.Step(in);
    const bool reopened = rule.OpenedByPress();
    in.coveragePermille = 64;in.nowMs = 20000;rule.Step(in);
    const bool afterClick = rule.OpenedByPress();
    // A menu opened over the popup keeps its moment to come up.
    in.menuPressSerial = 4;in.nowMs = 21000;rule.Step(in);
    const bool stillComing = rule.OpenedByPress();
    CHECK(menuOpened && !afterPress && reopened && !afterClick && stillComing);
}

int main()
{
    PopupInPlayIsNotOpenedByPress();
    MenuClosedOverAPopupLetsGo();
    ScoreboardKeyOpensAScreen();
    // The path a person gives, and the client a join-link command starts.
    {
        const auto Check = [](bool ok, const char* what) {
            if (!ok)
            {
                ++g_failed;
                std::printf("FAIL %s\n", what);
            }
        };
        Check(TidyPath(L"  \"D:\\Some Games\\Project Reclaimer\\0.9.8\\project-reclaimer-v0.9.8.exe\" \r\n") ==
                  L"D:\\Some Games\\Project Reclaimer\\0.9.8\\project-reclaimer-v0.9.8.exe",
              "a pasted path loses its quotes, spaces and line end");
        Check(TidyPath(L" \t\"\" ").empty() && TidyPath(L"").empty(), "nothing but quotes and spaces is nothing");
        Check(ClientOfOpenCommand(L"\"D:\\Some Games\\Project Reclaimer\\0.9.8\\project-reclaimer-v0.9.8.exe\" \"%1\"") ==
                  L"D:\\Some Games\\Project Reclaimer\\0.9.8\\project-reclaimer-v0.9.8.exe",
              "the join link's command names the client");
        Check(ClientOfOpenCommand(L"D:\\Some Games\\Project Reclaimer\\project-reclaimer.exe %1") ==
                  L"D:\\Some Games\\Project Reclaimer\\project-reclaimer.exe",
              "an unquoted path with spaces is taken up to its .exe");
        Check(ClientOfOpenCommand(L"C:\\Tools\\project-reclaimer-v1.0.0.EXE") ==
                  L"C:\\Tools\\project-reclaimer-v1.0.0.EXE",
              "a command that is only the program");
        Check(ClientOfOpenCommand(L"\"C:\\Games\\project-reclaimer-dedicated-v0.9.8.exe\" \"%1\"").empty(),
              "the dedicated server is not the game client");
        Check(ClientOfOpenCommand(L"\"C:\\Windows\\explorer.exe\" \"%1\"").empty() &&
                  ClientOfOpenCommand(L"").empty() && ClientOfOpenCommand(L"\"%1\"").empty(),
              "another program, or none");
    }
    // Which files are the game client.
    CHECK(IsClientExeName(L"project-reclaimer.exe"));
    CHECK(IsClientExeName(L"project-reclaimer-v0.9.8.exe"));
    CHECK(IsClientExeName(L"Project-Reclaimer-V1.0.exe"));
    CHECK(IsClientExeName(Leaf(L"D:\\Some Games\\Project Reclaimer\\0.9.8\\project-reclaimer-v0.9.8.exe")));
    // The client's centered crosshair: on, changing nothing else.
    {
        std::string out;
        CHECK(WithCenteredCrosshair("{\n  \"version\": 1,\n  \"centered_crosshair\": true\n}", out) ==
              CenteredCrosshairEdit::AlreadyOn);
        CHECK(WithCenteredCrosshair("{\"version\":1,\"centered_crosshair\" : false,\"fov\":70}", out) ==
                  CenteredCrosshairEdit::TurnedOn &&
              out == "{\"version\":1,\"centered_crosshair\" : true,\"fov\":70}");
        CHECK(WithCenteredCrosshair("\xEF\xBB\xBF{\n  \"version\": 1,\n  \"fov\": 70\n}\n", out) ==
                  CenteredCrosshairEdit::TurnedOn &&
              out == "\xEF\xBB\xBF{\n  \"centered_crosshair\": true,\n  \"version\": 1,\n  \"fov\": 70\n}\n");
        CHECK(WithCenteredCrosshair("{ }", out) == CenteredCrosshairEdit::TurnedOn &&
              out == "{\n  \"centered_crosshair\": true\n }");
        CHECK(WithCenteredCrosshair("", out) == CenteredCrosshairEdit::Created &&
              out == "{\n  \"version\": 1,\n  \"centered_crosshair\": true\n}\n");
        CHECK(WithCenteredCrosshair("{\"centered_crosshair\": 1}", out) == CenteredCrosshairEdit::Unreadable);
        CHECK(WithCenteredCrosshair("[1, 2]", out) == CenteredCrosshairEdit::Unreadable);
    }
    // The client's display mode: exclusive fullscreen becomes borderless, changing nothing else.
    {
        std::string out;
        CHECK(WithoutExclusiveFullscreen("{\n    \"display_mode\": \"exclusive_fullscreen\",\n    \"fov\": 70\n}", out) ==
                  DisplayModeEdit::MadeBorderless &&
              out == "{\n    \"display_mode\": \"borderless\",\n    \"fov\": 70\n}");
        CHECK(WithoutExclusiveFullscreen("{\"display_mode\" :\"exclusive_fullscreen\"}", out) ==
                  DisplayModeEdit::MadeBorderless &&
              out == "{\"display_mode\" :\"borderless\"}");
        CHECK(WithoutExclusiveFullscreen("{\"display_mode\": \"borderless\"}", out) == DisplayModeEdit::Kept);
        CHECK(WithoutExclusiveFullscreen("{\"display_mode\": \"windowed\"}", out) == DisplayModeEdit::Kept);
        CHECK(WithoutExclusiveFullscreen("{\"display_mode\": \"exclusive_fullscreen_x\"}", out) == DisplayModeEdit::Kept);
        CHECK(WithoutExclusiveFullscreen("{\"fov\": 70}", out) == DisplayModeEdit::Kept);
        CHECK(WithoutExclusiveFullscreen("", out) == DisplayModeEdit::Kept);
        CHECK(WithoutExclusiveFullscreen("{\"display_mode\": 2}", out) == DisplayModeEdit::Unreadable);
    }
    CHECK(IsClientExeName(L"project-reclaimer-client-v0.9.23.exe"));
    CHECK(IsClientExeName(L"project-reclaimer-client.exe"));
    CHECK(IsClientExeName(Leaf(L"D:\\Some Games\\Project Reclaimer\\0.9.23\\project-reclaimer-client-v0.9.23.exe")));
    CHECK(!IsClientExeName(L"project-reclaimer-clientx-v0.9.23.exe"));
    CHECK(!IsClientExeName(L"project-reclaimer-client-dedicated-v0.9.23.exe"));
    CHECK(VersionOf(L"project-reclaimer-client-v0.9.23.exe").part[2] == 23);
    CHECK(!IsClientExeName(L"project-reclaimer-dedicated-v0.9.8.exe"));
    CHECK(!IsClientExeName(L"reclaimer-rcon-v0.9.8.exe"));
    CHECK(!IsClientExeName(L"project-reclaimer-v0.9.8.exe.bak"));
    CHECK(!IsClientExeName(L"project-reclaimer-setup.exe"));
    CHECK(!IsClientExeName(L"MCC-Win64-Shipping.exe"));
    CHECK(!IsClientExeName(L""));

    // The roles, with the command lines measured on 0.9.8.
    const wchar_t* started = L"\"D:\\Some Games\\Project Reclaimer\\0.9.8\\project-reclaimer-v0.9.8.exe\" ";
    const wchar_t* worker =
        L"\"D:\\Some Games\\Project Reclaimer\\0.9.8\\project-reclaimer-v0.9.8.exe\" probe-engine-worker "
        L"\"\\\\?\\D:\\Some Library\\Halo The Master Chief Collection\\halo3\\halo3.dll\" "
        L"--guardian-slayer --client-window --startup --skip-render-warmup --shell-callbacks --native-udp";
    CHECK(RoleOf(started) == Role::Supervisor);
    CHECK(RoleOf(L"project-reclaimer.exe game-client") == Role::Supervisor);
    CHECK(RoleOf(worker) == Role::ClientWorker);
    CHECK(RoleOf(L"x.exe probe-engine-worker \"C:\\h\\halo3.dll\" --guardian-slayer") == Role::OtherWorker);
    // The words inside a path are not arguments.
    CHECK(RoleOf(L"x.exe game-client --mcc-path \"C:\\probe-engine-worker --client-window\"") == Role::Supervisor);
    // The program's own path is never read as an argument.
    CHECK(RoleOf(L"\"C:\\probe-engine-worker\\x.exe\" --client-window") == Role::Supervisor);
    CHECK(RoleOf(L"") == Role::Supervisor);

    const auto arguments = Arguments(worker);
    CHECK(arguments.size() == 9);
    CHECK(arguments.size() > 2 && arguments[1] == L"probe-engine-worker");
    CHECK(arguments.size() > 2 &&
          arguments[2] == L"\\\\?\\D:\\Some Library\\Halo The Master Chief Collection\\halo3\\halo3.dll");
    CHECK(Arguments(L"  a   \"b c\"  \"\" d").size() == 4);

    // Which of several client files is the newest.
    const Version v098 = VersionOf(L"project-reclaimer-v0.9.8.exe");
    const Version v0910 = VersionOf(L"project-reclaimer-v0.9.10.exe");
    const Version vNotRun = VersionOf(L"project-reclaimer-v0.9.99.exe");   // a client the mod has not been run with
    CHECK(Proven(VersionOf(L"project-reclaimer-v0.9.11.exe")));
    CHECK(Proven(VersionOf(L"project-reclaimer-v0.9.12.exe")));
    CHECK(!Proven(VersionOf(L"project-reclaimer-v0.9.13.exe")));
    const Version v1 = VersionOf(L"project-reclaimer-v1.exe");
    const Version none = VersionOf(L"project-reclaimer.exe");
    CHECK(v098.known && v098.part[0] == 0 && v098.part[1] == 9 && v098.part[2] == 8);
    CHECK(v0910.known && v0910.part[2] == 10);
    CHECK(v1.known && v1.part[0] == 1 && v1.part[1] == 0);
    CHECK(!none.known);
    CHECK(Newer(v0910, v098));
    CHECK(!Newer(v098, v0910));
    CHECK(Newer(v1, v0910));
    CHECK(Newer(v098, none));
    CHECK(!Newer(none, v098));
    CHECK(!Newer(v098, v098));
    CHECK(!Newer(none, none));

    // Which client the launcher starts: a version the mod has been run with before any other.
    {
        CHECK(Proven(v098));
        CHECK(Proven(v0910));
        CHECK(!Proven(vNotRun));
        CHECK(!Proven(v1));
        CHECK(!Proven(none));
        CHECK(!Proven(VersionOf(L"project-reclaimer-v0.9.exe")));
        CHECK(ChooseClient({}).chosen == -1 && ChooseClient({}).passedOver == -1);
        const std::vector<ClientCandidate> one{{L"a", vNotRun, 5}};
        CHECK(ChooseClient(one).chosen == 0 && ChooseClient(one).passedOver == -1);   // the only one, proven or not
        const std::vector<ClientCandidate> both{{L"new", vNotRun, 9}, {L"proven", v098, 1}, {L"newer", v1, 2}};
        const ClientChoice kept = ChooseClient(both);
        CHECK(kept.chosen == 1);                        // 0.9.8, though two newer clients sit beside it
        CHECK(kept.passedOver == 2);                    // ...and the newest of those is the one named in the log
        const std::vector<ClientCandidate> proven{{L"older", v098, 9}, {L"newer", v0910, 1}, {L"new", vNotRun, 5}};
        const ClientChoice higher = ChooseClient(proven);
        CHECK(higher.chosen == 1 && higher.passedOver == 2);    // of two proven clients, the higher version
        const Version v0923 = VersionOf(L"project-reclaimer-client-v0.9.23.exe");
        const std::vector<ClientCandidate> unproven{{L"a", v0923, 1}, {L"b", vNotRun, 1}, {L"c", none, 9}};
        const ClientChoice newest = ChooseClient(unproven);
        CHECK(newest.chosen == 1 && newest.passedOver == -1);   // none proven: the highest version
        const std::vector<ClientCandidate> copies{{L"old copy", v098, 1}, {L"new copy", v098, 7}};
        CHECK(ChooseClient(copies).chosen == 1 && ChooseClient(copies).passedOver == -1);   // the newer file
        const std::vector<ClientCandidate> unnamed{{L"x", none, 1}, {L"y", none, 3}};
        CHECK(ChooseClient(unnamed).chosen == 1);
    }

    // 0.10.0 and later run their own recompiled engine in the client itself: the mod goes into the client, never
    // into a server process it starts.
    {
        const Version v0100 = {{0, 10, 0}, true};   // what the file's version resource says after an in-place update
        CHECK(RunsRecompiledEngine(v0100));
        CHECK(RunsRecompiledEngine(VersionOf(L"project-reclaimer-client-v0.10.0.exe")));
        CHECK(RunsRecompiledEngine(VersionOf(L"project-reclaimer-client-v0.10.3.exe")));
        CHECK(RunsRecompiledEngine(VersionOf(L"project-reclaimer-client-v0.11.0.exe")));
        CHECK(RunsRecompiledEngine(v1));
        CHECK(!RunsRecompiledEngine(VersionOf(L"project-reclaimer-client-v0.9.23.exe")));
        CHECK(!RunsRecompiledEngine(vNotRun));       // 0.9.99
        CHECK(!RunsRecompiledEngine(v098));
        CHECK(!RunsRecompiledEngine(none));
        CHECK(!Proven(v0100));
        // Of several, the proven rule alone decides: 0.10.0 is a client like any other the mod has not been run with.
        const std::vector<ClientCandidate> updated{{L"0.10", v0100, 9}, {L"0.9.23", VersionOf(L"x-v0.9.23.exe"), 1}};
        CHECK(ChooseClient(updated).chosen == 0);

        const wchar_t* client = L"\"G:\\Games\\Project Reclaimer\\0.9.23\\project-reclaimer-client-v0.9.23.exe\"";
        CHECK(RoleOf(client) == Role::Supervisor);
        CHECK(TakesMod(RoleOf(client), true));        // 0.10: the client itself
        CHECK(!TakesMod(RoleOf(client), false));      // 0.9: never the supervisor
        const std::wstring server = std::wstring(client) + L" --server-worker --name \"Reclaimer Slayer\"";
        CHECK(RoleOf(server) == Role::OtherWorker && !TakesMod(RoleOf(server), true));
        CHECK(!TakesMod(RoleOf(std::wstring(client) + L" --headless"), true));
        const std::wstring worker = std::wstring(client) +
            L" probe-engine-worker \"D:\\MCC\\halo3\\halo3.dll\" --guardian-slayer --client-window --startup";
        CHECK(TakesMod(RoleOf(worker), false) && !TakesMod(RoleOf(worker), true));
        // "--headless" inside a quoted path is not the argument.
        CHECK(RoleOf(L"\"D:\\a --headless b\\project-reclaimer-client.exe\"") == Role::Supervisor);
    }

    // Where the settings are: 0.9 in Documents, 0.10 in the local app data folder it moved them to.
    CHECK(SettingsFolder(false, L"F:\\OneDrive\\Documents", L"C:\\Users\\p\\AppData\\Local") ==
          L"F:\\OneDrive\\Documents\\My Games\\Project Reclaimer");
    CHECK(SettingsFolder(true, L"F:\\OneDrive\\Documents", L"C:\\Users\\p\\AppData\\Local") ==
          L"C:\\Users\\p\\AppData\\Local\\ProjectReclaimer");
    CHECK(SettingsFolder(true, L"F:\\Docs", L"").empty() && SettingsFolder(false, L"", L"C:\\L").empty());

    // The version resource, read without version.dll: VS_FIXEDFILEINFO by its signature.
    {
        uint8_t resource[160]{};
        const uint32_t signature = 0xFEEF04BD, structVersion = 0x10000, ms = (0u << 16) | 10u, ls = (2u << 16) | 7u;
        std::memcpy(resource + 40, &signature, 4);
        std::memcpy(resource + 44, &structVersion, 4);
        std::memcpy(resource + 48, &ms, 4);
        std::memcpy(resource + 52, &ls, 4);
        const Version v = VersionFromVersionResource(resource, sizeof(resource));
        CHECK(v.known && v.part[0] == 0 && v.part[1] == 10 && v.part[2] == 2);
        CHECK(RunsRecompiledEngine(v));
        CHECK(!VersionFromVersionResource(resource, 60).known);    // cut short before the fixed part ends
        CHECK(!VersionFromVersionResource(nullptr, 0).known);
        uint8_t blank[160]{};
        std::memcpy(blank + 40, &signature, 4);
        CHECK(!VersionFromVersionResource(blank, sizeof(blank)).known);   // a zero version is no version
    }

    // The early-hooks event's name: the same for the relay and the mod, bounded by the buffer.
    wchar_t name[64]{};
    EarlyHooksEventName(name, 64, 68240);
    CHECK(std::wstring(name) == L"Local\\HaloMCCVR-EarlyHooks-68240");
    EarlyHooksEventName(name, 64, 0);
    CHECK(std::wstring(name) == L"Local\\HaloMCCVR-EarlyHooks-0");
    EarlyHooksEventName(name, 64, 4294967295ul);
    CHECK(std::wstring(name) == L"Local\\HaloMCCVR-EarlyHooks-4294967295");
    wchar_t tiny[8]{};
    EarlyHooksEventName(tiny, 8, 123);
    CHECK(std::wstring(tiny) == L"Local\\H");

    // A screen or an overlay: the talker's name in a match must not take the stereo picture away.
    {
        ScreenRule rule;
        ScreenRule::In in;
        in.nowMs = 1000;
        CHECK(!rule.Step(in));                          // the host draws nothing
        in.hostDraws = true;
        CHECK(rule.Step(in));                           // not in play: the main menu, a loading screen
        in.inPlay = true;
        CHECK(!rule.Step(in));                          // in play, no press: the talker's name, a chat line
        in.nowMs = 1500;
        CHECK(!rule.Step(in));

        // The player's Menu press opens a menu: flat from the first drawing after it, until the drawing stops.
        in.hostDraws = false;
        in.menuPressSerial = 1;
        in.nowMs = 2000;
        CHECK(!rule.Step(in));
        in.hostDraws = true;
        in.nowMs = 2400;
        CHECK(rule.Step(in));
        in.nowMs = 9000;
        CHECK(rule.Step(in));                           // it holds as long as the host draws
        in.hostDraws = false;
        CHECK(!rule.Step(in));
        in.hostDraws = true;
        CHECK(!rule.Step(in));                          // and a later overlay is an overlay again

        // A press that nothing followed in time opens nothing.
        in.hostDraws = false;
        in.menuPressSerial = 2;
        in.nowMs = 20000;
        CHECK(!rule.Step(in));
        in.hostDraws = true;
        in.nowMs = 20000 + ScreenRule::kMenuOpensMs + 1;
        CHECK(!rule.Step(in));

        // Pressed while a talker's name is up: the menu opens over it. Pressed again: the menu closes, and the
        // name that is still drawn does not keep the picture flat.
        in.menuPressSerial = 3;
        in.nowMs = 30000;
        CHECK(rule.Step(in));
        in.menuPressSerial = 4;
        in.nowMs = 34000;
        CHECK(!rule.Step(in));
        in.nowMs = 34100;
        CHECK(!rule.Step(in));
    }

    {
        // Measured over a frame of the game: a small drawing is an overlay in play or not, pressed or not;
        // anything more is a screen, in play too and with no press (a menu beside the play, opened by a key of
        // the host's own).
        ScreenRule rule;
        ScreenRule::In in;
        in.hostDraws = true;
        in.coverageKnown = true;
        in.gameFrame = true;
        in.nowMs = 1000;
        in.coveragePermille = 4;                        // a talker's name
        CHECK(!rule.Step(in));                          // ... while the player is dead (not in play)
        in.coveragePermille = 1000;                     // a menu over the whole frame
        CHECK(rule.Step(in));
        in.inPlay = true;
        in.coveragePermille = 142;                      // a panel beside the play: the Forge menu, no press at all
        CHECK(rule.Step(in));
        in.coveragePermille = 60;                       // between the two thresholds: it stays what it was
        CHECK(rule.Step(in));
        in.coveragePermille = 7;                        // the menu closed, a hint is still drawn
        CHECK(!rule.Step(in));
        in.coveragePermille = 60;                       // ...and stays an overlay until it reaches the upper one
        CHECK(!rule.Step(in));
        in.coveragePermille = 80;
        CHECK(rule.Step(in));
        in.hostDraws = false;                           // the drawing stops
        CHECK(!rule.Step(in));
        in.hostDraws = true;
        in.coveragePermille = 60;                       // a new drawing first seen between the thresholds: an overlay
        CHECK(!rule.Step(in));
        in.hostDraws = false;
        CHECK(!rule.Step(in));
        in.hostDraws = true;
        in.coveragePermille = 142;                      // a new drawing first seen as a menu
        CHECK(rule.Step(in));
        // A reading missed for a moment does not undo the last one; missed for long, the press rule is back.
        in.coverageKnown = false;
        in.nowMs = 1500;
        CHECK(rule.Step(in));
        in.nowMs = 1000 + ScreenRule::kMenuOpensMs + 1;
        CHECK(!rule.Step(in));                          // in play, no press: not a screen
        in.coverageKnown = true;
        CHECK(rule.Step(in));                           // read again: a menu
        in.coveragePermille = 5;                        // small now, then a reading missed: still an overlay
        in.nowMs = 9000;
        CHECK(!rule.Step(in));
        in.coverageKnown = false;
        in.inPlay = false;
        in.nowMs = 9500;
        CHECK(!rule.Step(in));                          // even out of play, while the last reading stands
        in.nowMs = 9000 + ScreenRule::kMenuOpensMs + 1;
        CHECK(rule.Step(in));                           // after it, out of play whatever the host draws is the picture
        // A press with only an overlay drawn: not a screen while measured, and forgotten after its time, so a
        // missing measurement later does not flatten the picture.
        ScreenRule pressed;
        ScreenRule::In p;
        p.hostDraws = p.inPlay = p.coverageKnown = p.gameFrame = true;
        p.coveragePermille = 5;
        p.menuPressSerial = 1;
        p.nowMs = 5000;
        CHECK(!pressed.Step(p));
        p.nowMs = 5000 + ScreenRule::kMenuOpensMs + 1;
        CHECK(!pressed.Step(p));
        p.coverageKnown = false;
        p.nowMs = 5000 + 3 * ScreenRule::kMenuOpensMs;
        CHECK(!pressed.Step(p));
        // Unmeasured, the press rule stands as before.
        p.menuPressSerial = 2;
        p.nowMs = 20000;
        CHECK(pressed.Step(p));
    }

    {
        // A press with a menu up: a step back inside it, or its close. Measured, the drawing's size says which.
        ScreenRule rule;
        ScreenRule::In in;
        in.hostDraws = in.inPlay = in.coverageKnown = in.gameFrame = true;
        in.coveragePermille = 5;                        // a name is up
        in.nowMs = 1000;
        CHECK(!rule.Step(in));
        in.menuPressSerial = 1;                         // the press opens a menu
        in.nowMs = 2000;
        CHECK(!rule.Step(in));                          // not drawn yet: still only the name
        in.coveragePermille = 300;
        in.nowMs = 2200;
        CHECK(rule.Step(in));
        in.menuPressSerial = 2;                         // Esc: back out of a settings page
        in.nowMs = 5000;
        CHECK(rule.Step(in));                           // the menu is still drawn: still the picture
        in.coverageKnown = false;                       // a reading missed for long after that press
        in.nowMs = 5000 + ScreenRule::kMenuOpensMs + 1;
        CHECK(rule.Step(in));                           // ...is not a close: the menu was seen up after it
        in.coverageKnown = true;
        in.menuPressSerial = 3;                         // Esc again: this time the menu closes
        in.nowMs = 9000;
        CHECK(rule.Step(in));                           // its last frames
        in.coveragePermille = 12;                       // gone, the name is still drawn
        in.nowMs = 9100;
        CHECK(!rule.Step(in));
        in.coverageKnown = false;                       // and a missing reading later does not bring it back
        in.nowMs = 9100 + ScreenRule::kMenuOpensMs + 1;
        CHECK(!rule.Step(in));

        // Unmeasured, the same press is the close (the earlier rule).
        ScreenRule blind;
        ScreenRule::In b;
        b.hostDraws = b.inPlay = true;
        b.menuPressSerial = 1;
        b.nowMs = 1000;
        CHECK(blind.Step(b));
        b.menuPressSerial = 2;
        b.nowMs = 3000;
        CHECK(!blind.Step(b));
    }

    {
        // With no frame of the game under it the measurement counts nothing useful (the host redraws its own last
        // frame): a host screen that reads as "nothing changed" is still the picture.
        ScreenRule rule;
        ScreenRule::In in;
        in.hostDraws = true;
        in.coverageKnown = true;
        in.coveragePermille = 0;                        // a loading screen redrawn over itself
        in.nowMs = 1000;
        CHECK(rule.Step(in));                           // not in play, no game frame: a screen
        in.coveragePermille = 12;                       // ...with something moving on it
        CHECK(rule.Step(in));
        in.gameFrame = true;                            // the level's first frames arrive under it
        CHECK(!rule.Step(in));                          // now the same reading is an overlay
        in.gameFrame = false;
        CHECK(rule.Step(in));
        in.inPlay = true;                               // in play the press rule decides, as unmeasured
        CHECK(!rule.Step(in));
        in.menuPressSerial = 1;
        in.nowMs = 2000;
        CHECK(rule.Step(in));
        in.coveragePermille = 990;                      // a reading that would make it a screen is not consulted either
        in.menuPressSerial = 2;
        in.nowMs = 3000;
        CHECK(!rule.Step(in));
    }

    {
        // The panel: which frames get one, the size of its image, and how its picture blends.
        CHECK(PanelFrameUsable(2560, 1440, 1));
        CHECK(PanelFrameUsable(3840, 2160, 1));
        CHECK(!PanelFrameUsable(2560, 1440, 4));           // a multisampled frame
        CHECK(!PanelFrameUsable(320, 200, 1));
        CHECK(!PanelFrameUsable(0, 0, 1));
        uint32_t w = 0, h = 0;
        PanelImageSize(2560, 1440, w, h);
        CHECK(w == 2560 && h == 1440);                      // as it is
        PanelImageSize(3840, 2160, w, h);
        CHECK(w == 1920 && h == 1080);                      // halved once
        PanelImageSize(7680, 4320, w, h);
        CHECK(w == 1920 && h == 1080);
        PanelImageSize(1280, 720, w, h);
        CHECK(w == 1280 && h == 720);
        CHECK(PanelAlphaFor(1, 0, true, 1000) == PanelAlpha::Premultiplied);   // a blended full screen
        CHECK(PanelAlphaFor(1, 1, true, 1000) == PanelAlpha::Opaque);          // an unblended one that fills the frame
        CHECK(PanelAlphaFor(1, 1, true, 140) == PanelAlpha::Premultiplied);    // an unblended widget: not a black frame
        CHECK(PanelAlphaFor(1, 1, false, 0) == PanelAlpha::Premultiplied);     // not measured
        CHECK(PanelAlphaFor(3, 2, true, 1000) == PanelAlpha::Premultiplied);   // one blended draw among them
        CHECK(PanelAlphaFor(0, 0, true, 1000) == PanelAlpha::Premultiplied);
        CHECK(ScreenShownAsPanel(true, true, true));
        CHECK(!ScreenShownAsPanel(true, false, true));      // the host's own menu: no frame of the game under it
        CHECK(!ScreenShownAsPanel(true, true, false));      // no panel: the flat picture, as before
        CHECK(!ScreenShownAsPanel(false, true, true));
    }

    {
        // Changed texels: rows with padding, 4 bytes a texel.
        const uint32_t w = 8, h = 4, bpt = 4;
        const size_t pitch = w * bpt + 16;
        std::vector<uint8_t> a(pitch * h, 7), b(pitch * h, 7);
        CHECK(ChangedTexels(a.data(), pitch, b.data(), pitch, w, h, bpt) == 0);
        b[pitch * 1 + 2 * bpt + 3] = 8;                 // one texel, one byte
        b[pitch * 3 + 7 * bpt + 0] = 0;
        b[pitch * 2 + w * bpt + 5] = 99;                // padding: not a texel
        CHECK(ChangedTexels(a.data(), pitch, b.data(), pitch, w, h, bpt) == 2);
        CHECK(ChangedTexels(a.data(), w * bpt - 1, b.data(), pitch, w, h, bpt) == 0);
        CHECK(ChangedTexels(nullptr, pitch, b.data(), pitch, w, h, bpt) == 0);
    }
    {
        // The size Reclaimer's Native render resolution is answered with: the Picture panel's, fitted to its range.
        unsigned w = 2912, h = 2100;
        CHECK(FitNativeRenderSize(w, h) && w == 2912 && h == 2100);
        w = 7688; h = 5544;                             // Resolution scale 2.64 ("Keith David")
        CHECK(FitNativeRenderSize(w, h) && h <= kNativeRenderMaxHeight && w <= kNativeRenderMaxWidth &&
              w % 2 == 0 && h % 2 == 0 && std::fabs(double(w) / h - 7688.0 / 5544.0) < 0.002);
        w = 9000; h = 1000;                             // too wide: the width is the limit
        CHECK(FitNativeRenderSize(w, h) && w == kNativeRenderMaxWidth && h == 852);
        w = 1456; h = 1050;                             // Resolution scale 0.5
        CHECK(FitNativeRenderSize(w, h) && w == 1456 && h == 1050);
        w = 600; h = 400;                               // smaller than Reclaimer accepts: left alone
        CHECK(!FitNativeRenderSize(w, h) && w == 600 && h == 400);
        w = 0; h = 1050;
        CHECK(!FitNativeRenderSize(w, h));
        w = 20000; h = 100;                             // no size in range has that shape
        CHECK(!FitNativeRenderSize(w, h));
    }
    {
        // The client's display mode as its settings state it.
        CHECK(ReadClientDisplayMode("{\"display_mode\": \"windowed\"}") == ClientDisplayMode::Windowed);
        CHECK(ReadClientDisplayMode("{\n  \"display_mode\" : \"borderless\"\n}") == ClientDisplayMode::Borderless);
        CHECK(ReadClientDisplayMode("{\"display_mode\":\"exclusive_fullscreen\"}") ==
              ClientDisplayMode::ExclusiveFullscreen);
        CHECK(ReadClientDisplayMode("{\"fov\": 70}") == ClientDisplayMode::Borderless);
        CHECK(ReadClientDisplayMode("") == ClientDisplayMode::Borderless);
        CHECK(ReadClientDisplayMode("{\"display_mode\": 2}") == ClientDisplayMode::Unknown);
        CHECK(ReadClientDisplayMode("{\"display_mode\": \"fullscreen\"}") == ClientDisplayMode::Unknown);
    }
    {
        // What Native is answered: never a size larger than the display, and the real mode in exclusive fullscreen.
        unsigned w = 0, h = 0;
        CHECK(AnswerNativeRenderSize(2912, 2100, 2560, 1440, ClientDisplayMode::Borderless, true, w, h) ==
                  NativeAnswer::PanelWithinDisplay &&
              w == 1996 && h == 1440);
        CHECK(AnswerNativeRenderSize(2912, 2100, 3840, 2160, ClientDisplayMode::Borderless, true, w, h) ==
                  NativeAnswer::Panel &&
              w == 2912 && h == 2100);
        CHECK(AnswerNativeRenderSize(2184, 1576, 3840, 2160, ClientDisplayMode::Windowed, true, w, h) ==
                  NativeAnswer::Panel &&
              w == 2184 && h == 1576);
        CHECK(AnswerNativeRenderSize(2912, 2100, 2560, 1440, ClientDisplayMode::ExclusiveFullscreen, true, w, h) ==
                  NativeAnswer::Display &&
              w == 2560 && h == 1440);
        CHECK(AnswerNativeRenderSize(2912, 2100, 2560, 1440, ClientDisplayMode::Unknown, true, w, h) ==
                  NativeAnswer::PanelWithinDisplay &&
              w <= 2560 && h <= 1440);
        CHECK(AnswerNativeRenderSize(4000, 1000, 2560, 1440, ClientDisplayMode::Borderless, true, w, h) ==
                  NativeAnswer::PanelWithinDisplay &&
              w == 2560 && h == 640);                   // too wide: the width is the limit
        CHECK(AnswerNativeRenderSize(2912, 2100, 640, 480, ClientDisplayMode::Borderless, true, w, h) ==
                  NativeAnswer::Display &&
              w == 640 && h == 480);                    // scaled down it would be under 480 high
        CHECK(AnswerNativeRenderSize(0, 2100, 2560, 1440, ClientDisplayMode::Borderless, true, w, h) ==
                  NativeAnswer::Display &&
              w == 2560 && h == 1440);
        CHECK(AnswerNativeRenderSize(2912, 2100, 0, 0, ClientDisplayMode::Borderless, true, w, h) == NativeAnswer::Display);
        // Fitting turned off: the Picture panel's full size, larger than the display; exclusive still the real size.
        CHECK(AnswerNativeRenderSize(2912, 2100, 2560, 1440, ClientDisplayMode::Borderless, false, w, h) ==
                  NativeAnswer::Panel &&
              w == 2912 && h == 2100);
        CHECK(AnswerNativeRenderSize(2912, 2100, 2560, 1440, ClientDisplayMode::ExclusiveFullscreen, false, w, h) ==
                  NativeAnswer::Display &&
              w == 2560 && h == 1440);
    }

    if (g_failed)
        std::printf("%d check(s) failed\n", g_failed);
    else
        std::printf("reclaimer host logic: all checks passed\n");
    return g_failed ? 1 : 0;
}
