// The synthetic gamepad's command language (src/common/dev_pad_script.h): what it accepts, and that one bad
// line refuses the whole script so a typo can never leave a button held.
#include "../src/common/dev_pad_script.h"
#include <cstdio>

namespace
{
unsigned checks{}, failures{};
void Check(bool ok, const char* what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}
} // namespace

int main()
{
    using namespace dev_pad;
    std::vector<Step> steps;
    std::string error;

    Check(Parse("press up 200\nwait 800\n# a comment\n\nhold a+rb\nrelease rb\nrelease all\n"
                "stick left 0 1\nstick R -0.5 0.25\ntrigger right 1\nclear\nseq 7\n", steps, error),
          "a full script parses");
    Check(steps.size() == 10, "every command line is a step, comments and blanks are not");
    if (steps.size() == 10)
    {
        Check(steps[0].op == Op::Press && steps[0].buttons == kUp && steps[0].ms == 200, "press up 200");
        Check(steps[1].op == Op::Wait && steps[1].ms == 800, "wait 800");
        Check(steps[2].op == Op::Hold && steps[2].buttons == (kA | kRightShoulder), "hold a+rb");
        Check(steps[3].op == Op::Release && steps[3].buttons == kRightShoulder, "release rb");
        Check(steps[4].op == Op::ReleaseAll, "release all");
        Check(steps[5].op == Op::Stick && steps[5].side == 0 && steps[5].x == 0.0f && steps[5].y == 1.0f,
              "stick left 0 1");
        Check(steps[6].op == Op::Stick && steps[6].side == 1 && steps[6].x == -0.5f && steps[6].y == 0.25f,
              "stick R -0.5 0.25 (case-insensitive side)");
        Check(steps[7].op == Op::Trigger && steps[7].side == 1 && steps[7].x == 1.0f, "trigger right 1");
        Check(steps[8].op == Op::Clear, "clear");
        Check(steps[9].op == Op::Seq && steps[9].seq == 7, "seq 7");
    }

    Check(Parse("PRESS X 100\r\nWait 5\r\n", steps, error) && steps.size() == 2 && steps[0].buttons == kX,
          "upper case and CRLF line ends parse");
    Check(Parse("set converged_aim 0\nset aim_direct_drive 1\n", steps, error) && steps.size() == 2 &&
              steps[0].op == Op::Set && steps[0].name == "converged_aim" && steps[0].value == 0 &&
              steps[1].name == "aim_direct_drive" && steps[1].value == 1,
          "set <setting> <int> parses");
    Check(Parse("set aim_lead_ticks_x100 150\n", steps, error) && steps.size() == 1 &&
              steps[0].name == "aim_lead_ticks_x100" && steps[0].value == 150,
          "a setting's name may hold digits after its first character");

    const char* bad[] = {
        "press up",            // no duration
        "press up 70000",      // longer than a minute
        "press up -5",         // negative
        "hold",                // no buttons
        "hold a++b",           // an empty button name
        "hold a+q",            // an unknown button
        "stick left 2 0",      // out of range
        "stick middle 0 0",    // no such stick
        "trigger left 1.5",    // out of range
        "wait 1.5",            // not a whole number of ms
        "jump",                // no such command
        "seq",                 // no number
        "clear now",           // extra words
        "set converged_aim",   // no value
        "set Converged-Aim 1", // not a setting name (after lower-casing, the dash)
        "set converged_aim x", // not a number
        "set 100x 1",          // a setting's name does not start with a digit
    };
    for (const char* line : bad)
    {
        const std::string script = std::string("press a 100\n") + line + "\n";
        const bool ok = Parse(script, steps, error);
        Check(!ok && steps.empty() && !error.empty(), line);
    }
    Check(!Parse("hold b\nhold q\n", steps, error) && error.find("line 2") != std::string::npos,
          "the error names the bad line");

    Check(StickRaw(1.0f) == 32767 && StickRaw(-1.0f) == -32767 && StickRaw(0.0f) == 0, "stick scale");
    Check(StickRaw(3.0f) == 32767, "stick clamps");
    Check(TriggerRaw(1.0f) == 255 && TriggerRaw(0.0f) == 0 && TriggerRaw(-1.0f) == 0, "trigger scale");

    std::printf("Synthetic pad script: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
