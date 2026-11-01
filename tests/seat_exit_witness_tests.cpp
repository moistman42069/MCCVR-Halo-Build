// SEATEXIT witness pure logic: change compression, the Use/Exit verdict, formatting and the edge limiter.
#include "../src/common/seat_exit_witness_logic.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace seat_witness;

static void Check(bool v, const char* message)
{
    if (!v) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static Sample At(uint64_t ms)
{
    Sample s{};
    s.ms = ms;
    s.raw = RawPadValid;
    s.route = RouteNormal;
    s.flags = FlagSemantic | FlagPublished | FlagGameplay;
    return s;
}

int main()
{

    // ---- which semantic actions reach the title's Use/Exit native action ----
    const uint32_t reloadUse = (1u << vr_mapping::Reload) | (1u << vr_mapping::Interact);
    Check(UseActions(GameTitle::Halo3) == reloadUse, "Halo 3: Reload and Use share native action 3");
    Check(UseActions(GameTitle::Halo3ODST) == reloadUse, "ODST: Reload and Use share native action 3");
    Check(UseActions(GameTitle::HaloReach) == (1u << vr_mapping::Interact), "Reach keeps Reload (0x3E) apart");
    Check(UseActions(GameTitle::Halo4) == reloadUse, "Halo 4: Reload and Use share native action 2");
    Check(UseActions(GameTitle::None) == (1u << vr_mapping::Interact), "unknown title falls back to Use only");

    vr_mapping::Transports native{};
    native[vr_mapping::Reload] = 0x4000;   // X
    native[vr_mapping::Interact] = 0x4000;
    native[vr_mapping::Melee] = 0x2000;    // B
    native[vr_mapping::Fire] = kTransportRT;
    Check(UseTransportMask(GameTitle::Halo3, native) == 0x4000, "Halo 3 use mask is the X transport");
    native[vr_mapping::Reload] = 0x0200;   // RB: Reach-style separate reload
    Check(UseTransportMask(GameTitle::HaloReach, native) == 0x4000, "Reach use mask ignores Reload");
    Check(UseTransportMask(GameTitle::Halo3, native) == (0x4000 | 0x0200), "Halo 3 mask unions both carriers");
    native[vr_mapping::Reload] = 0x4000;

    // ---- SameState: digital changes and analog ZONES only ----
    {
        Sample a = At(100), b = At(116);
        a.moveY = 80; b.moveY = 95;
        Check(SameState(a, b), "a driving stick wandering inside one zone is not a change");
        b.moveY = 20;
        Check(!SameState(a, b), "a stick leaving the D-pad zone is a change");
        b = a; b.ms = 200; b.thread = 7;
        Check(SameState(a, b), "a second polling thread with the same pad is the same state");
        b.trigR = 14; a.trigR = 0;
        Check(SameState(a, b), "trigger noise below 0.15 is not a change");
        b.trigR = 16;
        Check(!SameState(a, b), "trigger crossing 0.15 is a change");
        b = a; b.raw |= RawThumbrestL;
        Check(!SameState(a, b), "thumb-rest touch is a change");
        b = a; b.fed = 0x4000;
        Check(!SameState(a, b), "a fed bit is a change");
    }

    // ---- a scripted boot: X pressed 400 ms before an exit, carried by the mapper ----
    Sample ring[16];
    size_t n = 0;
    const uint64_t ev = 10000;
    ring[n] = At(ev - 2000); n++;                                  // idle, before the window
    ring[n] = At(ev - 900); ring[n].raw |= RawThumbrestL; n++;     // thumb on the rest
    ring[n] = At(ev - 400); ring[n].raw |= RawThumbrestL | RawX;
    ring[n].sources = vr_mapping::Bit(vr_mapping::X);
    ring[n].actions = reloadUse; ring[n].mapper = 0x4000; ring[n].fed = 0x4000; ring[n].useMask = 0x4000; n++;
    ring[n] = At(ev - 300); ring[n].raw |= RawThumbrestL; ring[n].useMask = 0x4000; n++;   // released
    ring[n] = At(ev + 100); ring[n].useMask = 0x4000; n++;         // after the edge
    for (size_t i = 0; i < n; ++i) if (!ring[i].useMask) ring[i].useMask = 0x4000;

    UseVerdict v = EvaluateUse(ring, n, ev, 600, UseActions(GameTitle::Halo3));
    Check(v.sent, "X 400 ms before the edge counts as Use/Exit sent");
    Check(v.firstMs == -400 && v.lastMs == -400, "the verdict names when it was sent");
    Check((v.subsystems & SubMapper) && (v.subsystems & SubBridge), "carried by the mapper and the bridge");
    Check(!(v.subsystems & (SubPhysical | SubCrouch | SubGesture | SubWeapon | SubDpad)),
        "no other subsystem is blamed");
    Check(v.rawX, "raw X was down");
    Check(v.covered && v.known, "window covered, mask known");

    // A sample's state holds until the next one: the press at -400 is released at -300, so a window that
    // starts between the two still sees it, from the window start.
    v = EvaluateUse(ring, n, ev, 320, UseActions(GameTitle::Halo3));
    Check(v.sent && v.firstMs == -320, "a held press that spans the window start counts from the start");
    v = EvaluateUse(ring, n, ev, 299, UseActions(GameTitle::Halo3));
    Check(!v.sent, "after the release (at -300) nothing was sent");

    // ---- the unexplained boot: nothing of ours, but the thumb was on the rest ----
    Sample quiet[4];
    quiet[0] = At(ev - 1000); quiet[0].useMask = 0x4000;
    quiet[1] = At(ev - 500); quiet[1].useMask = 0x4000; quiet[1].raw |= RawThumbrestR;
    quiet[1].actions = 1u << vr_mapping::Fire; quiet[1].fed = kTransportRT; quiet[1].mapper = kTransportRT;
    quiet[2] = At(ev - 100); quiet[2].useMask = 0x4000;
    v = EvaluateUse(quiet, 3, ev, 600, UseActions(GameTitle::Halo3));
    Check(!v.sent, "fire alone is not Use/Exit");
    Check(v.otherActions == (1u << vr_mapping::Fire), "other actions are reported");
    Check(v.otherFed == kTransportRT, "other fed bits are reported");

    // ---- physical / crouch / gesture / dpad attribution ----
    {
        Sample s = At(ev - 50);
        s.useMask = 0x4000;
        s.crouch = 0x4000; s.fed = 0x4000;
        Sample pair[2]{At(ev - 700), s};
        pair[0].useMask = 0x4000;
        v = EvaluateUse(pair, 2, ev, 600, UseActions(GameTitle::Halo3));
        Check(v.sent && v.subsystems == SubCrouch, "a crouch transport colliding with Use is blamed on crouch");
        pair[1].crouch = 0; pair[1].physical = 0x4000;
        v = EvaluateUse(pair, 2, ev, 600, UseActions(GameTitle::Halo3));
        Check(v.sent && v.subsystems == SubPhysical, "a physical pad press is blamed on the physical pad");
        pair[1].physical = 0; pair[1].dpad = 0x4000;
        v = EvaluateUse(pair, 2, ev, 600, UseActions(GameTitle::Halo3));
        Check(v.sent && v.subsystems == SubDpad, "a D-pad route press is blamed on the D-pad");
        pair[1].dpad = 0; pair[1].fed = 0; pair[1].actions = 1u << vr_mapping::Interact;
        v = EvaluateUse(pair, 2, ev, 600, UseActions(GameTitle::Halo3));
        Check(v.sent && v.subsystems == SubBridge, "a published Use action with no transport is still sent");
        pair[0].useMask = pair[1].useMask = 0;
        pair[1].actions = 0; pair[1].fed = 0x4000;
        v = EvaluateUse(pair, 2, ev, 600, UseActions(GameTitle::Halo3));
        Check(!v.sent && !v.known, "an unknown mask cannot convict a transport bit");
    }
    Check(!EvaluateUse(ring, 0, ev, 600, 0).sent, "an empty ring sends nothing");

    // ---- Compress: window-start state + changes, newest kept on overflow ----
    {
        size_t idx[8], omitted = 99;
        size_t lines = Compress(ring, n, ev - 1500, ev + 250, idx, 8, &omitted);
        Check(lines == 5 && omitted == 0, "every state in the window is shown");
        Check(idx[0] == 0, "the state at the window start is the sample before it");
        Check(idx[4] == 4, "the tail after the edge is shown");

        Sample dup[6];
        for (int i = 0; i < 6; ++i) dup[i] = At(ev - 1000 + 100 * i);   // heartbeats of one state
        lines = Compress(dup, 6, ev - 1500, ev, idx, 8, &omitted);
        Check(lines == 1 && omitted == 0, "unchanged heartbeats collapse to the first sample");

        Sample many[12];
        for (int i = 0; i < 12; ++i)
        {
            many[i] = At(ev - 1200 + 100 * i);
            many[i].raw |= (i & 1) ? RawA : 0u;   // toggles every sample
        }
        lines = Compress(many, 12, ev - 1500, ev, idx, 4, &omitted);
        Check(lines == 4, "output capped");
        Check(idx[0] == 0, "first kept");
        Check(idx[1] == 9 && idx[2] == 10 && idx[3] == 11, "newest changes kept");
        Check(omitted == 8, "omitted counted");
        lines = Compress(many, 12, ev - 1500, ev, idx, 1, &omitted);
        Check(lines == 1 && idx[0] == 0 && omitted == 11, "a single-line cap keeps the window start");
        Check(Compress(many, 12, ev - 1500, ev - 1300, idx, 4, &omitted) == 0, "no sample before toMs");
        Check(Compress(many, 0, 0, ev, idx, 4, &omitted) == 0, "empty ring");
    }

    // ---- Formatting ----
    {
        char line[512];
        FormatSample(line, sizeof(line), ring[2], ev);
        Check(std::strstr(line, "-400ms") != nullptr, "relative time printed");
        Check(std::strstr(line, "raw[X+TRL]") != nullptr, "raw buttons and touch printed");
        Check(std::strstr(line, "act[Reload+Use]") != nullptr, "actions named");
        Check(std::strstr(line, "fed[X]") != nullptr, "fed transport named");
        Check(std::strstr(line, "map[X]") != nullptr, "contributor named");
        Check(std::strstr(line, "<USE>") != nullptr, "use flagged");
        Check(std::strstr(line, "crouch[") == nullptr, "empty contributors omitted");
        char tiny[16];
        FormatSample(tiny, sizeof(tiny), ring[2], ev);
        Check(std::strlen(tiny) < sizeof(tiny), "formatting never overruns");
        char t[64];
        AppendTransport(t, sizeof(t), 0, kTransportLT | kTransportRT | 0x0010);
        Check(std::strcmp(t, "Start+LT+RT") == 0, "triggers use bits 16/17");
        AppendTransport(t, sizeof(t), 0, 0);
        Check(std::strcmp(t, "-") == 0, "empty transport prints a dash");
        Sample nopad{};
        FormatSample(line, sizeof(line), nopad, 0);
        Check(std::strstr(line, "raw[NOPAD]") != nullptr, "an invalid pad says so");
    }

    // ---- Edge limiter ----
    {
        EdgeLimiter l;
        unsigned carried = 0;
        int allowed = 0;
        for (int i = 0; i < 10; ++i) allowed += l.Allow(1000 + i, 4, 1000, &carried) ? 1 : 0;
        Check(allowed == 4, "burst capped");
        Check(l.Allow(2000, 4, 1000, &carried) && carried == 6, "suppressed edges are reported later");
        Check(l.Allow(2001, 4, 1000, &carried) && carried == 0, "count resets after reporting");
        Check(l.Allow(5, 4, 1000, &carried), "a backwards clock reopens the window");
    }

    // ---- Seat direct drive summary ----
    {
        static DriveSample d[128];
        size_t m = 0;
        for (int i = 0; i < 100; ++i)
        {
            DriveSample s{};
            s.ms = ev - 1000 + 10 * i;
            s.slot = 0; s.caller = 0; s.outcome = DriveWrote; s.pcLow = 0x1000;
            s.flags = DriveArmed | DriveFresh | DriveSeatedSet | DriveSeatConfirmed | DriveStint | DriveYawOk;
            s.aimYaw = 0.5f; s.aimPitch = 0.1f;
            s.wroteYaw = 0.51f; s.wrotePitch = 0.1f;
            s.nativeYaw = i ? 0.512f : 0.5f; s.nativePitch = 0.1f;
            if (s.ms == ev - 100) s.pcLow = 0x2000;   // the record moved
            d[m++] = s;
            if (s.ms == ev - 500)
            {
                DriveSample k = s;
                k.caller = 3; k.outcome = DrivePutBack;
                d[m++] = k;
            }
        }
        DriveSample other = d[0];
        other.slot = 1; other.ms = ev - 700; other.wroteYaw = 3.0f;
        d[m++] = other;   // an ODST call in a Halo 3 summary is ignored (it is out of order, also harmless)
        DriveBucket b[8];
        const size_t nb = SummarizeDrive(d, m, 0, ev - 1500, ev + 250, 250, b, 8);
        Check(nb == 7, "1.75 s in 250 ms buckets");
        Check(b[0].byCaller[0] == 0 && b[1].byCaller[0] == 0, "nothing before the first call");
        Check(b[2].byCaller[0] == 25 && b[2].yawWrites == 25, "25 stick calls in a 250 ms bucket at 100 Hz");
        Check(b[4].byCaller[3] == 1 && b[4].byOutcome[DrivePutBack] == 1, "the kick and its put-back counted");
        Check(b[2].maxWroteVsAimYaw > 0.56f && b[2].maxWroteVsAimYaw < 0.59f, "write vs aim in degrees");
        Check(b[3].maxDriftYaw > 0.10f && b[3].maxDriftYaw < 0.13f, "drift between our write and the next call");
        Check(b[2].maxDriftPitch < 1e-3f, "no pitch drift");
        Check(b[5].pcChanges == 2, "a record change is flagged (moved and moved back)");
        Check(b[3].byCaller[0] == 25 && b[2].maxWroteVsAimPitch < 1e-3f, "other slot ignored");
        Check(WrapRad(6.28f) < 0.0f && WrapRad(6.28f) > -0.01f, "yaw differences wrap");
        Check(AbsDeg(WrapRad(3.14f - (-3.14f))) < 0.4f, "a write across +-pi is a small drift");
        Check(SummarizeDrive(d, m, 0, ev, ev, 250, b, 8) == 0, "an empty window has no buckets");
        Check(b[2].haveWroteYaw && b[2].minWroteYaw > 29.2f && b[2].maxWroteYaw < 29.3f, "raw written yaw range");
        Check(b[2].branchJumps == 0 && b[2].bigSteps == 0, "steady writes are no jumps");
        // A write on the record's other 2*pi branch: wrapped it is 0.6 deg, raw it is a full turn.
        DriveSample j[2]{d[0], d[1]};
        j[1].wroteYaw = j[1].nativeYaw + 6.2831853f + 0.01f;
        SummarizeDrive(j, 2, 0, ev - 1500, ev + 250, 250, b, 8);
        Check(b[2].branchJumps == 1 && b[2].bigSteps == 1, "a branch jump is counted");
        Check(b[2].maxWroteVsAimYaw < 1.5f, "the wrapped difference alone would hide it");
        Check(b[2].maxWroteYaw - b[2].minWroteYaw > 350.0f, "the raw range shows it");

        // State-change compression over drive calls.
        size_t idx[6], omitted = 0;
        const size_t lines = CompressBy(d, 100, ev - 1500, ev + 250, idx, 6, &omitted,
            [](const DriveSample& a, const DriveSample& bb) { return DriveSameState(a, bb); });
        Check(lines >= 3 && idx[0] == 0, "the drive's first state and its changes are kept");
    }

    // ---- The game's own Use record ----
    {
        auto entry = [](uint64_t ms, uint8_t frames, uint8_t action = 3, uint8_t title = 1) {
            NativeActionEntry e{};
            e.ms = ms; e.frames = frames; e.heldMs = static_cast<uint16_t>(frames * 8);
            e.action = action; e.title = title; e.present = 1;
            return e;
        };
        NativeActionEntry e[8]{entry(ev - 2000, 0), entry(ev - 450, 1), entry(ev - 445, 9, 0x26),
            entry(ev - 440, 2), entry(ev - 430, 3), entry(ev - 300, 0), entry(ev - 50, 0), entry(ev + 50, 4)};
        NativeHeldSpan h = EvaluateNativeHeld(e, 8, 1, 3, ev, 600);
        Check(h.any && h.firstMs == -450 && h.lastMs == -300, "the game's Use was held -450..-300");
        Check(h.maxFrames == 3 && h.covered && h.reads == 5, "frames, coverage and reads");
        h = EvaluateNativeHeld(e, 8, 1, 3, ev, 200);
        Check(!h.any, "released before a 200 ms window");
        h = EvaluateNativeHeld(e, 8, 1, 0x26, ev, 600);
        Check(h.any && h.lastMs == 0 && h.maxFrames == 9, "a record still held at the edge runs to the edge");
        h = EvaluateNativeHeld(e, 8, 2, 3, ev, 600);
        Check(!h.any && !h.covered && h.reads == 0, "another title's entries are ignored");
    }

    // ---- The vehicle's frame ----
    {
        VehicleFrameEntry f[60];
        for (int i = 0; i < 50; ++i)
        {
            f[i] = VehicleFrameEntry{};
            f[i].ms = ev - 1000 + 20 * i;
            f[i].pos[0] = 0.2f * i;
            f[i].up[2] = 1.0f;
            f[i].fwd[0] = 1.0f;
            if (f[i].ms == ev - 200) { f[i].up[1] = 0.2f; f[i].up[2] = -0.98f; }
        }
        VehicleKinematics k = SummarizeVehicle(f, 50, ev, 1500);
        Check(k.valid && k.frames == 50, "every frame in the window");
        Check(k.speed > 9.9f && k.speed < 10.1f, "speed from consecutive positions (10 wu/s)");
        Check(k.minUpZ < 0.0f, "an upside-down frame is caught");
        Check(k.up[2] == 1.0f && k.lastMs == -20, "the last frame before the edge");
        k = SummarizeVehicle(f, 50, ev - 2000, 500);
        Check(!k.valid, "no frame before the window");
    }

    std::puts("SEATEXIT witness logic checks passed");
    return 0;
}
