// The player's lag tuning laid over the camera file (src/common/vehicle_camera_tuning.h): set, kept, written,
// read back, held to the file's bounds, and cleared.
#include <cstdio>
#include <cstdlib>
#include <string>
#include "../src/common/vehicle_camera_tuning.h"

using namespace vehicle_camera_tuning;

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

static const std::string kKey = "halo3\tWarthog\tThird Person";

static void TestSetAndFind()
{
    Overlay overlay;
    Tuning found;
    Check(!overlay.Find(kKey, found) && !overlay.Dirty(), "nothing is tuned to start with");
    overlay.Set(kKey, Tuning{0.5f, 0.8f});
    Check(overlay.Find(kKey, found) && found.lag == 0.5f && found.damping == 0.8f, "a tuning is kept by its camera");
    Check(overlay.Dirty(), "and is to be saved");
    overlay.Saved();
    overlay.Set(kKey, Tuning{0.5f, 0.8f});
    Check(!overlay.Dirty(), "the same values again are nothing to save");
    overlay.Set(kKey, Tuning{0.6f, 0.8f});
    Check(overlay.Dirty(), "a change is");
    Check(!overlay.Find("halo3\tWarthog\tSeat", found), "another camera of the same entry is not tuned");
    overlay.Set("", Tuning{1.0f, 1.0f});
    Check(overlay.Size() == 1, "a camera with no key is not kept");
}

static void TestBounds()
{
    Overlay overlay;
    Tuning found;
    overlay.Set(kKey, Tuning{-1.0f, 0.0f});
    Check(overlay.Find(kKey, found) && found.lag == 0.0f && found.damping == vehicle_camera_file::kMinLagDamping,
          "a lag below 0 is none, a damping below the least is the least");
    overlay.Set(kKey, Tuning{99.0f, 9.0f});
    Check(overlay.Find(kKey, found) && found.lag == vehicle_camera_file::kMaxLagSeconds &&
              found.damping == vehicle_camera_file::kMaxLagDamping,
          "and the most the camera file allows is the most");
}

static void TestFile()
{
    Overlay overlay;
    overlay.Set(kKey, Tuning{0.45f, 0.7f});
    overlay.Set("halo3\tcustom_1a2b3c4d\tOver the Shoulder", Tuning{0.2f, 1.0f});
    const std::string text = overlay.Text();
    Check(text.rfind("# ", 0) == 0, "the file opens with a note on what it is");

    Overlay back;
    Check(back.Load(text) == 2 && !back.Dirty(), "both lines read back, nothing to save");
    Tuning found;
    Check(back.Find(kKey, found) && found.lag == 0.45f && found.damping == 0.7f, "with their values");
    Check(back.Text() == text, "and written again they are the same text");

    Overlay odd;
    const int read = odd.Load("halo3\tWarthog\tThird Person\t0.3\t1\r\n"
                              "not a line\n"
                              "halo3\tWarthog\t0.3\t1\n"
                              "halo3\tGhost\tFollow\tfast\t1\n"
                              "# halo3\tGhost\tFollow\t0.3\t1\n"
                              "halo3\tGhost\tFollow\t0.25\t0.9");
    Check(read == 2 && odd.Find("halo3\tGhost\tFollow", found) && found.lag == 0.25f,
          "a line that does not read is skipped, a comment too, and a last line needs no newline");
}

static void TestClear()
{
    Overlay overlay;
    overlay.Set(kKey, Tuning{0.4f, 1.0f});
    overlay.Saved();
    Tuning found;
    Check(overlay.Clear(kKey) && !overlay.Find(kKey, found) && overlay.Dirty(),
          "cleared, the camera file's own applies again, and the file is to be saved");
    overlay.Saved();
    Check(!overlay.Clear(kKey) && !overlay.Dirty(), "clearing a camera with no tuning changes nothing");
}

int main()
{
    TestSetAndFind();
    TestBounds();
    TestFile();
    TestClear();
    std::printf("PASS: %u vehicle camera tuning checks\n", g_checks);
    return 0;
}
