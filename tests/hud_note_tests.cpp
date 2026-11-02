// A note in the game's own HUD messages (src/common/hud_note_logic.h): which way it goes, the text the HUD's font
// can carry, and when it is posted or handed to the mod's panel.
#include <cstdio>
#include <cstdlib>
#include <string>
#include "../src/common/hud_note_logic.h"

using namespace hud_note;

static unsigned g_checks = 0;
static void Check(bool condition, const char* message)
{
    ++g_checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static void TestChoose()
{
    Check(Choose(0, true, true) == Route::Off, "0 is no note");
    Check(Choose(1, true, true) == Route::GameHud, "1 is the game's HUD when it is bound and the session allows");
    Check(Choose(1, false, true) == Route::Panel, "not bound: the panel");
    Check(Choose(1, true, false) == Route::Panel, "a shared co-op simulation: the panel");
    Check(Choose(2, true, true) == Route::Panel, "2 is always the panel");
    Check(Choose(7, true, true) == Route::GameHud, "an unknown value reads as 1");
    Check(SessionAllows(true, false, -1), "the co-op switch open allows it anywhere");
    Check(!SessionAllows(false, false, 3), "MCC with the switch closed: the panel, whatever the type");
    for (const int type : {1, 3, 4, 5})
        Check(SessionAllows(false, true, type), "Project Reclaimer: not known to be shared, the game's HUD");
    Check(!SessionAllows(false, true, 2), "Project Reclaimer, a synchronous client: the panel");
    Check(!SessionAllows(false, true, -1), "Project Reclaimer, nothing read: the panel");
    Check(!SessionAllows(false, true, 0), "Project Reclaimer, no simulation: the panel");
}

static void TestText()
{
    Check(ForGameHud(L"Mongoose \u00B7 Over the Shoulder, Untethered \u00B7 Motion") ==
              L"Mongoose - Over the Shoulder, Untethered - Motion",
          "the separator becomes a dash");
    Check(ForGameHud(L"Caf\u00E9 \u2014 cam\ttab") == L"Caf? ? cam?tab", "anything outside printable ASCII is '?'");
    Check(ForGameHud(L"  Ghost \u00B7 Follow  ") == L"Ghost - Follow", "no spaces at either end");
    Check(ForGameHud(L"").empty(), "nothing stays nothing");

    std::wstring longNote(200, L'a');
    const std::wstring cut = ForGameHud(longNote);
    Check(cut.size() == kMaxChars && cut.substr(cut.size() - 3) == L"...", "a long note ends in ... at the limit");
    std::wstring spaced(kMaxChars - 4, L'b');
    spaced += L"      and more words";
    const std::wstring spacedCut = ForGameHud(spaced);
    Check(spacedCut.size() <= kMaxChars && spacedCut.find(L" ...") == std::wstring::npos,
          "a cut does not leave a space before the dots");
    Check(ForGameHud(std::wstring(kMaxChars, L'c')).size() == kMaxChars, "exactly the limit is kept whole");
}

static void TestPacing()
{
    Check(!Due(3, 3, 10000), "a shown note is not posted again");
    Check(!Due(4, 3, kSettleMs - 1), "a fresh ask waits for the taps to settle");
    Check(Due(4, 3, kSettleMs), "then it is posted");
    Check(!Overdue(4, 3, kFallbackMs - 1), "the panel waits for the game first");
    Check(Overdue(4, 3, kFallbackMs), "then takes the note");
    Check(!Overdue(4, 4, kFallbackMs * 10), "a note the game showed never reaches the panel too");
    Check(kSettleMs < kFallbackMs, "the game has its turn before the panel");
}

int main()
{
    TestChoose();
    TestText();
    TestPacing();
    std::printf("PASS: %u HUD note checks\n", g_checks);
    return 0;
}
