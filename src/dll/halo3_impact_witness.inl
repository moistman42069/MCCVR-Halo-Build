// Included in game.cpp (inside its anonymous namespace) right after halo3_assist_witness.inl: it reads the fire lane's
// thread state (t_halo3Fire, t_legacyFirePredicted, g_halo3FiringWeapon, g_halo3IndependentShot) and object access
// (Halo3MeleeSelectionObject), and is fed from halo3_independent_shots.inl, halo3_collision_dispatch.inl,
// legacy_sight_trace.inl, the compositor (Game_DevNoteTracedReticle) and the XInput hook (Game_DevNoteFireTrigger).
//
// THE SHOT IMPACT WITNESS, Halo 3 on foot (dev builds only). The SHOT LAG [muzzle] line is circular: a
// muzzle shot's direction is BUILT as normalize(P - muzzle), so it passes through P by construction. This witness
// asks the game's own objects where each local infantry round went (shot_impact_logic.h):
//
//   SHOT IMPACT Halo 3 [<muzzle|native>; <predicted|authoritative>] (dev): fires, projectiles; ROUND LINE vs P (the
//   spawned object's position and VELOCITY against P, per eye-speed bin, along/across travel); the native's turn after
//   our line (forward vs the direction we handed it); velocity off the forward (what the shooter's motion added, and
//   how much of the eye's speed that is); speed, flight time, range; later ticks (the same object re-read: moved off
//   its spawn line?); IMPACT vs P (the hit point of the projectile's own collision query); and the round line and the
//   impact vs P' -- P moved with the eye since its camera sample (dev key shot_point_transport, shot_lag_logic.h 4a),
//   computed on every fire whichever the key, so both arms of the A/B report against both points
//
//   SHOT RETICLE Halo 3 (dev): the reticle the compositor last drew (its ray mapped to the world through the aim
//   path's own map, from the rendered eye of its frame, at the range it was drawn for) against P; how old P was at
//   the fire (its hand, its camera), whether P's frame was older than the reticle's; and RT-to-fire latency.
//
//   AS SEEN Halo 3 VR [...] (dev): each on-foot impact against the reticle drawn in the FIRST prepared frame rendered
//   after it (SEEN), against the game's own crosshair line in that frame (STOCK), and against P's line (AT FIRE), per
//   strafe-speed and range bin, with the clocks between (as_seen_logic.h; the flat probe computes the same line for
//   stock Halo 3); AS SEEN Halo 3 VR camera (dev): did the engine eye move on every prepared frame while moving?
//
//   SHOT IMPACT Halo 3 SEATED [gun owner def, weapon def, the vehicle's gun | his own weapon] (dev): the same, per
//   seat, for the local player's seated fires (the shot line names him the shooter; his unit's parent is set), judged
//   against the assist's output (the line our seated re-aim made), plus INHERITANCE: the fire routine's branch flag,
//   the owner flag it tests first, and the placement's sideways velocity along travel -- and whether each round's
//   speed is the constructor's (scale x the tag's initial speed). SHOT IMPACT Halo 3 seated: the measured velocity
//   unit and the compensation's counts (dev key seated_inherit_compensation, Halo3SeatedInheritPoint).
//
// HOW THE GAME SPAWNS A ROUND (pinned retail halo3.dll; read with capstone, every RVA checked against the bytes):
//   * the fire routine 0x3683A0 (pdata to 0x369FB9) builds a placement block on its frame (rbp+0xD0) through
//     0x341508, writes position (+0x1C), forward (+0x28, after spread 0x109648) and translational velocity (+0x40),
//     then calls object_new 0x341AE8 at 0x369720 (return 0x369725) and keeps the handle at [rsp+0x4C];
//   * object_new copies the block into the object: +0x1C -> +0x50 (position), +0x28 -> +0x5C (forward), +0x34 -> +0x68
//     (up), +0x40 -> +0x74 (velocity) (0x341D19..0x341D56); the projectile type (index 5 of the type table at
//     0x815160, named "projectile") constructs at 0x384D3C, which adds forward * initial speed into +0x74
//     (0x384FBE..0x384FE6);
//   * THE INHERITED VELOCITY: the placement velocity is forward * max(0, V.forward) with V the shot line's own
//     velocity output (0x3524B0's fourth argument, [rbp+0x28]; 0x368BD6..0x368C14), PLUS -- when the owner's object
//     flag +0xA8 bit 3 is set (0x368B97) and 0x397E4C answers -- the velocity of the physics body the owner rides
//     (0x397EA8: v + w x r) with its forward part removed (0x369419..0x3694AF, flag [rsp+0x45]). So a gun carried by a
//     vehicle (its owner is the vehicle) inherits the vehicle's full sideways velocity; whether an on-foot biped ever
//     carries the flag is not static -- this witness measures it ("velocity off the forward").
//   * the firing assist 0x13BAD0 (called at 0x368DFE) traces 128 wu along the CAMERA ray (0x212198 at 0x13BC08; the
//     muzzle lane substitutes the muzzle ray there, Halo3IndependentCameraDetour) and turns the shot toward the hit
//     minus the shot origin; on foot that turn is uncapped. Its no-target path does not subtract the shooter's
//     velocity; its target path leads the TARGET's velocity (0x13BDBC..0x13BE48).
//
// ANCHORS (each unique in the module, matched where pdata says it belongs; offsets decoded from the bytes, never
// assumed), and the per-shot proof:
//   * object_new's prologue -> its entry; its placement copy (four movsd/mov pairs) -> the six offsets above, and
//     RtlLookupFunctionEntry must place the copy inside object_new;
//   * the fire routine's call `lea rcx, [rbp+..]; call object_new; ...; mov [rsp+..], eax` -> the call must target
//     object_new and lie inside the dual lane's fire routine (its pinned entry; pdata);
//   * object_new's type-table load `lea rbx, [image base]; ...; mov rbx, [rbx+rax*8+table]` -> the table; entry 5's
//     name must read "projectile";
//   * every shot: the new object's position and forward must equal the placement's bitwise, and its velocity the
//     placement's plus something along the forward (shot_impact::SpawnProven) -- else the shot is "unproven" and
//     measured not at all.
// The hook forwards its one argument exactly: `py -3 tools/audit_caller_area.py halo3.dll 341AE8:1:i` is clean (no
// caller-area slot touched; 47 native callers, none relies on a volatile register across the call). It filters by
// return address and touches nothing it does not read. ADDR-HYGIENE: dev-only (a witness; decides nothing).

#if HALOMCCVR_DEV_DIAGNOSTICS
using Halo3ObjectNewFn = int32_t(__fastcall*)(void*);

struct Halo3ImpactWitness
{
    void* target = nullptr;
    Halo3ObjectNewFn original = nullptr;
    std::atomic<uint32_t> callbacks{0};
    uintptr_t base = 0;
    uintptr_t fireReturn = 0;
    // Decoded from object_new's copy of the placement block.
    uint32_t placePos = 0, placeFwd = 0, placeVel = 0, objPos = 0, objFwd = 0, objVel = 0;
    uint8_t projectileType = 0xFF;
    // The fire routine's frame (seated rounds; decoded at install, frameProven when every anchor agreed): rbp - rsp,
    // the inheritance flag byte [rsp+flag], the body velocity [rbp+body], the shooter's velocity the shot line
    // reported [rbp+occupant], and the owner's flag word (+ownerFlag, bit ownerBit) the branch first tests.
    bool frameProven = false;
    uint32_t rbpDelta = 0, flagDisp = 0, bodyDisp = 0, occupantDisp = 0, ownerFlagDisp = 0;
    uint8_t ownerFlagBit = 0;
    // The projectile constructor's speed (speedProven): it adds forward x (object +scale) x (tag +tagSpeed).
    bool speedAnchors = false;
    uint32_t scaleDisp = 0, tagSpeedDisp = 0;
    std::atomic<bool> ready{false};
    std::atomic<int> armed{0};
    std::atomic<uint32_t> faults{0};
    // Everything below is under `lock`.
    SRWLOCK lock = SRWLOCK_INIT;
    shot_impact::Tracker tracker;
    shot_impact::Window windows[4];   // on foot: [muzzle][predicted]
    shot_impact::SeatWindows seats;   // seated: per gun owner and weapon
    shot_impact::UnitsTrack units;    // the object velocity's unit, measured (never reset: a property of the build)
    shot_impact::SpeedTable speeds;   // each weapon's proven round speed (the compensation reads it)
    shot_impact::ReticleWindow reticle;
    shot_impact::TriggerLatch trigger;
    uint32_t nested = 0, ourLineThroughP = 0, ourLineOffP = 0;
    // The compensation (seated_inherit_compensation), per window.
    uint32_t compApplied = 0, compNoInherit = 0, compNoSpeed = 0, compNoFrame = 0, compNoSolution = 0;
    float compShiftMmSum = 0.0f, compShiftMmMax = 0.0f, compResidualMax = 0.0f;
};
Halo3ImpactWitness g_halo3Impact;

// The reticle the compositor last drew (Game_DevNoteTracedReticle): tracking space, its prepared frame.
struct Halo3ImpactReticle
{
    SRWLOCK lock = SRWLOCK_INIT;
    bool valid = false;
    uint64_t serial = 0, stampUs = 0;
    float center[3]{}, dir[3]{};
    float rangeM = 0.0f;
    bool hit = false;
};
Halo3ImpactReticle g_halo3ImpactReticle;

// AS SEEN (as_seen_logic.h). The symptom it measures: while strafing, flat shows the impact marks at the reticle,
// and VR shows them trailing it by about half the reticle. SHOT IMPACT proves the round lands on
// P; the player judges it against the reticle on screen WHEN THE IMPACT APPEARS. So, per tracked round that hits:
//   * at the spawn: P's line (its rendered eye H_P along u), its camera sample (the engine eye E_P, its stamp, the
//     prepared frame it rendered in and that frame's sim tick), the fire's stamp and tick;
//   * at the hit (the projectile's own collision query): the impact point, its stamp and sim tick -> pending;
//   * the FIRST prepared frame whose render follows the hit (CamCopyHook, one note per prepared serial): its rendered
//     eye H_F, engine eye E_F, its scene camera basis and head pose, the game's aim forward, its sim tick;
//   * the reticle the compositor drew for THAT serial (Game_DevNoteTracedReticle): its tracking direction mapped into
//     the world through that frame's own camera (as the compositor shows it over the scene), from H_F.
// Then SEEN = the impact against that reticle line; STOCK = against the game's own crosshair line in that frame
// (E_F along the aim forward: what flat would show for the same motion); AT FIRE = against P's line; and the clocks.
// Everything here is under g_halo3Impact.lock; the frame note takes it once per prepared frame.
struct Halo3AsSeenAim
{
    bool valid = false;
    float head[3]{}, dir[3]{}, eye[3]{};   // H_P, u, E_P
    bool eyeValid = false;
    uint64_t camUs = 0, serial = 0, fireUs = 0;
    int64_t camTick = -1;
    uint32_t fireTick = 0;
    uint32_t queries = 0;   // the round's own collision queries so far (one a tick): ticks from the first to the hit
    bool velValid = false;
    float vel[3]{};
};
struct Halo3AsSeenFrame
{
    bool valid = false;
    uint64_t serial = 0, stampUs = 0;
    uint32_t tick = 0;
    float head[3]{}, eye[3]{}, fwd[3]{}, up[3]{}, aimFwd[3]{}, key[3]{}, quat[4]{};
};
struct Halo3AsSeenPending
{
    bool used = false;
    bool framed = false;
    float impact[3]{};
    uint64_t impactUs = 0;
    uint32_t impactTick = 0;
    Halo3AsSeenAim aim{};
    Halo3AsSeenFrame frame{};
};
struct Halo3AsSeenReticle
{
    bool valid = false;
    uint64_t serial = 0, stampUs = 0;
    float center[3]{}, dir[3]{};
};
struct Halo3AsSeen
{
    static constexpr int kFrames = 32, kReticles = 16, kPending = 16;
    Halo3AsSeenAim aim[shot_impact::Tracker::kSlots]{};
    Halo3AsSeenFrame frames[kFrames]{};
    uint32_t frameNext = 0;
    Halo3AsSeenReticle reticles[kReticles]{};
    uint32_t reticleNext = 0;
    Halo3AsSeenPending pending[kPending]{};
    std::atomic<uint32_t> pendingCount{0};   // used slots: the frame note takes the lock only while any wait
    as_seen::Window window{};
    as_seen::CameraCensus census{};
    // The reticle's world direction two ways -- through the frame's own camera (as drawn over the scene) and through
    // the aim path's map (TrackingDirToWorld) -- and the reticle's origin against the head sample the frame rendered.
    uint32_t mapN = 0;
    double mapSumDeg = 0.0, mapMaxDeg = 0.0, originSumMm = 0.0;
    uint32_t evictedPending = 0;
};
Halo3AsSeen g_halo3AsSeen;

// One native fire on this thread: opened by the fire detour, filled by its shot line and its object_new.
struct Halo3ImpactFire
{
    bool open = false;
    uint64_t beginUs = 0;   // Sight_NowUs at the fire detour's entry
    bool lineSeen = false;
    bool local = false;
    bool muzzle = false;
    int predicted = -1;
    uint32_t projectiles = 0;
    float lineOrigin[3]{}, lineDir[3]{};
    bool pointKnown = false;
    shot_lag::IntentPoint point{};   // seated: point = the intent's solvePoint (the point the seated re-aim uses)
    // P' -- that point moved with the eye since its camera sample (Sight_TransportIntent, keys not consulted) -- and
    // P' - P, valid when `transport` applied. Every fire is judged against both, whichever the key.
    shot_lag::Transport transport = shot_lag::Transport::Count;
    float pointT[3]{}, shiftT[3]{};
    // Seated (the occupant's parent link is set): the gun's owner, its definition, the weapon's.
    bool seated = false;
    shot_impact::SeatKey seat{};
    uint32_t weaponDef = UINT32_MAX;
    bool compensated = false;
};
thread_local Halo3ImpactFire t_halo3ImpactFire;

namespace
{
int Halo3ImpactGroup(const Halo3ImpactFire& fire)
{
    return (fire.muzzle ? 2 : 0) + (fire.predicted > 0 ? 1 : 0);
}

// Guarded reads (no C++ objects with destructors here: SEH only).
bool Halo3ImpactCopy3(const void* from, float out[3])
{
    __try
    {
        memcpy(out, from, sizeof(float) * 3);
        return shot_lag::Finite3(out);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// The projectile object's position / forward / velocity, or false when the handle no longer names a live projectile.
bool Halo3ImpactReadObject(uint32_t handle, float pos[3], float fwd[3], float vel[3])
{
    const auto& w = g_halo3Impact;
    __try
    {
        const auto* object = Halo3MeleeSelectionObject(handle, w.projectileType);
        if (!object)
            return false;
        memcpy(pos, object + w.objPos, sizeof(float) * 3);
        memcpy(fwd, object + w.objFwd, sizeof(float) * 3);
        memcpy(vel, object + w.objVel, sizeof(float) * 3);
        return shot_lag::Finite3(pos) && shot_lag::Finite3(fwd) && shot_lag::Finite3(vel);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_halo3Impact.faults.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}

// Any live object's data (Halo3ContactObject's checked table walk), or null. Guarded.
const uint8_t* Halo3ImpactObjectData(uint32_t handle)
{
    __try
    {
        if (!Halo3ContactObject(handle))
            return nullptr;
        const auto* table = *reinterpret_cast<const uint8_t* const*>(Halo3ContactTls() + 0x38);
        const auto* entries = *reinterpret_cast<const uint8_t* const*>(table + 0x48);
        return *reinterpret_cast<const uint8_t* const*>(entries + (handle & 0xFFFF) * 0x18 + 0x10);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

// An object's definition (its tag datum, object +0), or UINT32_MAX.
uint32_t Halo3ImpactDefinition(uint32_t handle)
{
    __try
    {
        const auto* data = Halo3ImpactObjectData(handle);
        return data ? *reinterpret_cast<const uint32_t*>(data) : UINT32_MAX;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return UINT32_MAX;
    }
}

// The fire routine's inheritance, read from its own frame (frameProven): the branch flag, the body velocity and the
// shooter's velocity the shot line reported. `fireRsp`: the routine's rsp at the call we are inside of.
bool Halo3ImpactReadFireFrame(uintptr_t fireRsp, bool& taken, float body[3], float occupant[3])
{
    const auto& w = g_halo3Impact;
    if (!w.frameProven || !fireRsp)
        return false;
    __try
    {
        taken = *reinterpret_cast<const uint8_t*>(fireRsp + w.flagDisp) != 0;
        const uintptr_t rbp = fireRsp + w.rbpDelta;
        memcpy(body, reinterpret_cast<const void*>(rbp + w.bodyDisp), sizeof(float) * 3);
        memcpy(occupant, reinterpret_cast<const void*>(rbp + w.occupantDisp), sizeof(float) * 3);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_halo3Impact.faults.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}

// The flag the fire routine tests on the gun's owner before it asks for the body velocity (0x368B97). Guarded.
bool Halo3ImpactOwnerFlag(uint32_t owner)
{
    const auto& w = g_halo3Impact;
    __try
    {
        const auto* data = Halo3ImpactObjectData(owner);
        return data && ((*reinterpret_cast<const uint32_t*>(data + w.ownerFlagDisp) >> w.ownerFlagBit) & 1u) != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// The projectile's speed as the constructor computed it: (object +scale) x (its tag +tagSpeed). Guarded.
bool Halo3ImpactReadRoundSpeed(uint32_t handle, float& scale, float& tagSpeed)
{
    const auto& w = g_halo3Impact;
    if (!w.speedAnchors)
        return false;
    __try
    {
        const auto* object = Halo3MeleeSelectionObject(handle, w.projectileType);
        const auto* tag = object ? Halo3LoadedTagDefinition(*reinterpret_cast<const uint32_t*>(object)) : nullptr;
        if (!tag)
            return false;
        memcpy(&scale, object + w.scaleDisp, sizeof(scale));
        memcpy(&tagSpeed, tag + w.tagSpeedDisp, sizeof(tagSpeed));
        return std::isfinite(scale) && std::isfinite(tagSpeed);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// The rendered eye of a prepared frame (the camera ring, PublishCamFrame), by serial.
bool Halo3ImpactFrameBySerial(uint64_t serial, shot_lag::EyeFrame& out)
{
    if (!serial)
        return false;
    shot_lag::EyeFrame frames[kCamFrameSlots]{};
    const size_t count = ReadCamFrames(frames);
    for (size_t i = 0; i < count; ++i)
        if (frames[i].valid && frames[i].serial == serial)
        {
            out = frames[i];
            return true;
        }
    return false;
}

// The local fire's P (the intent the shot sites read), fresh and this generation's. Seated: the point the seated
// re-aim aims through (SightIntent::solvePoint), and only an intent that says it is seated.
// `how` / `pointT` / `shiftT`: P moved with the eye (Sight_TransportIntent), for the vs P' columns.
bool Halo3ImpactReadPoint(uint64_t nowUs, bool seated, shot_lag::IntentPoint& out, shot_lag::Transport& how,
                          float pointT[3], float shiftT[3])
{
    SightIntent intent{};
    how = shot_lag::Transport::Count;
    const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::Halo3);
    if (!Sight_ReadIntent(GameTitle::Halo3, intent) || !generation || intent.p.generation != generation ||
        !intent.p.stampUs || nowUs < intent.p.stampUs || nowUs - intent.p.stampUs > 250000ull ||
        intent.p.seated != seated)
        return false;
    out = intent.p;
    if (seated)
        memcpy(out.point, intent.solvePoint, sizeof(out.point));
    if (!shot_lag::Finite3(out.point))
        return false;
    // P' as THIS fire's shot used it (the muzzle site decides before the shot line: Sight_LastShotPoint on this thread,
    // stamped inside this fire); else from this tick's eye carried a tick, whichever the key -- a witness never calls
    // the game itself.
    SightLastShotPoint last{};
    if (!seated && Sight_LastShotPoint(last) && t_halo3ImpactFire.beginUs && last.stampUs >= t_halo3ImpactFire.beginUs &&
        last.stampUs <= nowUs)
    {
        how = last.how;
        memcpy(pointT, last.point, sizeof(float) * 3);
        memcpy(shiftT, last.shift, sizeof(float) * 3);
        return true;
    }
    how = Sight_TransportIntent(GameTitle::Halo3, intent, seated, nullptr, false, pointT, shiftT);
    return true;
}

// The reticle the player saw at this fire, against P (and P's age), into the reticle window. Under the lock.
void Halo3ImpactMeasureReticle(const shot_lag::IntentPoint& p, uint64_t nowUs)
{
    auto& w = g_halo3Impact;
    auto& r = w.reticle;
    Halo3ImpactReticle seen{};
    AcquireSRWLockShared(&g_halo3ImpactReticle.lock);
    seen.valid = g_halo3ImpactReticle.valid;
    seen.serial = g_halo3ImpactReticle.serial;
    seen.stampUs = g_halo3ImpactReticle.stampUs;
    memcpy(seen.center, g_halo3ImpactReticle.center, sizeof(seen.center));
    memcpy(seen.dir, g_halo3ImpactReticle.dir, sizeof(seen.dir));
    seen.rangeM = g_halo3ImpactReticle.rangeM;
    seen.hit = g_halo3ImpactReticle.hit;
    ReleaseSRWLockShared(&g_halo3ImpactReticle.lock);

    const float handAgeMs = float(nowUs - p.stampUs) / 1000.0f;
    const bool eyeKnown = p.eyeStampUs && nowUs >= p.eyeStampUs;
    const float eyeAgeMs = eyeKnown ? float(nowUs - p.eyeStampUs) / 1000.0f : 0.0f;
    const int64_t framesOlder = (seen.valid && seen.serial && p.serial)
        ? static_cast<int64_t>(seen.serial) - static_cast<int64_t>(p.serial) : 0;
    r.AddPointAge(handAgeMs, eyeKnown, eyeAgeMs, framesOlder);

    const float scale = Sight_UnitsPerVrMetre(GameTitle::Halo3);
    if (!seen.valid || !seen.hit || !seen.stampUs || nowUs < seen.stampUs || nowUs - seen.stampUs > 100000ull ||
        !std::isfinite(scale) || !(scale > 0.0f) || !(seen.rangeM > 0.0f))
    {
        ++r.noReticle;
        return;
    }
    shot_lag::EyeFrame frame{};
    if (!Halo3ImpactFrameBySerial(seen.serial, frame))
    {
        ++r.noFrame;
        return;
    }
    float dirWorld[3]{};
    if (!shot_impact::TrackingDirToWorld(seen.dir, g_gameYawRef, g_headYawRef, g_yawSign.load(),
                                         g_pitchSign.load(), dirWorld))
    {
        ++r.noReticle;
        return;
    }
    const float rangeWu = seen.rangeM * scale;
    const float offset[3]{frame.head[0] + dirWorld[0] * rangeWu - p.point[0],
                          frame.head[1] + dirWorld[1] * rangeWu - p.point[1],
                          frame.head[2] + dirWorld[2] * rangeWu - p.point[2]};
    const float keyDelta[3]{seen.center[0] - frame.key[0], seen.center[1] - frame.key[1],
                            seen.center[2] - frame.key[2]};
    r.AddOffset(shot_impact::Decompose(offset, p.dir, p.eyeVelValid ? p.eyeVel : nullptr),
                float(nowUs - seen.stampUs) / 1000.0f, shot_lag::Length(keyDelta) * 1000.0f);
}

// The window a fire reports into (under the lock): on foot its group's; seated its seat's (`seat` receives the
// index, -1 on foot).
shot_impact::Window& Halo3ImpactFireWindow(const Halo3ImpactFire& fire, uint64_t nowUs, int& seat)
{
    auto& w = g_halo3Impact;
    if (!fire.seated)
    {
        seat = -1;
        return w.windows[Halo3ImpactGroup(fire)];
    }
    seat = w.seats.Index(fire.seat, nowUs);
    return w.seats.window[seat];
}

// A tracked round's window (under the lock), or null when its seat's window has since been given to another seat.
shot_impact::Window* Halo3ImpactTrackedWindow(const shot_impact::Tracked& t)
{
    auto& w = g_halo3Impact;
    if (t.seat < 0)
        return &w.windows[t.group & 3];
    const int i = t.seat;
    if (i >= shot_impact::SeatWindows::kSeats || !w.seats.used[i])
        return nullptr;
    const shot_impact::SeatKey key{t.seatOwnerDef, t.seatWeaponDef, t.seatOwnGun};
    return w.seats.key[i] == key ? &w.seats.window[i] : nullptr;
}

// ---- AS SEEN helpers (all under g_halo3Impact.lock) ----------------------------------------------------------------
// The sim tick of the prepared frame `serial` rendered in (the frame ring), or -1.
int64_t Halo3AsSeenTickOfSerial(uint64_t serial)
{
    if (!serial)
        return -1;
    for (const auto& f : g_halo3AsSeen.frames)
        if (f.valid && f.serial == serial)
            return f.tick;
    return -1;
}

// v rotated by the inverse of the unit quaternion q (x, y, z, w): tracking space -> the head's own frame.
void Halo3AsSeenRotateInverse(const float q[4], const float v[3], float out[3])
{
    const float qv[3]{-q[0], -q[1], -q[2]};
    const float w = q[3];
    const float t[3]{2.0f * (qv[1] * v[2] - qv[2] * v[1]), 2.0f * (qv[2] * v[0] - qv[0] * v[2]),
                     2.0f * (qv[0] * v[1] - qv[1] * v[0])};
    out[0] = v[0] + w * t[0] + (qv[1] * t[2] - qv[2] * t[1]);
    out[1] = v[1] + w * t[1] + (qv[2] * t[0] - qv[0] * t[2]);
    out[2] = v[2] + w * t[2] + (qv[0] * t[1] - qv[1] * t[0]);
}

// The reticle's tracking direction as the compositor shows it over this frame's scene: into the head's frame (OpenXR:
// +X right, +Y up, -Z forward) with the head pose the render used, then out through the camera basis the head look
// wrote for that render (forward, up, left = up x forward).
bool Halo3AsSeenReticleWorld(const Halo3AsSeenFrame& frame, const float trackingDir[3], float out[3])
{
    const float ql = std::sqrt(frame.quat[0] * frame.quat[0] + frame.quat[1] * frame.quat[1] +
                               frame.quat[2] * frame.quat[2] + frame.quat[3] * frame.quat[3]);
    if (!std::isfinite(ql) || ql < 0.5f || !shot_lag::Finite3(trackingDir) || !shot_lag::Finite3(frame.fwd) ||
        !shot_lag::Finite3(frame.up))
        return false;
    const float q[4]{frame.quat[0] / ql, frame.quat[1] / ql, frame.quat[2] / ql, frame.quat[3] / ql};
    float local[3]{};
    Halo3AsSeenRotateInverse(q, trackingDir, local);
    const float* f = frame.fwd;
    const float* u = frame.up;
    const float left[3]{u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2], u[0] * f[1] - u[1] * f[0]};
    for (int axis = 0; axis < 3; ++axis)
        out[axis] = -local[2] * f[axis] + local[1] * u[axis] - local[0] * left[axis];
    const float l = shot_lag::Length(out);
    if (!std::isfinite(l) || l < 1.0e-6f)
        return false;
    for (int axis = 0; axis < 3; ++axis)
        out[axis] /= l;
    return true;
}

// The engine eye's velocity at `frame` from the ring (50-200 ms back), else `fallback`.
bool Halo3AsSeenEyeVel(const Halo3AsSeenFrame& frame, const float* fallback, float out[3])
{
    const Halo3AsSeenFrame* best = nullptr;
    for (const auto& f : g_halo3AsSeen.frames)
    {
        if (!f.valid || f.stampUs >= frame.stampUs)
            continue;
        const uint64_t dt = frame.stampUs - f.stampUs;
        if (dt < 50000 || dt > 200000)
            continue;
        if (!best || f.stampUs > best->stampUs)
            best = &f;
    }
    if (best)
    {
        const float dt = float(frame.stampUs - best->stampUs) * 1.0e-6f;
        for (int axis = 0; axis < 3; ++axis)
            out[axis] = (frame.eye[axis] - best->eye[axis]) / dt;
        if (shot_lag::Finite3(out))
            return true;
    }
    if (fallback && shot_lag::Finite3(fallback))
    {
        memcpy(out, fallback, sizeof(float) * 3);
        return true;
    }
    return false;
}

// A framed pending impact and the reticle drawn for its frame: one sample.
void Halo3AsSeenEvaluate(Halo3AsSeenPending& p, const Halo3AsSeenReticle& r)
{
    auto& s = g_halo3AsSeen;
    const Halo3AsSeenFrame& frame = p.frame;
    float drawn[3]{};
    if (!Halo3AsSeenReticleWorld(frame, r.dir, drawn))
    {
        ++s.window.noLine;
        return;
    }
    // The same direction through the aim path's map: what the aim assumed the reticle meant.
    float mapped[3]{};
    if (shot_impact::TrackingDirToWorld(r.dir, g_gameYawRef, g_headYawRef, g_yawSign.load(), g_pitchSign.load(),
                                        mapped))
    {
        const float deg = shot_impact::AngleDeg(drawn, mapped);
        if (std::isfinite(deg))
        {
            ++s.mapN;
            s.mapSumDeg += deg;
            s.mapMaxDeg = deg > s.mapMaxDeg ? deg : s.mapMaxDeg;
            const float keyDelta[3]{r.center[0] - frame.key[0], r.center[1] - frame.key[1],
                                    r.center[2] - frame.key[2]};
            s.originSumMm += shot_lag::Length(keyDelta) * 1000.0f;
        }
    }
    float vel[3]{};
    const bool velKnown = Halo3AsSeenEyeVel(frame, p.aim.velValid ? p.aim.vel : nullptr, vel);
    const float* v = velKnown ? vel : nullptr;
    as_seen::Sample sample{};
    sample.seen = as_seen::Measure(p.impact, frame.head, drawn, v);
    sample.stockValid = shot_lag::Finite3(frame.aimFwd);
    if (sample.stockValid)
        sample.stock = as_seen::Measure(p.impact, frame.eye, frame.aimFwd, v);
    sample.atFireValid = p.aim.valid;
    if (sample.atFireValid)
        sample.atFire = as_seen::Measure(p.impact, p.aim.head, p.aim.dir, v);
    sample.aimCamUs = p.aim.camUs;
    sample.fireUs = p.aim.fireUs;
    sample.impactUs = p.impactUs;
    sample.frameCamUs = frame.stampUs;
    sample.presentUs = r.stampUs;   // VR: the compositor building this serial's layers (its submit), not a photon
    // Ticks from the round's first query to its hit -- its own queries, counted the same way in the flat probe (the
    // sim tick counter, p.impactTick - p.aim.fireTick, counts the local unit's control passes instead).
    sample.ticksFireToImpact = p.aim.queries ? static_cast<int32_t>(p.aim.queries - 1) : -1;
    sample.ticksAimToFrame = p.aim.camTick >= 0 ? static_cast<int32_t>(int64_t(frame.tick) - p.aim.camTick) : -1;
    if (p.aim.eyeValid)
    {
        const float moved[3]{frame.eye[0] - p.aim.eye[0], frame.eye[1] - p.aim.eye[1], frame.eye[2] - p.aim.eye[2]};
        sample.eyeMovedAlongMm = as_seen::AlongTravelMm(moved, drawn, v, &sample.eyeMovedKnown);
    }
    s.window.Add(sample, g_config.crosshair_size_deg * 0.5f);
}

// Free a pending slot.
void Halo3AsSeenRelease(Halo3AsSeenPending& p)
{
    if (p.used)
        g_halo3AsSeen.pendingCount.fetch_sub(1, std::memory_order_acq_rel);
    p = Halo3AsSeenPending{};
}
} // namespace

// The fire detour's entry and exit (Halo3IndependentFireDetour). A fire inside a fire is not measured.
void Halo3ImpactFireBegin(uint8_t predicted)
{
    if (!g_halo3Impact.ready.load(std::memory_order_acquire))
        return;
    auto& fire = t_halo3ImpactFire;
    if (fire.open)
    {
        AcquireSRWLockExclusive(&g_halo3Impact.lock);
        ++g_halo3Impact.nested;
        ReleaseSRWLockExclusive(&g_halo3Impact.lock);
        return;
    }
    fire = Halo3ImpactFire{};
    fire.open = true;
    fire.beginUs = Sight_NowUs();
    fire.predicted = predicted ? 1 : 0;
    t_halo3AssistShot.lastValid = false;   // this fire's assist output, once it runs
}

void Halo3ImpactFireEnd()
{
    auto& fire = t_halo3ImpactFire;
    if (!fire.open)
        return;
    if (fire.local && fire.lineSeen)
    {
        AcquireSRWLockExclusive(&g_halo3Impact.lock);
        int seat = -1;
        auto& window = Halo3ImpactFireWindow(fire, Sight_NowUs(), seat);
        window.AddFire(fire.projectiles);
        if (fire.compensated)
            ++window.compensated;
        ReleaseSRWLockExclusive(&g_halo3Impact.lock);
    }
    fire = Halo3ImpactFire{};
}

// The fire's shot line as it leaves the shot-line call (0x368B97): what we handed the game. `muzzle`: the muzzle
// lane's ray. The local player's fire only: on foot his own weapon (the gun's owner is the unit itself); SEATED (his
// unit's parent link is set) any gun the shot line names him the shooter of -- the vehicle's or turret's own gun
// (its owner is the vehicle) or a passenger's personal weapon (its owner is the unit).
void Halo3ImpactNoteShotLine(uint32_t unit, const float origin[3], const float direction[3], bool muzzle)
{
    auto& fire = t_halo3ImpactFire;
    if (!fire.open || fire.lineSeen || !origin || !direction)
        return;
    fire.lineSeen = true;
    fire.local = t_halo3Fire == shot_lag::kFireLocal && unit != UINT32_MAX;
    if (!fire.local || !shot_lag::Finite3(origin) || !shot_lag::Finite3(direction))
    {
        fire.local = false;
        return;
    }
    const uint32_t owner = Halo3WeaponOwner(g_halo3FiringWeapon);
    uint32_t parent = UINT32_MAX;
    if (const auto* occupant = Halo3ImpactObjectData(unit))
        memcpy(&parent, occupant + kHalo3ObjectParentOffset, sizeof(parent));
    fire.seated = parent != UINT32_MAX;
    if (!fire.seated && owner != unit)
    {
        fire.local = false;   // on foot, a gun he does not hold: not his shot to judge
        return;
    }
    fire.weaponDef = Halo3ImpactDefinition(g_halo3FiringWeapon);
    if (fire.seated)
        fire.seat = shot_impact::SeatKey{Halo3ImpactDefinition(owner), fire.weaponDef, owner == unit};
    fire.muzzle = muzzle;
    memcpy(fire.lineOrigin, origin, sizeof(fire.lineOrigin));
    memcpy(fire.lineDir, direction, sizeof(fire.lineDir));
    const uint64_t nowUs = Sight_NowUs();
    fire.pointKnown = Halo3ImpactReadPoint(nowUs, fire.seated, fire.point, fire.transport, fire.pointT, fire.shiftT);
    auto& w = g_halo3Impact;
    if (fire.seated)
        return;   // the SHOT RETICLE line is the on-foot reticle's
    AcquireSRWLockExclusive(&w.lock);
    const float latency = w.trigger.Fire(nowUs);
    if (latency >= 0.0f)
        w.reticle.AddTrigger(latency);
    ++w.reticle.fires;
    if (!fire.pointKnown)
        ++w.reticle.noPoint;
    else
    {
        Halo3ImpactMeasureReticle(fire.point, nowUs);
        // The circular check: does the line we handed the game pass through the point the muzzle lane built it
        // through -- P' when this fire's shot was transported (flat parity), else P?
        if (muzzle)
        {
            const bool transported =
                shot_lag::TransportApplied(fire.transport) && Sight_TransportWanted(GameTitle::Halo3, false);
            const shot_lag::ShotLagSample ours = shot_lag::MeasureShotLag(
                origin, direction, transported ? fire.pointT : fire.point.point, nullptr, nullptr);
            if (ours.valid && ours.missMm <= 1.0f)
                ++w.ourLineThroughP;
            else
                ++w.ourLineOffP;
        }
    }
    ReleaseSRWLockExclusive(&w.lock);
}

// object_new returned `handle` for a projectile the fire routine placed (`placement*`: the block before the call).
void Halo3ImpactNoteSpawn(int32_t handle, const float placementPos[3], const float placementFwd[3],
                          const float placementVel[3], uintptr_t fireRsp)
{
    auto& fire = t_halo3ImpactFire;
    auto& w = g_halo3Impact;
    ++fire.projectiles;
    float pos[3]{}, fwd[3]{}, vel[3]{};
    const bool read = handle != -1 && Halo3ImpactReadObject(static_cast<uint32_t>(handle), pos, fwd, vel);
    const bool proven = read && shot_impact::SpawnProven(placementPos, placementFwd, placementVel, pos, fwd, vel);
    // A velocity still equal to the placement's has not had the projectile's initial speed added (its constructor
    // has not run inside object_new): its direction is not the round's. The tick reads give the line instead.
    const float added[3]{vel[0] - placementVel[0], vel[1] - placementVel[1], vel[2] - placementVel[2]};
    const float addedSpeed = shot_lag::Length(added);
    const bool constructed = addedSpeed > 1.0e-4f;
    // The constructor's speed, value-proven: what it added == (object scale) x (the tag's initial speed).
    float scale = NAN, tagSpeed = NAN;
    const bool speedRead = proven && constructed && Halo3ImpactReadRoundSpeed(static_cast<uint32_t>(handle), scale,
                                                                                tagSpeed);
    const bool speedAgrees = speedRead && shot_impact::SpeedAgrees(addedSpeed, scale, tagSpeed);
    // The fire routine's inheritance, from its own frame (seated fires only: the on-foot lines stay as they were).
    bool taken = false, ownerFlag = false;
    float body[3]{}, occupant[3]{};
    const bool frame = fire.seated && Halo3ImpactReadFireFrame(fireRsp, taken, body, occupant);
    if (frame)
        ownerFlag = Halo3ImpactOwnerFlag(Halo3WeaponOwner(g_halo3FiringWeapon));
    // A seated round is judged against what the assist produced (the line our re-aim made it fly), else the shot line.
    const auto& assist = t_halo3AssistShot;
    const bool fromAssist = fire.seated && assist.lastValid && shot_lag::Finite3(assist.lastPost);
    const uint64_t nowUs = Sight_NowUs();
    AcquireSRWLockExclusive(&w.lock);
    int seat = -1;
    auto& window = Halo3ImpactFireWindow(fire, nowUs, seat);
    if (speedRead)
    {
        if (speedAgrees)
            ++window.speedProven;
        else
            ++window.speedOff;
        w.speeds.Note(fire.weaponDef, speedAgrees, scale * tagSpeed, nowUs);
    }
    const float velScale = w.units.Scale();
    if (!proven)
        ++window.unproven;
    else if (!fire.pointKnown)
        ++window.noPoint;
    else
    {
        const shot_lag::IntentPoint& p = fire.point;
        const float* eyeVel = p.eyeVelValid ? p.eyeVel : nullptr;
        if (frame)
        {
            // The placement's own velocity off its forward is exactly what the branch added (the rest of it lies
            // along the forward: 0x368BD6..0x368C14).
            float placedAlong = 0.0f;
            const bool placedKnown =
                shot_impact::LateralAlongTravel(placementVel, placementFwd, eyeVel, velScale, placedAlong);
            window.AddInheritance(taken, ownerFlag, placedKnown, placedAlong);
        }
        const bool transported = shot_lag::TransportApplied(fire.transport);
        if (constructed)
        {
            window.AddSpawn(shot_impact::MeasureSpawn(fromAssist ? assist.lastPost : fire.lineDir, fwd, vel, pos,
                                                      p.point, eyeVel, velScale));
            if (fromAssist)
                ++window.fromAssist;
            // The same round line against P' (P moved with the eye), and the shift P' - P in the player's frame.
            window.AddTransported(fire.transport,
                                  transported ? shot_lag::MeasureShotLag(pos, vel, fire.pointT, nullptr, eyeVel)
                                              : shot_lag::ShotLagSample{},
                                  transported ? shot_impact::Decompose(fire.shiftT, p.dir, eyeVel)
                                              : shot_impact::Axes{});
        }
        else
            ++window.noInitialSpeed;
        shot_impact::Tracked& t = w.tracker.Arm(nowUs);
        t.handle = static_cast<uint32_t>(handle);
        t.group = static_cast<uint8_t>(Halo3ImpactGroup(fire));
        t.seat = static_cast<int8_t>(seat);
        t.seatOwnerDef = fire.seat.ownerDef;
        t.seatWeaponDef = fire.seat.weaponDef;
        t.seatOwnGun = fire.seat.ownGun;
        t.lastUs = nowUs;
        memcpy(t.point, p.point, sizeof(t.point));
        t.pointTValid = transported;
        if (transported)
            memcpy(t.pointT, fire.pointT, sizeof(t.pointT));
        memcpy(t.view, p.dir, sizeof(t.view));
        t.eyeVelValid = p.eyeVelValid;
        if (p.eyeVelValid)
            memcpy(t.eyeVel, p.eyeVel, sizeof(t.eyeVel));
        memcpy(t.spawnPos, pos, sizeof(t.spawnPos));
        memcpy(t.spawnVel, vel, sizeof(t.spawnVel));
        memcpy(t.lastPos, pos, sizeof(t.lastPos));
        memcpy(t.lastVel, vel, sizeof(t.lastVel));
        // AS SEEN: P's line and its camera sample, for when this round's impact first appears (on foot only: the
        // seated reticle is another line).
        const ptrdiff_t index = &t - w.tracker.slot;
        if (index >= 0 && index < shot_impact::Tracker::kSlots)
        {
            Halo3AsSeenAim& a = g_halo3AsSeen.aim[index];
            a = Halo3AsSeenAim{};
            a.valid = !fire.seated;
            memcpy(a.head, p.head, sizeof(a.head));
            memcpy(a.dir, p.dir, sizeof(a.dir));
            a.eyeValid = p.pointEyeValid && shot_lag::Finite3(p.pointEye);
            memcpy(a.eye, p.pointEye, sizeof(a.eye));
            a.camUs = p.pointEyeStampUs;
            a.serial = p.serial;
            a.camTick = Halo3AsSeenTickOfSerial(p.serial);
            a.fireUs = nowUs;
            a.fireTick = g_legacySimTicks[0].load(std::memory_order_acquire);
            a.velValid = p.eyeVelValid;
            memcpy(a.vel, p.eyeVel, sizeof(a.vel));
        }
    }
    w.armed.store(w.tracker.Armed(), std::memory_order_release);
    ReleaseSRWLockExclusive(&w.lock);
}

__declspec(noinline) int32_t __fastcall Halo3ImpactObjectNewDetour(void* placement)
{
    auto& w = g_halo3Impact;
    w.callbacks.fetch_add(1, std::memory_order_acq_rel);
    int32_t result = -1;
    __try
    {
        const auto& fire = t_halo3ImpactFire;
        const bool ours = reinterpret_cast<uintptr_t>(_ReturnAddress()) == w.fireReturn && fire.open &&
                          fire.local && placement && w.ready.load(std::memory_order_acquire);
        float placementPos[3]{}, placementFwd[3]{}, placementVel[3]{};
        const bool placed = ours && Halo3ImpactCopy3(static_cast<const uint8_t*>(placement) + w.placePos, placementPos) &&
                            Halo3ImpactCopy3(static_cast<const uint8_t*>(placement) + w.placeFwd, placementFwd) &&
                            Halo3ImpactCopy3(static_cast<const uint8_t*>(placement) + w.placeVel, placementVel);
        // The fire routine's rsp at its call: our entry rsp holds its return address, its rsp is one slot above.
        const uintptr_t fireRsp = reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()) + sizeof(void*);
        if (w.original)
            result = w.original(placement);
        if (placed)
            Halo3ImpactNoteSpawn(result, placementPos, placementFwd, placementVel, fireRsp);
    }
    __finally
    {
        w.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

// The collision scheduler's detour, after the native query (Halo3CollisionResolveDetour): a tracked projectile's
// own query -- starting at its object's position, along its velocity -- that hit records its impact point.
void Halo3ImpactWitnessNoteQuery(uintptr_t caller, const float* start, const float* vector, const void* result,
                                 uint8_t hit)
{
    auto& w = g_halo3Impact;
    if (!w.armed.load(std::memory_order_acquire) || !start || !vector || !result)
        return;
    float s[3]{}, v[3]{};
    if (!Halo3ImpactCopy3(start, s) || !Halo3ImpactCopy3(vector, v))
        return;
    float fraction = NAN, point[3]{};
    bool landed = false;
    if (hit)
    {
        __try
        {
            memcpy(&fraction, static_cast<const uint8_t*>(result) + 4, sizeof(fraction));
            memcpy(point, static_cast<const uint8_t*>(result) + 8, sizeof(point));
            landed = std::isfinite(fraction) && fraction >= 0.0f && fraction <= 1.0f && shot_lag::Finite3(point);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            landed = false;
        }
    }
    AcquireSRWLockExclusive(&w.lock);
    for (auto& t : w.tracker.slot)
    {
        if (!t.armed)
            continue;
        float pos[3]{}, fwd[3]{}, vel[3]{};
        if (!Halo3ImpactReadObject(t.handle, pos, fwd, vel))
            continue;
        // Within ~3 cm of the object and 5 deg of its velocity: this projectile's own segment.
        if (!shot_impact::QueryIsProjectiles(pos, vel, s, v, 0.01f, 5.0f))
            continue;
        {
            // AS SEEN: the round's own queries, one a tick (the flat probe counts the same thing).
            const ptrdiff_t index = &t - w.tracker.slot;
            if (index >= 0 && index < shot_impact::Tracker::kSlots && g_halo3AsSeen.aim[index].valid)
                ++g_halo3AsSeen.aim[index].queries;
        }
        if (landed && !t.impact)
        {
            t.impact = true;
            memcpy(t.impactPos, point, sizeof(t.impactPos));
            t.impactCaller = static_cast<uint32_t>(caller - w.base);
            // AS SEEN: wait for the first frame rendered after this hit (Halo3AsSeenNoteFrame).
            const ptrdiff_t index = &t - w.tracker.slot;
            if (index >= 0 && index < shot_impact::Tracker::kSlots && g_halo3AsSeen.aim[index].valid)
            {
                auto& s = g_halo3AsSeen;
                Halo3AsSeenPending* slot = nullptr;
                for (auto& candidate : s.pending)
                    if (!candidate.used)
                    {
                        slot = &candidate;
                        break;
                    }
                if (!slot)
                {
                    // Full: the oldest gives way (counted).
                    slot = &s.pending[0];
                    for (auto& candidate : s.pending)
                        if (candidate.impactUs < slot->impactUs)
                            slot = &candidate;
                    Halo3AsSeenRelease(*slot);
                    ++s.evictedPending;
                }
                slot->used = true;
                s.pendingCount.fetch_add(1, std::memory_order_acq_rel);
                memcpy(slot->impact, point, sizeof(slot->impact));
                slot->impactUs = Sight_NowUs();
                slot->impactTick = g_legacySimTicks[0].load(std::memory_order_acquire);
                slot->aim = s.aim[index];
                s.aim[index].valid = false;
            }
        }
        break;
    }
    ReleaseSRWLockExclusive(&w.lock);
}

// The local unit's simulation tick (LegacySightTraceTick): re-read each tracked projectile; retire the gone.
void Halo3ImpactWitnessTick()
{
    auto& w = g_halo3Impact;
    if (!w.armed.load(std::memory_order_acquire))
        return;
    const uint64_t nowUs = Sight_NowUs();
    AcquireSRWLockExclusive(&w.lock);
    for (auto& t : w.tracker.slot)
    {
        if (!t.armed)
            continue;
        shot_impact::Window* tracked = Halo3ImpactTrackedWindow(t);
        if (!tracked)
        {
            t.armed = false;   // its seat's window went to another seat: nothing left to credit
            continue;
        }
        auto& window = *tracked;
        const float* eyeVel = t.eyeVelValid ? t.eyeVel : nullptr;
        float pos[3]{}, fwd[3]{}, vel[3]{};
        if (!Halo3ImpactReadObject(t.handle, pos, fwd, vel))
        {
            if (t.impact)
            {
                const float offset[3]{t.impactPos[0] - t.point[0], t.impactPos[1] - t.point[1],
                                      t.impactPos[2] - t.point[2]};
                window.AddImpact(shot_impact::Decompose(offset, t.view, eyeVel), t.impactCaller);
                if (t.pointTValid)
                {
                    const float offsetT[3]{t.impactPos[0] - t.pointT[0], t.impactPos[1] - t.pointT[1],
                                           t.impactPos[2] - t.pointT[2]};
                    window.AddImpactTransported(shot_impact::Decompose(offsetT, t.view, eyeVel));
                }
            }
            else
            {
                ++window.goneWithoutImpact;
                if (!t.samples)
                    ++window.goneBeforeTick;
            }
            t.armed = false;
            continue;
        }
        const bool moved = pos[0] != t.lastPos[0] || pos[1] != t.lastPos[1] || pos[2] != t.lastPos[2];
        if (moved)
        {
            // The stored velocity's unit: this step over the wall time since the last read, against the speed.
            const float step[3]{pos[0] - t.lastPos[0], pos[1] - t.lastPos[1], pos[2] - t.lastPos[2]};
            if (t.lastUs && nowUs > t.lastUs)
                w.units.Add(shot_lag::Length(step), float(nowUs - t.lastUs) * 1.0e-6f, shot_lag::Length(t.lastVel));
            t.lastUs = nowUs;
        }
        if (t.samples < shot_impact::Tracker::kSamples && moved)
        {
            ++t.samples;
            window.AddTick(shot_lag::MeasureShotLag(pos, vel, t.point, nullptr, eyeVel),
                           shot_impact::OffLineMm(t.spawnPos, t.spawnVel, pos));
        }
        memcpy(t.lastPos, pos, sizeof(t.lastPos));
        memcpy(t.lastVel, vel, sizeof(t.lastVel));
        if (nowUs - t.armedUs > 3000000ull)
        {
            ++window.expired;
            t.armed = false;
        }
    }
    w.armed.store(w.tracker.Armed(), std::memory_order_release);
    ReleaseSRWLockExclusive(&w.lock);
}

void Halo3ImpactNoteReticle(uint64_t serial, const float center[3], const float dir[3], float rangeM, bool hit)
{
    if (!g_halo3Impact.ready.load(std::memory_order_acquire) || !center || !dir)
        return;
    auto& r = g_halo3ImpactReticle;
    AcquireSRWLockExclusive(&r.lock);
    r.valid = true;
    r.serial = serial;
    r.stampUs = Sight_NowUs();
    memcpy(r.center, center, sizeof(r.center));
    memcpy(r.dir, dir, sizeof(r.dir));
    r.rangeM = rangeM;
    r.hit = hit;
    const uint64_t stampUs = r.stampUs;
    ReleaseSRWLockExclusive(&r.lock);

    // AS SEEN: keep this serial's reticle, and judge every impact whose first frame it is.
    auto& w = g_halo3Impact;
    auto& s = g_halo3AsSeen;
    AcquireSRWLockExclusive(&w.lock);
    Halo3AsSeenReticle& kept = s.reticles[s.reticleNext++ % Halo3AsSeen::kReticles];
    kept.valid = true;
    kept.serial = serial;
    kept.stampUs = stampUs;
    memcpy(kept.center, center, sizeof(kept.center));
    memcpy(kept.dir, dir, sizeof(kept.dir));
    if (s.pendingCount.load(std::memory_order_acquire))
        for (auto& p : s.pending)
        {
            if (!p.used || !p.framed)
                continue;
            if (p.frame.serial == serial)
            {
                Halo3AsSeenEvaluate(p, kept);
                Halo3AsSeenRelease(p);
            }
            else if (p.frame.serial + 3 < serial)
            {
                ++s.window.noLine;   // its frame drew no traced reticle (off, a seat, a miss of the gate)
                Halo3AsSeenRelease(p);
            }
        }
    ReleaseSRWLockExclusive(&w.lock);
}

// The render thread, once per keyed render (CamCopyHook): the first render of each prepared serial is noted. An impact
// that hit before this render is first shown by it; its reticle arrives with the compositor's layer for the serial.
void Halo3AsSeenNoteFrame(const float head[3], const float eye[3], const float fwd[3], const float up[3],
                          const float aimFwd[3], const float headKey[3], const float headQuat[4])
{
    auto& w = g_halo3Impact;
    if (!w.ready.load(std::memory_order_acquire) || !head || !eye || !fwd || !up || !aimFwd || !headKey || !headQuat)
        return;
    const uint64_t serial = VR_CurrentPreparedSerial();
    static thread_local uint64_t s_lastSerial = 0;
    if (!serial || serial == s_lastSerial)
        return;
    s_lastSerial = serial;
    const uint64_t nowUs = Sight_NowUs();
    auto& s = g_halo3AsSeen;
    AcquireSRWLockExclusive(&w.lock);
    Halo3AsSeenFrame& f = s.frames[s.frameNext++ % Halo3AsSeen::kFrames];
    f.valid = true;
    f.serial = serial;
    f.stampUs = nowUs;
    f.tick = g_legacySimTicks[0].load(std::memory_order_acquire);
    memcpy(f.head, head, sizeof(f.head));
    memcpy(f.eye, eye, sizeof(f.eye));
    memcpy(f.fwd, fwd, sizeof(f.fwd));
    memcpy(f.up, up, sizeof(f.up));
    memcpy(f.aimFwd, aimFwd, sizeof(f.aimFwd));
    memcpy(f.key, headKey, sizeof(f.key));
    memcpy(f.quat, headQuat, sizeof(f.quat));
    s.census.Add(eye, nowUs);
    if (s.pendingCount.load(std::memory_order_acquire))
        for (auto& p : s.pending)
        {
            if (!p.used || p.framed || p.impactUs > nowUs)
                continue;
            p.framed = true;
            p.frame = f;
            // Its reticle may already be here (the compositor ran first).
            for (const auto& r : s.reticles)
                if (r.valid && r.serial == serial)
                {
                    Halo3AsSeenEvaluate(p, r);
                    Halo3AsSeenRelease(p);
                    break;
                }
        }
    ReleaseSRWLockExclusive(&w.lock);
}

// THE COMPENSATION (dev key seated_inherit_compensation, default 0; LegacySightReaimCrosshairQuery calls it for a
// Halo 3 SEATED crosshair query it is about to re-aim through P). Inside the local seated fire's own assist only: the
// fire routine has already decided the inheritance (its flag byte, 0x368BC8) and fetched the body velocity
// (0x368BC3), and the shot line has reported the shooter's velocity (0x368B92), all in its frame; the round's speed
// is the weapon's value-proven one (SpeedTable). Then `out` = the point that makes u(d) = d * Vp + inheritance point
// at P from the shot's origin (shot_impact::CompensatedPoint), and the assist turns the shot toward it. False leaves
// the re-aim through P exactly as before.
bool Halo3SeatedInheritPoint(const float point[3], float out[3])
{
    auto& w = g_halo3Impact;
    if (!g_config.seated_inherit_compensation || !w.ready.load(std::memory_order_acquire) || !point || !out)
        return false;
    const auto& shot = t_halo3AssistShot;
    auto& fire = t_halo3ImpactFire;
    if (!shot.active || !fire.open || !fire.local || !fire.seated)
        return false;
    bool taken = false;
    float body[3]{}, occupant[3]{};
    const bool frame = Halo3ImpactReadFireFrame(shot.fireRsp, taken, body, occupant);
    float speed = 0.0f;
    bool speedKnown = false;
    if (frame && taken)
    {
        AcquireSRWLockShared(&w.lock);
        speedKnown = w.speeds.Lookup(fire.weaponDef, speed);
        ReleaseSRWLockShared(&w.lock);
    }
    float residual = NAN;
    const bool applied = frame && taken && speedKnown &&
                         shot_impact::CompensatedPoint(shot.origin, point, body, occupant, speed, out, &residual);
    AcquireSRWLockExclusive(&w.lock);
    if (!frame)
        ++w.compNoFrame;
    else if (!taken)
        ++w.compNoInherit;
    else if (!speedKnown)
        ++w.compNoSpeed;
    else if (!applied)
        ++w.compNoSolution;
    else
    {
        ++w.compApplied;
        const float shift[3]{out[0] - point[0], out[1] - point[1], out[2] - point[2]};
        const float mm = shot_lag::Length(shift) * shot_lag::kMmPerWu;
        w.compShiftMmSum += mm;
        w.compShiftMmMax = mm > w.compShiftMmMax ? mm : w.compShiftMmMax;
        if (std::isfinite(residual) && residual > w.compResidualMax)
            w.compResidualMax = residual;
    }
    ReleaseSRWLockExclusive(&w.lock);
    fire.compensated = fire.compensated || applied;
    return applied;
}

void Halo3ImpactNoteTrigger(bool pressed)
{
    if (!g_halo3Impact.ready.load(std::memory_order_acquire))
        return;
    AcquireSRWLockExclusive(&g_halo3Impact.lock);
    g_halo3Impact.trigger.Note(pressed, Sight_NowUs());
    ReleaseSRWLockExclusive(&g_halo3Impact.lock);
}

bool RemoveHalo3ImpactWitness()
{
    auto& w = g_halo3Impact;
    w.ready.store(false, std::memory_order_release);
    if (!w.target)
        return true;
    const auto status = MCCVR_DisableHookForRetirement(w.target);
    if (status != MH_OK && status != MH_ERROR_DISABLED && status != MH_ERROR_NOT_CREATED)
    {
        LOG("SHOT IMPACT Halo 3 witness CleanupRequired: the hook did not disable");
        return false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&Halo3ImpactObjectNewDetour)};
    const void* trampolines[]{reinterpret_cast<const void*>(w.original)};
    if (!WaitForNativeDetourQuiescence(functions, trampolines, 1, w.callbacks))
    {
        LOG("SHOT IMPACT Halo 3 witness CleanupRequired: the witness is busy");
        return false;
    }
    const auto removed = MH_RemoveHook(w.target);
    if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
    {
        LOG("SHOT IMPACT Halo 3 witness CleanupRequired: the hook was not removed");
        return false;
    }
    w.target = nullptr;
    w.original = nullptr;
    AcquireSRWLockExclusive(&w.lock);
    w.tracker = shot_impact::Tracker{};
    w.armed.store(0, std::memory_order_release);
    ReleaseSRWLockExclusive(&w.lock);
    return true;
}

namespace
{
// The function pdata places `address` in (its entry), or 0.
uintptr_t Halo3ImpactFunctionOf(uintptr_t address)
{
    DWORD64 imageBase = 0;
    const auto* entry = RtlLookupFunctionEntry(static_cast<DWORD64>(address), &imageBase, nullptr);
    return entry ? static_cast<uintptr_t>(imageBase) + entry->BeginAddress : 0;
}

uintptr_t Halo3ImpactUnique(uintptr_t base, size_t size, const char* pattern)
{
    const uintptr_t hit = sig::Find(base, size, pattern);
    if (!hit || sig::Find(hit + 1, base + size - hit - 1, pattern))
        return 0;
    return hit;
}

// Does the type-table slot at `slot` point at a definition whose first field names `name`?
bool Halo3ImpactTypeNamed(uintptr_t slot, const char* name)
{
    __try
    {
        const auto* definition = *reinterpret_cast<const uint8_t* const*>(slot);
        const char* text = definition ? *reinterpret_cast<const char* const*>(definition) : nullptr;
        return text && strncmp(text, name, 16) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Does the type definition the table slot points at hold `function` among its first 0x100 bytes (its callbacks)?
bool Halo3ImpactTypeHolds(uintptr_t slot, uintptr_t function)
{
    __try
    {
        const auto* definition = *reinterpret_cast<const uint8_t* const*>(slot);
        if (!definition)
            return false;
        for (size_t at = 0; at < 0x100; at += sizeof(uintptr_t))
            if (*reinterpret_cast<const uintptr_t*>(definition + at) == function)
                return true;
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

template <typename T>
T Halo3ImpactAt(uintptr_t address)
{
    T value{};
    memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

// THE FIRE ROUTINE'S FRAME (seated inheritance). Every displacement decoded from the bytes, each site unique and
// inside the fire routine (pdata), and the three sites must name the same slots:
//   prologue (the dual lane's pinned entry): 7 pushes, lea rbp, [rax+D]; mov eax, S; call __chkstk; sub rsp, rax
//     -> rbp - rsp = 7*8 + S + D (0x100 in this build);
//   0x368B89: lea r9, [rbp+V]; ...; call <the shot line>; mov eax, [rbx+F]; mov r15d, 1; shr eax, B
//     -> the shooter's velocity output V (0x28) and the owner flag F bit B (+0xA8, bit 3);
//   0x368BAE: lea rdx, [rbp+Y]; call <body velocity>; mov [rsp+G], r15b; test al, al; jne; mov [rsp+G], dil
//     -> the body velocity Y (0x98) and the inheritance flag G (0x45);
//   0x369419: cmp [rsp+G], dil; je; movss xmm3, [rbp+Y+4]; movss xmm2, [rbp+Y]; ...; movss xmm4, [rbp+Y+8]
//     -> the consumer reads the same flag and the same three floats.
void Halo3ImpactProveFrame(uintptr_t base, size_t size, uintptr_t fireEntry)
{
    auto& w = g_halo3Impact;
    w.frameProven = false;
    static constexpr char kOccupant[] =
        "4C 8D 4D ?? 4C 89 5C 24 20 E8 ?? ?? ?? ?? 8B 83 ?? ?? 00 00 41 BF 01 00 00 00 C1 E8 ??";
    static constexpr char kBody[] =
        "40 88 7C 24 20 4C 8D 85 ?? ?? 00 00 8B CE 48 8D 95 ?? ?? 00 00 E8 ?? ?? ?? ?? 44 88 7C 24 ?? 84 C0 75 05 40 "
        "88 7C 24 ??";
    static constexpr char kConsumer[] =
        "40 38 7C 24 ?? 0F 84 ?? ?? ?? ?? F3 0F 10 9D ?? ?? 00 00 F3 0F 10 95 ?? ?? 00 00 0F 28 EB F3 0F 10 A5 ?? ?? 00 "
        "00";
    const uintptr_t occupant = Halo3ImpactUnique(base, size, kOccupant);
    const uintptr_t body = Halo3ImpactUnique(base, size, kBody);
    const uintptr_t consumer = Halo3ImpactUnique(base, size, kConsumer);
    const auto* p = reinterpret_cast<const uint8_t*>(fireEntry);
    static constexpr uint8_t kPushes[]{0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
    const bool prologue = fireEntry && memcmp(p + 18, kPushes, sizeof(kPushes)) == 0 && p[29] == 0x48 &&
                          p[30] == 0x8D && p[31] == 0xA8 && p[36] == 0xB8 && p[41] == 0xE8 && p[46] == 0x48 &&
                          p[47] == 0x2B && p[48] == 0xE0;
    const bool sites = occupant && body && consumer && Halo3ImpactFunctionOf(occupant) == fireEntry &&
                       Halo3ImpactFunctionOf(body) == fireEntry && Halo3ImpactFunctionOf(consumer) == fireEntry;
    if (!prologue || !sites)
    {
        LOG("SHOT IMPACT Halo 3 witness: the fire routine's frame did not prove (prologue %d, sites %d/%d/%d inside it "
            "%d); seated rounds are measured without their inheritance, and the compensation stays off",
            prologue ? 1 : 0, occupant ? 1 : 0, body ? 1 : 0, consumer ? 1 : 0, sites ? 1 : 0);
        return;
    }
    const int64_t rbpFromEntry = Halo3ImpactAt<int32_t>(fireEntry + 32);
    const int64_t frame = Halo3ImpactAt<uint32_t>(fireEntry + 37);
    const int64_t delta = 7 * 8 + frame + rbpFromEntry;
    // The shot line the occupant site calls must be the dual lane's (0x3524B0).
    const uintptr_t lineCall = occupant + 14 + static_cast<intptr_t>(Halo3ImpactAt<int32_t>(occupant + 10));
    const uint8_t occupantDisp = Halo3ImpactAt<uint8_t>(occupant + 3);
    const uint32_t ownerFlagDisp = Halo3ImpactAt<uint32_t>(occupant + 16);
    const uint8_t ownerBit = Halo3ImpactAt<uint8_t>(occupant + 28);
    const uint32_t bodyDisp = Halo3ImpactAt<uint32_t>(body + 17);
    const uint8_t flag = Halo3ImpactAt<uint8_t>(body + 30);
    const bool agree = Halo3ImpactAt<uint8_t>(body + 39) == flag && Halo3ImpactAt<uint8_t>(consumer + 4) == flag &&
                       Halo3ImpactAt<uint32_t>(consumer + 23) == bodyDisp &&
                       Halo3ImpactAt<uint32_t>(consumer + 15) == bodyDisp + 4 &&
                       Halo3ImpactAt<uint32_t>(consumer + 34) == bodyDisp + 8 &&
                       lineCall == reinterpret_cast<uintptr_t>(g_halo3Dual.aimTarget) && occupantDisp < 0x80 &&
                       ownerBit < 32 && delta > 0 && delta < 0x1000 && bodyDisp < 0x1000 && ownerFlagDisp < 0x1000;
    if (!agree)
    {
        LOG("SHOT IMPACT Halo 3 witness: the fire routine's frame sites disagree (flag +0x%X, body +0x%X, rbp-rsp "
            "0x%llX, shot line call %s); seated inheritance unmeasured, compensation off", flag, bodyDisp,
            static_cast<long long>(delta), lineCall == reinterpret_cast<uintptr_t>(g_halo3Dual.aimTarget) ? "ok" : "MOVED");
        return;
    }
    w.rbpDelta = static_cast<uint32_t>(delta);
    w.flagDisp = flag;
    w.bodyDisp = bodyDisp;
    w.occupantDisp = occupantDisp;
    w.ownerFlagDisp = ownerFlagDisp;
    w.ownerFlagBit = ownerBit;
    w.frameProven = true;
    LOG("SHOT IMPACT Halo 3 witness (dev): the fire routine's frame: rbp = rsp + 0x%X; inheritance flag [rsp+0x%X], body "
        "velocity [rbp+0x%X] (+0x%llX/+0x%llX/+0x%llX), shooter velocity [rbp+0x%X], owner flag +0x%X bit %u",
        w.rbpDelta, flag, bodyDisp, static_cast<unsigned long long>(body - base),
        static_cast<unsigned long long>(consumer - base), static_cast<unsigned long long>(occupant - base),
        occupantDisp, ownerFlagDisp, ownerBit);
}

// THE ROUND'S SPEED. The projectile constructor (a callback of the "projectile" type definition) adds
//   forward x (object +S) x (tag +T)
// to the object's velocity: 0x384E98 `movss xmm3, [rbx+S]` (S = 0x1B8), 0x384FA0 `mulss xmm3, [r9+T]` (T = 0x23C, r9 =
// the projectile's tag, the instance-table walk of Halo3LoadedTagDefinition) followed by the forward x/velocity x
// multiply-add, whose displacements must be the object's forward and velocity the placement copy proved. Every round
// then re-proves it (SpeedAgrees).
void Halo3ImpactProveSpeed(uintptr_t base, size_t size, uintptr_t typeSlot)
{
    auto& w = g_halo3Impact;
    w.speedAnchors = false;
    static constexpr char kScale[] = "F3 0F 10 9B ?? ?? 00 00 0F 28 C3 41 B8 C8 00 00 00 F3 0F 59 C4 F3 0F 11 83";
    static constexpr char kSpeed[] =
        "F3 41 0F 59 99 ?? ?? 00 00 48 8D 53 1C 66 44 89 64 24 ?? B1 01 0F 57 D2 0F 28 C3 0F 28 CB F3 0F 59 43 ?? F3 "
        "0F 58 43 ?? F3 0F 11 43 ??";
    const uintptr_t scale = Halo3ImpactUnique(base, size, kScale);
    const uintptr_t speed = Halo3ImpactUnique(base, size, kSpeed);
    const uintptr_t constructor = speed ? Halo3ImpactFunctionOf(speed) : 0;
    const bool ok = scale && speed && constructor && Halo3ImpactFunctionOf(scale) == constructor &&
                    Halo3ImpactTypeHolds(typeSlot, constructor) &&
                    Halo3ImpactAt<uint8_t>(speed + 34) == w.objFwd && Halo3ImpactAt<uint8_t>(speed + 39) == w.objVel &&
                    Halo3ImpactAt<uint8_t>(speed + 44) == w.objVel;
    if (!ok)
    {
        LOG("SHOT IMPACT Halo 3 witness: the round's speed did not prove (scale %d, speed %d, one function %d, the "
            "projectile type's constructor %d); no speed check, compensation off", scale ? 1 : 0, speed ? 1 : 0,
            constructor && scale && Halo3ImpactFunctionOf(scale) == constructor ? 1 : 0,
            constructor && Halo3ImpactTypeHolds(typeSlot, constructor) ? 1 : 0);
        return;
    }
    w.scaleDisp = Halo3ImpactAt<uint32_t>(scale + 4);
    w.tagSpeedDisp = Halo3ImpactAt<uint32_t>(speed + 5);
    w.speedAnchors = w.scaleDisp < 0x1000 && w.tagSpeedDisp < 0x1000;
    LOG("SHOT IMPACT Halo 3 witness (dev): the round's speed = object +0x%X x tag +0x%X (constructor +0x%llX)",
        w.scaleDisp, w.tagSpeedDisp, static_cast<unsigned long long>(constructor - base));
}
} // namespace

// After the dual lane (its fire routine's entry is the one this call site must lie in). Its own transaction: failing
// it costs only the two lines.
void InstallHalo3ImpactWitness(uintptr_t base, size_t size)
{
    auto& w = g_halo3Impact;
    if (w.target)
    {
        LOG("SHOT IMPACT Halo 3 witness: a previous hook has not retired; no impact line");
        return;
    }
    w.ready.store(false, std::memory_order_release);
    static constexpr char kObjectNew[] =
        "48 8B C4 53 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 78 83 CD FF 45 33 E4 F6 41 18 10 48 8B F1 8B FD 44 88 "
        "60 10";
    // object_new's copy of the placement block: movsd/mov pairs for position, forward, up and velocity.
    static constexpr char kPlacementCopy[] =
        "F2 0F 10 46 ?? F2 0F 11 43 ?? 8B 46 ?? 89 43 ?? F2 0F 10 46 ?? F2 0F 11 43 ?? 8B 46 ?? 89 43 ?? F2 0F 10 46 ?? "
        "F2 0F 11 43 ?? 8B 46 ?? 89 43 ?? F2 0F 10 46 ?? F2 0F 11 43 ?? 8B 46 ?? 89 43 ??";
    // The fire routine's projectile creation: lea rcx, [rbp+placement]; call object_new; ...; mov [rsp+..], eax.
    static constexpr char kFireCall[] =
        "48 8D 8D ?? ?? 00 00 E8 ?? ?? ?? ?? F3 44 0F 10 4C 24 ?? 41 83 C9 FF 89 44 24 ?? 41 3B C1 0F 84";
    // object_new's type-table load: lea rbx, [image base]; movsx rax, word [r15]; mov r13, r12; mov rbx, [rbx+rax*8+T].
    static constexpr char kTypeTable[] = "48 8D 1D ?? ?? ?? ?? 49 0F BF 07 4D 8B EC 48 8B 9C C3 ?? ?? ?? ??";
    const uintptr_t entry = Halo3ImpactUnique(base, size, kObjectNew);
    const uintptr_t copy = Halo3ImpactUnique(base, size, kPlacementCopy);
    const uintptr_t call = Halo3ImpactUnique(base, size, kFireCall);
    const uintptr_t table = Halo3ImpactUnique(base, size, kTypeTable);
    const uintptr_t fireEntry = reinterpret_cast<uintptr_t>(g_halo3Dual.fireTarget);
    if (!entry || !copy || !call || !table || !fireEntry)
    {
        LOG("SHOT IMPACT Halo 3 witness: an anchor did not prove (object_new %d, its placement copy %d, the fire's "
            "call %d, the type table %d, the dual lane's fire routine %d); no impact line",
            entry ? 1 : 0, copy ? 1 : 0, call ? 1 : 0, table ? 1 : 0, fireEntry ? 1 : 0);
        return;
    }
    int32_t rel = 0, tableRel = 0;
    uint32_t tableRva = 0;
    memcpy(&rel, reinterpret_cast<const void*>(call + 8), sizeof(rel));
    memcpy(&tableRel, reinterpret_cast<const void*>(table + 3), sizeof(tableRel));
    memcpy(&tableRva, reinterpret_cast<const void*>(table + 18), sizeof(tableRva));
    const auto* c = reinterpret_cast<const uint8_t*>(copy);
    const uint32_t placePos = c[4], objPos = c[9], placePosZ = c[0x0C], objPosZ = c[0x0F];
    const uint32_t placeFwd = c[0x14], objFwd = c[0x19];
    const uint32_t placeVel = c[0x34], objVel = c[0x39];
    const bool copyCoherent = placePosZ == placePos + 8 && objPosZ == objPos + 8 && placeFwd == placePos + 0xC &&
                              objFwd == objPos + 0xC && placeVel == placePos + 0x24 && objVel == objPos + 0x24;
    const bool inObjectNew = Halo3ImpactFunctionOf(copy) == entry && Halo3ImpactFunctionOf(table) == entry;
    const bool callsObjectNew = call + 12 + static_cast<intptr_t>(rel) == entry;
    const bool inFire = Halo3ImpactFunctionOf(call) == fireEntry;
    const bool imageBase = table + 7 + static_cast<intptr_t>(tableRel) == base && tableRva && tableRva + 6 * 8 <= size;
    if (!copyCoherent || !inObjectNew || !callsObjectNew || !inFire || !imageBase)
    {
        LOG("SHOT IMPACT Halo 3 witness: the decoded layout did not prove (placement copy coherent %d, inside "
            "object_new %d, the fire's call targets it %d, inside the fire routine %d, type table %d); no impact line",
            copyCoherent ? 1 : 0, inObjectNew ? 1 : 0, callsObjectNew ? 1 : 0, inFire ? 1 : 0, imageBase ? 1 : 0);
        return;
    }
    // The projectile type: the table entry whose definition is named "projectile" (index 5 in this build).
    uint8_t projectileType = 0xFF;
    for (uint8_t index = 0; index < 16 && projectileType == 0xFF; ++index)
        if (Halo3ImpactTypeNamed(base + tableRva + index * 8u, "projectile"))
            projectileType = index;
    if (projectileType == 0xFF)
    {
        LOG("SHOT IMPACT Halo 3 witness: no object type named \"projectile\" in the table at +0x%X; no impact line",
            tableRva);
        return;
    }
    w.base = base;
    w.fireReturn = call + 12;
    w.placePos = placePos;
    w.placeFwd = placeFwd;
    w.placeVel = placeVel;
    w.objPos = objPos;
    w.objFwd = objFwd;
    w.objVel = objVel;
    w.projectileType = projectileType;
    // Optional halves (each fails closed alone): the fire's frame (seated inheritance, the compensation) and the
    // round's speed (its check, the compensation).
    Halo3ImpactProveFrame(base, size, fireEntry);
    Halo3ImpactProveSpeed(base, size, base + tableRva + projectileType * 8u);
    AcquireSRWLockExclusive(&w.lock);
    w.tracker = shot_impact::Tracker{};
    for (auto& window : w.windows)
        window = shot_impact::Window{};
    w.seats = shot_impact::SeatWindows{};
    w.speeds = shot_impact::SpeedTable{};
    w.reticle = shot_impact::ReticleWindow{};
    w.trigger = shot_impact::TriggerLatch{};
    w.compApplied = w.compNoInherit = w.compNoSpeed = w.compNoFrame = w.compNoSolution = 0;
    w.compShiftMmSum = w.compShiftMmMax = w.compResidualMax = 0.0f;
    w.armed.store(0, std::memory_order_release);
    for (auto& p : g_halo3AsSeen.pending)
        Halo3AsSeenRelease(p);
    for (auto& a : g_halo3AsSeen.aim)
        a = Halo3AsSeenAim{};
    for (auto& f : g_halo3AsSeen.frames)
        f = Halo3AsSeenFrame{};
    for (auto& r : g_halo3AsSeen.reticles)
        r = Halo3AsSeenReticle{};
    g_halo3AsSeen.window = as_seen::Window{};
    g_halo3AsSeen.census = as_seen::CameraCensus{};
    ReleaseSRWLockExclusive(&w.lock);
    void* const target = reinterpret_cast<void*>(entry);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&Halo3ImpactObjectNewDetour),
                      reinterpret_cast<void**>(&w.original)) != MH_OK)
    {
        w.original = nullptr;
        LOG("SHOT IMPACT Halo 3 witness: the hook was not created; no impact line");
        return;
    }
    w.target = target;
    if (MCCVR_EnableHook(target) != MH_OK)
    {
        LOG("SHOT IMPACT Halo 3 witness: the hook did not enable; no impact line");
        (void)RemoveHalo3ImpactWitness();
        return;
    }
    w.ready.store(true, std::memory_order_release);
    LOG("SHOT IMPACT Halo 3 witness (dev): hooked object_new +0x%llX for the fire routine's call +0x%llX (return "
        "+0x%llX); placement +0x%X/+0x%X/+0x%X -> object +0x%X/+0x%X/+0x%X (position/forward/velocity); projectile "
        "type %u (table +0x%X)", static_cast<unsigned long long>(entry - base),
        static_cast<unsigned long long>(call + 7 - base), static_cast<unsigned long long>(call + 12 - base), placePos,
        placeFwd, placeVel, objPos, objFwd, objVel, projectileType, tableRva);
}

// Cold worker (GunAimReport, every 2 s): the windows.
void Halo3ImpactWitnessReport()
{
    auto& w = g_halo3Impact;
    shot_impact::Window windows[4];
    shot_impact::Window seatWindows[shot_impact::SeatWindows::kSeats];
    shot_impact::SeatKey seatKeys[shot_impact::SeatWindows::kSeats];
    bool seatUsed[shot_impact::SeatWindows::kSeats]{};
    shot_impact::ReticleWindow reticle;
    uint32_t nested = 0, through = 0, off = 0, evicted = 0, seatsDropped = 0;
    uint32_t compApplied = 0, compNoInherit = 0, compNoSpeed = 0, compNoFrame = 0, compNoSolution = 0;
    float compShiftSum = 0.0f, compShiftMax = 0.0f, compResidualMax = 0.0f;
    shot_impact::UnitsTrack units;
    AcquireSRWLockExclusive(&w.lock);
    for (int g = 0; g < 4; ++g)
    {
        windows[g] = w.windows[g];
        w.windows[g] = shot_impact::Window{};
    }
    for (int s = 0; s < shot_impact::SeatWindows::kSeats; ++s)
    {
        // The seat keeps its slot (a round in flight still credits it); only its counts start over.
        seatUsed[s] = w.seats.used[s];
        seatKeys[s] = w.seats.key[s];
        seatWindows[s] = w.seats.window[s];
        w.seats.window[s] = shot_impact::Window{};
    }
    seatsDropped = w.seats.dropped;
    w.seats.dropped = 0;
    compApplied = w.compApplied;
    compNoInherit = w.compNoInherit;
    compNoSpeed = w.compNoSpeed;
    compNoFrame = w.compNoFrame;
    compNoSolution = w.compNoSolution;
    compShiftSum = w.compShiftMmSum;
    compShiftMax = w.compShiftMmMax;
    compResidualMax = w.compResidualMax;
    w.compApplied = w.compNoInherit = w.compNoSpeed = w.compNoFrame = w.compNoSolution = 0;
    w.compShiftMmSum = w.compShiftMmMax = w.compResidualMax = 0.0f;
    units = w.units;
    reticle = w.reticle;
    w.reticle = shot_impact::ReticleWindow{};
    nested = w.nested;
    through = w.ourLineThroughP;
    off = w.ourLineOffP;
    evicted = w.tracker.evicted;
    w.nested = w.ourLineThroughP = w.ourLineOffP = 0;
    w.tracker.evicted = 0;
    // AS SEEN: an impact no frame showed within half a second is dropped (counted); then the window and the census.
    auto& asSeen = g_halo3AsSeen;
    {
        const uint64_t nowUs = Sight_NowUs();
        for (auto& p : asSeen.pending)
            if (p.used && nowUs > p.impactUs + 500000)
            {
                if (p.framed)
                    ++asSeen.window.noLine;
                else
                    ++asSeen.window.unframed;
                Halo3AsSeenRelease(p);
            }
    }
    const as_seen::Window seenWindow = asSeen.window;
    asSeen.window = as_seen::Window{};
    char censusClause[512]{};
    const bool censusAny = asSeen.census.Format(censusClause, sizeof(censusClause));
    asSeen.census.ResetCounts();
    const uint32_t mapN = asSeen.mapN, evictedPending = asSeen.evictedPending;
    const double mapMean = mapN ? asSeen.mapSumDeg / mapN : 0.0, mapMax = asSeen.mapMaxDeg;
    const double originMean = mapN ? asSeen.originSumMm / mapN : 0.0;
    asSeen.mapN = asSeen.evictedPending = 0;
    asSeen.mapSumDeg = asSeen.mapMaxDeg = asSeen.originSumMm = 0.0;
    ReleaseSRWLockExclusive(&w.lock);
    {
        static char seenLine[4096];   // the cold worker only
        if (seenWindow.Format("Halo 3 VR [vs the reticle quad as drawn over that frame; STOCK = the engine camera "
                              "along its aim, as flat draws it; radii = crosshair_size_deg / 2]",
                              seenLine, sizeof(seenLine)))
            LOG("%s; reticle direction as drawn vs the aim path's map: mean %.3f max %.3f deg over %u, its origin vs "
                "the frame's head sample %.1f mm%s", seenLine, mapMean, mapMax, mapN, originMean,
                evictedPending ? " (pending impacts evicted: see the count)" : "");
        if (evictedPending)
            LOG("AS SEEN Halo 3 VR (dev): %u pending impacts evicted (more than %d awaiting a frame)", evictedPending,
                Halo3AsSeen::kPending);
        if (censusAny)
            LOG("AS SEEN Halo 3 VR camera (dev; one note per prepared frame, the engine eye before head look): %s",
                censusClause);
    }
    static const char* const kLabels[4]{"Halo 3 [native; authoritative]", "Halo 3 [native; predicted]",
                                        "Halo 3 [muzzle; authoritative]", "Halo 3 [muzzle; predicted]"};
    char line[2048]{};
    for (int g = 0; g < 4; ++g)
        if (windows[g].Format(kLabels[g], line, sizeof(line)))
            LOG("%s", line);
    bool seatedAny = false;
    for (int s = 0; s < shot_impact::SeatWindows::kSeats; ++s)
    {
        if (!seatUsed[s])
            continue;
        char label[128]{};
        std::snprintf(label, sizeof(label), "Halo 3 SEATED [gun owner def 0x%08X, weapon def 0x%08X, %s]",
                      seatKeys[s].ownerDef, seatKeys[s].weaponDef,
                      seatKeys[s].ownGun ? "his own weapon" : "the vehicle's gun");
        if (seatWindows[s].Format(label, line, sizeof(line)))
        {
            LOG("%s", line);
            seatedAny = true;
        }
    }
    if (seatedAny || compApplied || compNoInherit || compNoSpeed || compNoFrame || compNoSolution || seatsDropped)
        LOG("SHOT IMPACT Halo 3 seated (dev): velocity unit %.2f wu/s per stored unit (%u steps%s); compensation key %d: "
            "applied %u (P moved mean %.0f max %.0f mm, residual max %.4f deg), no inheritance %u, no proven speed %u, "
            "frame unproven %u, no solution %u; seats dropped %u",
            static_cast<double>(units.Scale()), units.samples, units.Known() ? "" : ", NOT YET KNOWN: speeds read as wu/s",
            g_config.seated_inherit_compensation, compApplied,
            compApplied ? static_cast<double>(compShiftSum / static_cast<float>(compApplied)) : 0.0,
            static_cast<double>(compShiftMax), static_cast<double>(compResidualMax), compNoInherit, compNoSpeed,
            compNoFrame, compNoSolution, seatsDropped);
    if (reticle.Format("Halo 3", line, sizeof(line)))
        LOG("%s; our muzzle line through P %u, off it %u%s", line, through, off,
            nested || evicted ? " (nested fires or evicted projectiles: see below)" : "");
    const uint32_t faults = w.faults.exchange(0, std::memory_order_relaxed);
    if (nested || evicted || faults)
        LOG("SHOT IMPACT Halo 3 (dev): %u fires inside a fire (not measured), %u tracked projectiles evicted, %u read "
            "faults", nested, evicted, faults);
}
#endif
