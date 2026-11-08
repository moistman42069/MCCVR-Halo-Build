// Halo 4: objective markers follow the head, not the gun (src/common/halo4_nav_marker_logic.h). The decoding of
// the two sites from their byte patterns and every way it must refuse; the run-time proof that the camera the HUD
// places markers with is the aim; the verdicts; and, on a simple pinhole model of a camera, the report itself: a
// marker placed from the gun is on screen only while the gun points at it, one placed from the head while the
// head looks at it. A model, not the game.
#include "halo4_nav_marker_logic.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

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

using namespace halo4_nav_markers;
using head_relative::kDegToRad;

std::vector<uint8_t> PatternBytes(const char* pattern)
{
    std::vector<uint8_t> bytes;
    for (const char* at = pattern; *at;)
    {
        if (*at == ' ')
        {
            ++at;
            continue;
        }
        if (at[0] == '?')
            bytes.push_back(0xCC);
        else
            bytes.push_back(static_cast<uint8_t>(std::stoul(std::string(at, 2), nullptr, 16)));
        at += 2;
    }
    return bytes;
}
void PutI32(std::vector<uint8_t>& bytes, size_t at, int32_t value) { std::memcpy(bytes.data() + at, &value, 4); }

// Retail halo4.dll (offsets from the module base).
constexpr int64_t kProjectorRva = 0x40B5F4, kBuildRva = 0x40B0B8, kBuilderRva = 0x38F658, kPointRva = 0x40B180;

std::vector<uint8_t> RetailProjector()
{
    std::vector<uint8_t> projector = PatternBytes(kProjectorPattern);
    PutI32(projector, kProjectorPointCall + 1,
           static_cast<int32_t>(kPointRva - (kProjectorRva + static_cast<int64_t>(kProjectorPointCall) + 5)));
    return projector;
}

std::vector<uint8_t> RetailBuild()
{
    std::vector<uint8_t> build = PatternBytes(kCameraBuildPattern);
    PutI32(build, kCameraBuildProjectionCall + 1,
           static_cast<int32_t>(kBuilderRva - (kBuildRva + static_cast<int64_t>(kCameraBuildProjectionCall) + 5)));
    PutI32(build, kCameraBuildConverterCall + 1, 0x12345);
    return build;
}
auto IsBuilder = [](int64_t rva) { return rva == kBuilderRva || rva == kPointRva; };

void Decoding()
{
    Check(PatternBytes(kProjectorPattern).size() == kProjectorBytes && kProjectorPointCall + 5 == kProjectorBytes,
          "the projector's pattern ends with its call of the point projection");
    Check(PatternBytes(kCameraBuildPattern).size() == kCameraBuildBytes, "the camera build is its whole function");
    {
        const std::vector<uint8_t> build = PatternBytes(kCameraBuildPattern);
        Check(build[kCameraBuildConverterCall] == 0xE8 && build[kCameraBuildProjectionCall] == 0xE8,
              "the converter's and the projection builder's calls are where the decode reads them");
        // The observer's place, literal in the build: engine TLS +0x680, 0x428 per user, +0x15C.
        const uint8_t tls[]{0xB8, 0x80, 0x06, 0x00, 0x00}, stride[]{0x4D, 0x69, 0xCE, 0x28, 0x04, 0x00, 0x00},
                      start[]{0x49, 0x81, 0xC1, 0x5C, 0x01, 0x00, 0x00};
        auto has = [&build](const uint8_t* what, size_t n) {
            for (size_t i = 0; i + n <= build.size(); ++i)
                if (std::memcmp(build.data() + i, what, n) == 0)
                    return true;
            return false;
        };
        Check(has(tls, sizeof(tls)) && has(stride, sizeof(stride)) && has(start, sizeof(start)),
              "the build reads the user's observer camera from engine TLS +0x680, 0x428 apart, +0x15C");
    }
    const std::vector<uint8_t> projector = RetailProjector();
    const std::vector<uint8_t> build = RetailBuild();
    Sites sites;
    Check(DecodeSites(projector.data(), kProjectorRva, true, build.data(), kBuildRva, true, IsBuilder, sites) ==
                  Bind::Bound &&
              sites.projectorRva == kProjectorRva && sites.projectionRva == kBuilderRva && sites.pointRva == kPointRva,
          "retail decodes: the projector at +0x40B5F4, the projection builder at +0x38F658, the point projection at "
          "+0x40B180");
    {
        std::vector<uint8_t> wrong = projector;
        PutI32(wrong, kProjectorPointCall + 1, 0x777);
        Sites none;
        Check(DecodeSites(wrong.data(), kProjectorRva, true, build.data(), kBuildRva, true, IsBuilder, none) ==
                      Bind::PointNotFunction && none.pointRva == 0,
              "a point projection that does not begin a function binds nothing");
    }
    Sites untouched;
    Check(DecodeSites(nullptr, kProjectorRva, true, build.data(), kBuildRva, true, IsBuilder, untouched) ==
                  Bind::ProjectorMissing &&
              DecodeSites(projector.data(), kProjectorRva, false, build.data(), kBuildRva, true, IsBuilder,
                          untouched) == Bind::ProjectorMissing &&
              DecodeSites(projector.data(), 0, true, build.data(), kBuildRva, true, IsBuilder, untouched) ==
                  Bind::ProjectorMissing,
          "a missing, ambiguous or mid-function projector binds nothing");
    Check(DecodeSites(projector.data(), kProjectorRva, true, nullptr, kBuildRva, true, IsBuilder, untouched) ==
                  Bind::CameraBuildMissing &&
              DecodeSites(projector.data(), kProjectorRva, true, build.data(), kBuildRva, false, IsBuilder,
                          untouched) == Bind::CameraBuildMissing,
          "a missing, ambiguous or mid-function camera build binds nothing");
    {
        std::vector<uint8_t> wrong = projector;
        wrong[kProjectorPointCall] = 0xE9;
        Check(DecodeSites(wrong.data(), kProjectorRva, true, build.data(), kBuildRva, true, IsBuilder, untouched) ==
                  Bind::NotACall,
              "a projector that jumps instead of calling binds nothing");
        std::vector<uint8_t> jump = build;
        jump[kCameraBuildProjectionCall] = 0xE9;
        Check(DecodeSites(projector.data(), kProjectorRva, true, jump.data(), kBuildRva, true, IsBuilder,
                          untouched) == Bind::NotACall,
              "a camera build whose last call is a jump binds nothing");
    }
    for (int32_t off : {-5, -1, 1, 0x10})
    {
        std::vector<uint8_t> moved = build;
        PutI32(moved, kCameraBuildProjectionCall + 1,
               static_cast<int32_t>(kBuilderRva + off -
                                    (kBuildRva + static_cast<int64_t>(kCameraBuildProjectionCall) + 5)));
        Check(DecodeSites(projector.data(), kProjectorRva, true, moved.data(), kBuildRva, true, IsBuilder,
                          untouched) == Bind::BuilderNotFunction,
              "a projection builder that does not begin a function binds nothing");
    }
    Check(untouched.projectorRva == 0 && untouched.projectionRva == 0, "a refusal fills nothing");
    for (int why = 0; why < static_cast<int>(Bind::Count); ++why)
        Check(BindName(static_cast<Bind>(why))[0] != '?', "every refusal has a name for the log");
}

void Forward(float yawDeg, float pitchDeg, float out[3])
{
    const float y = yawDeg * kDegToRad, p = pitchDeg * kDegToRad;
    out[0] = std::cos(p) * std::cos(y);
    out[1] = std::cos(p) * std::sin(y);
    out[2] = std::sin(p);
}

void Proof()
{
    float camera[3], aim[3];
    // A constant camera never proves, however long it agrees.
    {
        MarkerProof proof;
        Forward(40.0f, 0.0f, camera);
        for (int i = 0; i < 200; ++i)
            (void)StepProof(proof, camera, true, camera);
        Check(!proof.proven, "a camera that agrees but never moves is not proven");
    }
    // The camera following the aim across more than the spread proves it at kProofSamples.
    MarkerProof proof;
    ProofStep last = ProofStep::Count;
    uint32_t calls = 0;
    for (int i = 0; i < 100 && !proof.proven; ++i, ++calls)
    {
        Forward(10.0f + i * 0.5f, 5.0f, aim);
        Forward(10.0f + i * 0.5f + 1.0f, 5.0f, camera);  // a degree of lag: inside the tolerance
        last = StepProof(proof, camera, true, aim);
    }
    Check(proof.proven && last == ProofStep::Proven && calls >= kProofSamples,
          "a camera that follows the aim across the spread is proven");
    // No reference: neither for nor against.
    Check(StepProof(proof, camera, false, aim) == ProofStep::NoReference && proof.proven,
          "with no reference nothing changes");
    {
        const float vertical[3]{0.0f, 0.0f, 1.0f};
        Check(StepProof(proof, vertical, true, aim) == ProofStep::NoReference,
              "a camera looking straight up has no heading to compare");
    }
    // Lagging counts for nothing; disagreement withdraws only after a long run.
    Forward(10.0f, 5.0f, aim);
    Forward(20.0f, 5.0f, camera);
    Check(StepProof(proof, camera, true, aim) == ProofStep::Lagging && proof.proven,
          "a miss between the tolerance and the disagreement bound is lag");
    Forward(80.0f, 5.0f, camera);
    ProofStep step = ProofStep::Count;
    for (uint32_t i = 0; i < kProofWithdrawRun; ++i)
        step = StepProof(proof, camera, true, aim);
    Check(step == ProofStep::Withdrawn && !proof.proven, "a long run of disagreement withdraws the proof");
    // Before it is proven, one disagreement starts it over.
    MarkerProof fresh;
    Forward(0.0f, 0.0f, aim);
    (void)StepProof(fresh, aim, true, aim);
    Forward(90.0f, 0.0f, camera);
    Check(StepProof(fresh, camera, true, aim) == ProofStep::Disagreed && fresh.agreed == 0,
          "an unproven proof starts over on a disagreement");
    // Headings wrap.
    MarkerProof wrap;
    Forward(179.0f, 0.0f, aim);
    Forward(-179.5f, 0.0f, camera);
    Check(StepProof(wrap, camera, true, aim) == ProofStep::Agreed, "headings either side of 180 degrees agree");
    for (int s = 0; s < static_cast<int>(ProofStep::Count); ++s)
        Check(ProofStepName(static_cast<ProofStep>(s))[0] != '?', "every proof step has a name");
}

void Verdicts()
{
    const uint32_t generation = 3;
    const uint64_t now = 5'000'000;
    head_relative::HeadSample sample;
    sample.valid = true;
    sample.generation = generation;
    sample.stampUs = now - 10'000;
    Forward(30.0f, 0.0f, sample.forward);
    sample.up[2] = 1.0f;
    const float position[3]{1.0f, 2.0f, 3.0f};
    Check(HeadUse(true, false, true, true, sample, position, generation, now) == Use::Used,
          "a live, fresh head outside the eye passes is used");
    Check(HeadUse(true, true, true, true, sample, position, generation, now) == Use::InEyePass,
          "inside an eye's render the camera is that eye's: nothing changes");
    Check(HeadUse(false, false, true, true, sample, position, generation, now) == Use::SwitchOff, "switch off");
    Check(HeadUse(true, false, false, true, sample, position, generation, now) == Use::NotGameplay &&
              HeadUse(true, false, true, false, sample, position, generation, now) == Use::NotGameplay,
          "no coherent read, or a cinematic: the game's placement");
    head_relative::HeadSample stale = sample;
    stale.stampUs = now - head_relative::kMaxSampleAgeUs - 1;
    Check(HeadUse(true, false, true, true, stale, position, generation, now) == Use::Stale, "a stale head");
    head_relative::HeadSample other = sample;
    other.generation = generation + 1;
    Check(HeadUse(true, false, true, true, other, position, generation, now) == Use::OtherGeneration,
          "a head of another load");
    Check(HeadUse(true, false, true, true, sample, position, 0, now) == Use::OtherGeneration,
          "a feature bound to another generation uses no head");
    head_relative::HeadSample dead = sample;
    dead.valid = false;
    Check(HeadUse(true, false, true, true, dead, position, generation, now) == Use::NoSample, "no live head");
    const float nowhere[3]{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
    Check(HeadUse(true, false, true, true, sample, nowhere, generation, now) == Use::NoHead, "no finite position");
    for (int u = 0; u < static_cast<int>(Use::Count); ++u)
        Check(UseName(static_cast<Use>(u))[0] != '?', "every verdict has a name for the log");

    // The copy handed to the game: the pose always, the field of view only when it is one.
    uint8_t camera[kCameraBytes];
    std::memset(camera, 0xCD, sizeof(camera));
    const float gameFov = 1.0f;
    std::memcpy(camera + kCameraVerticalFov, &gameFov, sizeof(gameFov));
    const float up[3]{0.0f, 0.0f, 1.0f};
    WriteHeadCamera(camera, position, sample.forward, up, 0.0f);
    float fov = 0.0f, got[3]{};
    std::memcpy(&fov, camera + kCameraVerticalFov, sizeof(fov));
    std::memcpy(got, camera + kCameraForward, sizeof(got));
    Check(fov == gameFov && std::memcmp(got, sample.forward, sizeof(got)) == 0,
          "no field of view given: the game's stands, the head's forward is written");
    std::memcpy(got, camera + kCameraPosition, sizeof(got));
    Check(std::memcmp(got, position, sizeof(got)) == 0, "the head's position is written");
    std::memcpy(got, camera + kCameraUp, sizeof(got));
    Check(std::memcmp(got, up, sizeof(got)) == 0, "the head's up is written");
    Check(camera[0x24] == 0xCD && camera[0x2C] == 0xCD && camera[0x80] == 0xCD,
          "nothing beside the four fields is written");
    WriteHeadCamera(camera, position, sample.forward, up, 1.9f);
    std::memcpy(&fov, camera + kCameraVerticalFov, sizeof(fov));
    Check(fov == 1.9f, "the eyes' field of view, when given");
    WriteHeadCamera(camera, position, sample.forward, up, std::numeric_limits<float>::quiet_NaN());
    std::memcpy(&fov, camera + kCameraVerticalFov, sizeof(fov));
    Check(fov == 1.9f, "a field of view that is not one is not written");
}

// THE REPORT, on a pinhole model: a world point is on screen when it lies in front of the camera inside its
// half-angles (the projector's own test: in front and inside the window).
bool OnScreen(const float forward[3], float halfYawDeg, float targetYawDeg)
{
    float yaw = 0.0f;
    if (!Heading(forward, yaw))
        return false;
    return std::fabs(WrapPi(targetYawDeg * kDegToRad - yaw)) <= halfYawDeg * kDegToRad;
}
// THE HUD'S AFFINE undone: a marker laid out at the undone place is SHOWN on its target by the affine the HUD
// applies (x' = h x, y' = v y + height); outside the HUD's own area it becomes an edge arrow toward the target.
void Show(const HudAffine& a, const float uv[2], float& x, float& y)
{
    const float lx = uv[0] * 2.0f - 1.0f, ly = 1.0f - uv[1] * 2.0f;
    x = a.horizontal * lx;
    y = a.vertical * ly + a.heightNdc;
}
void Affine()
{
    HudAffine affine;
    Check(!MakeHudAffine(0.41f, 0.41f, -117.0f, 0.0f, affine) && !MakeHudAffine(0.0f, 0.41f, 0.0f, 1080.0f, affine) &&
              !MakeHudAffine(0.41f, std::numeric_limits<float>::quiet_NaN(), 0.0f, 1080.0f, affine),
          "an unusable affine is refused");
    // The report's settings: hud_size 0.41, aspect 1, -117 px on a 1080-pixel-high picture.
    Check(MakeHudAffine(0.41f, 0.41f, -117.0f, 1080.0f, affine) && std::fabs(affine.heightNdc + 117.0f / 540.0f) < 1e-6f,
          "the height in half-picture units");
    for (float tx : {-0.3f, -0.1f, 0.0f, 0.2f, 0.35f})
        for (float ty : {-0.5f, -0.3f, 0.0f, 0.1f})
        {
            // A target at (tx, ty) of the picture (y up); the projection's place for it.
            float uv[2]{(tx + 1.0f) * 0.5f, (1.0f - ty) * 0.5f};
            bool on = true;
            Check(UndoHudAffine(affine, on, uv), "an on-screen point is laid out again");
            if (!on)
                continue;
            float x = 0.0f, y = 0.0f;
            Show(affine, uv, x, y);
            Check(std::fabs(x - tx) < 1e-5f && std::fabs(y - ty) < 1e-5f,
                  "laid out at the undone place, the HUD shows the marker on its target");
        }
    {
        // Before: laid out at its own place, the marker was shown at 0.41 of its distance and 117 px low.
        const float uv[2]{(0.3f + 1.0f) * 0.5f, (1.0f - 0.2f) * 0.5f};
        float x = 0.0f, y = 0.0f;
        Show(affine, uv, x, y);
        Check(std::fabs(x - 0.3f) > 0.15f && y < 0.0f, "without the undo the marker was far off its target");
    }
    {
        // Beyond the HUD's own area (here 0.6 of the picture to the right: the HUD spans 0.41 of it): an edge arrow.
        float uv[2]{(0.6f + 1.0f) * 0.5f, 0.5f};
        bool on = true;
        Check(UndoHudAffine(affine, on, uv) && !on, "outside the HUD's area the point is off screen");
        Check(uv[0] > 0.999f && std::fabs(uv[0] - 1.0f) < 1e-5f && uv[1] > 0.0f && uv[1] < 1.0f,
              "its place is on the area's edge, on the target's side");
    }
    {
        float uv[2]{0.2f, 0.7f};
        bool on = false;
        Check(!UndoHudAffine(affine, on, uv) && uv[0] == 0.2f && uv[1] == 0.7f,
              "a point the projection had off screen keeps the game's own edge direction");
    }
    {
        HudAffine identity;
        float uv[2]{0.83f, 0.21f};
        bool on = true;
        Check(UndoHudAffine(identity, on, uv) && on && std::fabs(uv[0] - 0.83f) < 1e-6f &&
                  std::fabs(uv[1] - 0.21f) < 1e-6f,
              "with the stock HUD nothing moves");
    }
}

void Report()
{
    // The objective straight ahead of the head (yaw 0), the gun swung 60 degrees to the right.
    float head[3], gun[3];
    Forward(0.0f, 0.0f, head);
    Forward(-60.0f, 0.0f, gun);
    Check(!OnScreen(gun, 45.0f, 0.0f), "placed from the gun, the objective the player looks at is off screen");
    Check(OnScreen(head, 45.0f, 0.0f), "placed from the head, it is on screen");
    // The gun on the objective, the head turned away: from the gun it was on screen, where the head cannot see it.
    Forward(120.0f, 0.0f, head);
    Forward(0.0f, 0.0f, gun);
    Check(OnScreen(gun, 45.0f, 0.0f) && !OnScreen(head, 45.0f, 0.0f),
          "the gun on it and the head away: the head's placement puts it off screen, so its arrow shows");
}
} // namespace

int main()
{
    Decoding();
    Proof();
    Verdicts();
    Affine();
    Report();
    std::printf("Halo 4 navigation markers: %u checks, %u failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
