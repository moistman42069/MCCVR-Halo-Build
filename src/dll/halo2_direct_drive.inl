// Included by halo2_observer_6dof.cpp inside its anonymous namespace, after CountPatternMatches. The drive's
// state and the accessors the observer reads (Halo2DriveState, g_halo2Drive, the record offsets) are
// declared earlier in that file.
//
// HALO 2 DIRECT DRIVE (aim_direct_drive, F1 > Weapon & Aim). Writing only the UNIT's aiming
// vectors (Halo2NativeAimUpdateDetour, +0x8FDF50) is downstream of player control, and falls short twice. The
// body's facing and the walk frame, which the game builds from player control's desired angles, stay
// wherever the thumbstick left them: legs up to 180 deg off the aim, and walking forward plays the
// backpedal. And nothing the game NETWORKS carries the aim, because player control's
// angles are what goes into the player_action the simulation submits. So the aim goes where Reach,
// Halo 3, ODST and Halo 4 put it: into player control's desired angles, right after the native accumulator.
// The unit-aim write stays as well, so the shot keeps following the hand without the unit's own easing.
//
// The accumulator (retail halo2.dll, identified by structure: the Halo 1 decomp's
// player_control_modify_desired_angles, E-H2-80's player_control.cpp attribution of +0x6C0DF0, and the
// "player control globals" allocation):
//   +0x6C3DF0  void (int user /*ecx*/, float dyaw /*xmm1*/, float dpitch /*xmm2*/): desired += delta, yaw
//              wrapped to [0, 2pi), pitch clamped to the record's eased limits [+0xA8, +0xAC]
//   record     *[the rip slot the prologue loads] + user * 0xB8: desired yaw +0x38, pitch +0x3C, radians;
//              yaw 0 = world +X, positive toward +Y; pitch positive up (forward = cos y cos p, sin y cos p,
//              sin p, the conversion +0x6C0DF0 tail-calls)
//   callers    the per-user control update (+0x6C0A46), the only one written after, and a per-shot recoil
//              kick (+0x6C47A0 tail-jumps in from weapon fire), which stays stock: the next control update
//              rewrites the pitch it kicked, so the aim follows the hand, not the recoil
// After the accumulator returns, the control update builds player_action from +0x38 (action+8) and submits
// it to the simulation; the per-player update then sets the unit's desired facing, aiming and looking from
// it. A write here therefore reaches the network and the body. After the control update it would be too
// late: the action is already built.
//
// ADDR-HYGIENE: resolved (src/common/drive_sites.h: kHalo2ControlAnchors, ResolveHalo2Control). No recorded RVA
// says where anything is. Five exact-byte anchors are FOUND by signature anywhere in the module's code, each
// exactly once: the entry prologue (where a function starts; it also carries the record slot's disp32), the stride
// block and the yaw and the pitch accumulators (all inside that function), and the control update's call site,
// whose rel32 must land on the found entry (its return address is the only one written after). The slot must
// resolve inside the module. Anything not found: no hook, said once, and Halo 2 keeps its unit aim (the shot
// follows the hand; the body and the network do not).
// ARM GATE (ValueAgreement): installed disarmed. It writes only after the record has held the stock
// camera's direction (the game builds that camera from the record) within 1.7 deg for 30 updates -- both
// axes on foot, yaw in a seat -- and only while each later update resolves the same record. On foot it
// writes only while the observer publishes the view at its own heading (halo2_foot_view.h): written before
// that, the stock camera would turn the view with the hand, which is unacceptable in a headset (C-H2-41). SEATED
// it writes, axis by axis, only while the seat's own view is up and the seat's own record has
// passed its checks (the seated branch below).
//
// THE SEAT'S FRAME (seat_frame_reader.h, docs/ALL-TITLES-AIM-2026-09-29.md "Seats aim in world angles").
// The per-player update hands the action's angles to 0x8F0D20 (from 0x6A40CD), which rotates them by the vehicle's
// up frame R(u) unless the occupied seat's live flags carry bit 4. No patch of ours clears that flag in Halo 2, so a
// seat without it lacks it in its own data, and every peer -- the host included -- applies R(u) to the angles we
// send: case (b). Each seated update therefore reads, on this thread and from the unit the rebase will rotate, the
// seat's flags and the vehicle's up exactly as the rebase does, and takes the game's aim (for the stint check and
// the arm gate) and the hand's aim (for the write) into the record's frame; the yaw latch and the record's own eased
// pitch limits then work in that frame too, as the accumulator's do. A frame that does not read writes nothing.

// ValueAgreement, as the other titles.
constexpr float kHalo2DriveAgreeRad = 0.03f;
constexpr uint32_t kHalo2DriveAgreeUpdates = 30;
// The native's own pitch clip, for a record whose eased limits read unusable.
constexpr float kHalo2DrivePitchClip = 1.4922565f;

// Halo yaw and pitch of a direction (world +Z up): forward = (cos y cos p, sin y cos p, sin p).
bool Halo2DriveYawPitch(const float direction[3], float& yaw, float& pitch) noexcept
{
    const float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] +
        direction[2] * direction[2]);
    if (!std::isfinite(length) || length < 1.0e-6f)
        return false;
    yaw = std::atan2(direction[1], direction[0]);
    pitch = std::asin(std::clamp(direction[2] / length, -1.0f, 1.0f));
    return std::isfinite(yaw) && std::isfinite(pitch);
}

// The local player's unit is a biped: -1 unknown (fails closed), 0 on foot, 1 seated (`parent`, `seat` set).
int Halo2DriveOccupancy(uint32_t& parent, int16_t& seat) noexcept
{
    parent = UINT32_MAX;
    seat = -1;
    if (!g_vehicleSeatVerified.load(std::memory_order_acquire))
        return -1;
    const uint32_t owned = Halo2OwnedUnit();
    if (owned == UINT32_MAX)
        return -1;
    const auto* unit = static_cast<const uint8_t*>(Halo2ObjectFromIndex(owned));
    if (!unit || unit[0xAA] != 0)
        return -1;
    seat = *reinterpret_cast<const int16_t*>(unit + kHalo2UnitParentSeatOffset);
    if (seat == -1)
        return 0;
    parent = *reinterpret_cast<const uint32_t*>(unit + 0x14);
    return seat >= 0 && parent != UINT32_MAX ? 1 : -1;
}

// The seat's frame (seat_frame_reader.h): bound with the hook, read on the sim thread each seated update.
seat_frame_rt::Binding g_halo2SeatFrame;
seat_frame_rt::Counters g_halo2SeatFrameCounters;

// AIM THE SEAT'S GUN THROUGH THE POINT (seat_barrel_logic.h). A vehicle gun's shot leaves its muzzle, and the fire
// turns it at the crosshair's point only inside the weapon's deviation angle about the barrel (SEAT CONVERGENCE,
// below). A seat aimed from its camera through the point holds its barrel parallel to that line, so its shot lands
// on the point only where the barrel's distance from the camera fits inside that angle, and beside the point nearer
// than that. Each fire of the vehicle's gun by our seat shows the barrel's line as the fire took it -- the muzzle
// marker and the direction the fire was given, before the engine's obstruction step and before its turn -- with
// the seat's eye and the vehicle's frame of the same update; from those the model learns where the barrel sits, and
// the seat's write becomes the aim for which the barrel's line runs through the point. What a seat's fires taught
// is kept when the seat is left and taken up again by the next seat of that kind of vehicle, and a gun measured
// beforehand (kHalo2MeasuredGuns) starts from its measured fit; so only the first shot from a gun that is new to
// both is aimed along the camera's line, as is any update the model declines.
// Only the direction written to player control changes -- what the title replicates -- and nothing of the shot is
// touched.
// The fire and the control update both run on the simulation's thread; the try-lock covers the case they do not
// (the loser of a race keeps the camera's line for that update, or learns nothing from that fire).
struct Halo2SeatBarrelState
{
    std::atomic_flag busy = ATOMIC_FLAG_INIT;
    uint32_t stance = 0;
    bool gunner = false;          // this seat aims its vehicle's gun (the control update's read of the seat's flags)
    struct Gun
    {
        uint32_t weapon = UINT32_MAX;
        int16_t barrel = -1;
        seat_barrel::Model model;
    } guns[2];
    int active = -1;              // the gun fired last (the other slot is the one a third gun would take)
    uint64_t identity = 0;        // the seat's vehicle (halo2_vehicle_identity; 0: not one it knows) and its seat
    int16_t seat = -1;
    // What earlier seats of this session taught, by kind of vehicle and seat (the guns by barrel: a weapon's
    // handle is another vehicle's next time).
    struct Kept
    {
        uint64_t identity = 0;
        int16_t seat = -1;
        Gun guns[2];
    } kept[6];
    uint32_t keptNext = 0;
    float eye[3]{};               // the seat's eye as the engine last answered it, and when (0: never)
    uint64_t eyeUs = 0;
    bool lastSolved = false;      // the last update's write was the barrel's
    std::atomic<uint32_t> learned{0}, aimed{0}, byEye{0}, declined{0}, noFrame{0}, staleEye{0}, othersFire{0};
    std::atomic<uint32_t> recalled{0}, measured{0};
#if HALOMCCVR_DEV_DIAGNOSTICS
    // The fire in progress, kept for the round it creates (dev).
    struct Pending
    {
        bool valid = false, pointKnown = false, barrelWrite = false;
        int16_t barrel = -1;
        uint32_t modelFires = 0;
        float point[3]{}, muzzle[3]{}, direction[3]{};
    } pending;
    // The last rounds, for the cold report (dev): what the barrel showed and what the engine made of it. An angle
    // of -1 was not measured (no point at that fire).
    struct FireNote
    {
        float rangeWu = 0, barrelOffDeg = -1, barrelMissWu = 0, turnedDeg = -1, roundOffDeg = -1, roundFromMuzzleWu = 0;
        uint32_t modelFires = 0;
        int16_t barrel = -1;
        bool barrelWrite = false;   // the write before the fire was a barrel's line (any gun of the seat)
    } notes[8];
    uint32_t noteCount = 0, noteReported = 0;
#endif
} g_halo2SeatBarrel;
// A seat's eye older than this is not this fire's (microseconds: a few control updates).
constexpr uint64_t kHalo2SeatBarrelEyeFreshUs = 100000;

// Guns measured in the game: the fit a seat's own fires reach (Model::State), with the title's default seat camera.
// A seat that aims one starts from it. By the vehicle's name (halo2_vehicle_identity) and the weapon's barrel.
struct Halo2MeasuredGun
{
    const char* vehicle;
    int16_t barrel;
    double fit[5];
};
constexpr Halo2MeasuredGun kHalo2MeasuredGuns[]{
    // Scorpion: the cannon, and the machine gun 0.54 m to its right. The turret's pivot sits 1.56 m behind and
    // 1.98 m above the driver's seat camera, on the tank's centreline.
    {"Scorpion", 0, {-0.5105, 0.0002, 0.6495, 0.0001, 0.0820}},
    {"Scorpion", 1, {-0.5107, -0.0003, 0.6617, 0.1784, 0.0760}},
};

// Sim thread, every seated control update: which seat this is (its vehicle and seat index) and whether it aims the
// vehicle's gun. On another seat the guns are put away under the seat they were learned in, and the new seat takes
// up what is kept for its kind: this session's, else a measured gun's.
void Halo2SeatBarrelNoteSeat(uint32_t stance, uint32_t parent, int16_t seat, bool gunner) noexcept
{
    auto& s = g_halo2SeatBarrel;
    if (s.busy.test_and_set(std::memory_order_acquire))
        return;
    if (s.stance != stance)
    {
        if (s.identity && (s.guns[0].model.fires() || s.guns[1].model.fires()))
        {
            auto* slot = &s.kept[s.keptNext % 6];
            for (auto& kept : s.kept)
                if (kept.identity == s.identity && kept.seat == s.seat)
                    slot = &kept;
            if (slot == &s.kept[s.keptNext % 6])
                ++s.keptNext;
            slot->identity = s.identity;
            slot->seat = s.seat;
            for (int i = 0; i < 2; ++i)
            {
                slot->guns[i] = s.guns[i];
                slot->guns[i].weapon = UINT32_MAX;
            }
        }
        for (auto& gun : s.guns)
            gun = {};
        s.active = -1;
        s.eyeUs = 0;
        s.lastSolved = false;
        s.stance = stance;
        s.seat = seat;
        s.identity = parent != UINT32_MAX && seat >= 0 ? ReadHalo2VehicleIdentity(parent) : 0;
        if (s.identity)
        {
            bool recalled = false;
            for (const auto& kept : s.kept)
                if (kept.identity == s.identity && kept.seat == s.seat)
                {
                    s.guns[0] = kept.guns[0];
                    s.guns[1] = kept.guns[1];
                    recalled = true;
                }
            if (recalled)
                s.recalled.fetch_add(1, std::memory_order_relaxed);
            else if (const char* name = halo2_vehicle_identity::Name(s.identity))
            {
                int slot = 0;
                for (const auto& gun : kHalo2MeasuredGuns)
                    if (slot < 2 && std::strcmp(gun.vehicle, name) == 0)
                    {
                        s.guns[slot].barrel = gun.barrel;
                        s.guns[slot].model.Seed(gun.fit);
                        ++slot;
                    }
                if (slot)
                    s.measured.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    s.gunner = gunner;
    s.busy.clear(std::memory_order_release);
}

// Sim thread: the seat's eye as the engine's own evaluator just answered it (Halo2BuildSeatConvergedAim: every
// seated control update with the hand aiming, and a fire's own ask for its point).
void Halo2SeatBarrelNoteEye(const float* eye) noexcept
{
    auto& s = g_halo2SeatBarrel;
    if (!eye || !seat_barrel::Finite3(eye) || s.busy.test_and_set(std::memory_order_acquire))
        return;
    std::memcpy(s.eye, eye, sizeof(s.eye));
    s.eyeUs = Sight_NowUs();
    s.busy.clear(std::memory_order_release);
}

// Our unit is seated (parent, seat) and the weapon is a gun of that vehicle: held by something other than the unit,
// under the same root. (A passenger's weapon beside us is that too: Halo2SeatBarrelOurFire tells them apart.)
bool Halo2SeatBarrelSeatedGun(uint32_t unit, uint32_t weapon, uint32_t& parent, int16_t& seat) noexcept
{
    __try
    {
        if (Halo2DriveOccupancy(parent, seat) != 1)
            return false;
        const auto* data = static_cast<const uint8_t*>(Halo2ObjectFromIndex(weapon));
        if (!data)
            return false;
        const uint32_t holder = *reinterpret_cast<const uint32_t*>(data + 0x14);
        const uint32_t root = Halo2UltimateParent(unit);
        return holder != UINT32_MAX && holder != unit && root != UINT32_MAX && Halo2UltimateParent(weapon) == root;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// The fire is our seat's: the unit the fire names is ours, or something our unit sits in (the vehicle, a turret on
// it). Another occupant firing -- a passenger's rifle, another seat's gun -- names a unit beside ours.
bool Halo2SeatBarrelOurFire(uint32_t firer, uint32_t unit) noexcept
{
    __try
    {
        uint32_t at = unit;
        for (int depth = 0; depth < 4 && at != UINT32_MAX; ++depth)
        {
            if (at == firer)
                return true;
            const auto* data = static_cast<const uint8_t*>(Halo2ObjectFromIndex(at));
            if (!data)
                return false;
            at = *reinterpret_cast<const uint32_t*>(data + 0x14);
        }
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

#if HALOMCCVR_DEV_DIAGNOSTICS
// A gun this seat has fired (the projectile witness asks whose round it sees).
bool Halo2SeatBarrelKnownGun(uint32_t weapon) noexcept
{
    auto& s = g_halo2SeatBarrel;
    if (weapon == UINT32_MAX || s.busy.test_and_set(std::memory_order_acquire))
        return false;
    const bool known = s.guns[0].weapon == weapon || s.guns[1].weapon == weapon;
    s.busy.clear(std::memory_order_release);
    return known;
}
#endif

// Fire thread: one seated fire's barrel line. `muzzle` is the fire's start as it came into the aim call -- the
// barrel's marker. (The call's own answer is not used: its obstruction step moves the start to whatever stands
// between the firing unit's camera and the muzzle, which for a first-person seat can be a step from the camera,
// and the fire then starts the round from the marker again.) `direction` is the line the fire was given: the axis
// of the cone the engine pins the round to.
void Halo2SeatBarrelLearn(uint32_t firer, uint32_t weapon, int16_t barrel, const float* muzzle,
                          const float* direction) noexcept
{
    auto& s = g_halo2SeatBarrel;
    const uint32_t unit = Halo2OwnedUnit();
    uint32_t parent = UINT32_MAX;
    int16_t seat = -1;
    if (unit == UINT32_MAX || weapon == UINT32_MAX || !muzzle || !direction ||
        !Halo2SeatBarrelSeatedGun(unit, weapon, parent, seat))
        return;
    if (!Halo2SeatBarrelOurFire(firer, unit))
    {
        s.othersFire.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    float forward[3]{}, up[3]{};
    seat_barrel::Frame hull{};
    const bool framed =
        seat_frame_rt::ReadVehicleFrame(g_halo2SeatFrame, &Halo2ObjectFromIndex, unit, forward, up) &&
        seat_barrel::MakeFrame(forward, up, hull);
    if (!framed)
        s.noFrame.fetch_add(1, std::memory_order_relaxed);
    // The seat's point now. Asking it asks the seat's eye too (Halo2BuildSeatConvergedAim), so while the seat is
    // converging the eye below is this fire's own. Both take the lock themselves: before it.
    float point[3]{};
    [[maybe_unused]] const bool pointKnown = Halo2SeatShotPoint(unit, point);
    const uint64_t now = Sight_NowUs();
    if (s.busy.test_and_set(std::memory_order_acquire))
        return;
#if HALOMCCVR_DEV_DIAGNOSTICS
    s.pending = {};
#endif
    const bool eyeFresh = s.eyeUs != 0 && now >= s.eyeUs && now - s.eyeUs <= kHalo2SeatBarrelEyeFreshUs;
    const bool seated = s.gunner && s.stance == seat_converge::StanceKey(parent, seat);
    if (framed && seated && !eyeFresh)
        s.staleEye.fetch_add(1, std::memory_order_relaxed);
    if (framed && seated && eyeFresh)
    {
        int slot = -1;
        for (int i = 0; i < 2; ++i)
            if (s.guns[i].weapon == weapon && s.guns[i].barrel == barrel)
                slot = i;
        // A gun kept from an earlier seat, or a measured one, has no weapon yet: this barrel's first fire is it.
        for (int i = 0; slot < 0 && i < 2; ++i)
            if (s.guns[i].weapon == UINT32_MAX && s.guns[i].barrel == barrel && s.guns[i].model.known())
            {
                s.guns[i].weapon = weapon;
                slot = i;
            }
        if (slot < 0)
        {
            slot = s.guns[0].weapon == UINT32_MAX ? 0 : s.guns[1].weapon == UINT32_MAX ? 1 : s.active == 0 ? 1 : 0;
            s.guns[slot] = {};
            s.guns[slot].weapon = weapon;
            s.guns[slot].barrel = barrel;
        }
        auto& gun = s.guns[slot];
#if HALOMCCVR_DEV_DIAGNOSTICS
        auto& p = s.pending;
        p.valid = true;
        p.pointKnown = pointKnown;
        p.barrel = barrel;
        p.modelFires = gun.model.fires();
        p.barrelWrite = g_config.seat_aim_through_barrel && s.lastSolved;
        std::memcpy(p.point, point, sizeof(p.point));
        std::memcpy(p.muzzle, muzzle, sizeof(p.muzzle));
        std::memcpy(p.direction, direction, sizeof(p.direction));
#endif
        if (gun.model.Learn(hull, s.eye, muzzle, direction))
        {
            s.active = slot;
            s.learned.fetch_add(1, std::memory_order_relaxed);
        }
    }
    s.busy.clear(std::memory_order_release);
}

#if HALOMCCVR_DEV_DIAGNOSTICS
// Fire thread (dev: the projectile witness is the caller, object_new from the fire routine): the round a seated
// fire created, against the barrel it left and the point it was aimed at.
void Halo2SeatBarrelNoteRound(const float spawn[3], const float forward[3], const float* velocity) noexcept
{
    auto& s = g_halo2SeatBarrel;
    if (!spawn || !forward || !seat_barrel::Finite3(spawn))
        return;
    float launch[3]{forward[0], forward[1], forward[2]};
    if (velocity && seat_barrel::Finite3(velocity))
    {
        const float speed =
            std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]);
        if (speed > 1.0e-3f)
            for (int i = 0; i < 3; ++i)
                launch[i] = velocity[i] / speed;
    }
    if (!seat_barrel::Finite3(launch) || s.busy.test_and_set(std::memory_order_acquire))
        return;
    auto& p = s.pending;
    if (p.valid)
    {
        p.valid = false;   // one round per fire
        const auto length = [](const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); };
        const auto angleDeg = [&](const float a[3], const float b[3]) {
            const float scale = length(a) * length(b);
            if (!(scale > 1.0e-9f))
                return -1.0f;
            const float dot = (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / scale;
            return std::acos(std::clamp(dot, -1.0f, 1.0f)) * 57.29578f;
        };
        auto& note = s.notes[s.noteCount % 8];
        note = {};
        note.barrel = p.barrel;
        note.modelFires = p.modelFires;
        note.barrelWrite = p.barrelWrite;
        note.turnedDeg = angleDeg(p.direction, launch);
        const float fromMuzzle[3]{spawn[0] - p.muzzle[0], spawn[1] - p.muzzle[1], spawn[2] - p.muzzle[2]};
        note.roundFromMuzzleWu = length(fromMuzzle);
        if (p.pointKnown)
        {
            const float toPoint[3]{p.point[0] - p.muzzle[0], p.point[1] - p.muzzle[1], p.point[2] - p.muzzle[2]};
            const float fromSpawn[3]{p.point[0] - spawn[0], p.point[1] - spawn[1], p.point[2] - spawn[2]};
            note.rangeWu = length(toPoint);
            note.barrelOffDeg = angleDeg(p.direction, toPoint);
            note.roundOffDeg = angleDeg(launch, fromSpawn);
            double d[3]{p.direction[0], p.direction[1], p.direction[2]};
            if (seat_barrel::Normalize(d))
            {
                const double o[3]{p.muzzle[0], p.muzzle[1], p.muzzle[2]}, at[3]{p.point[0], p.point[1], p.point[2]};
                note.barrelMissWu = static_cast<float>(seat_barrel::LineMiss(o, d, at));
            }
        }
        ++s.noteCount;
    }
    s.busy.clear(std::memory_order_release);
}
#endif

// Sim thread: `bent` (the aim from the seat's camera through `point`) becomes the aim for which the barrel's line
// runs through `point`, when a gun of this seat has fired and the model answers. Otherwise it is left as it is.
void Halo2SeatBarrelAim(uint32_t unit, uint32_t stance, const float eye[3], const float point[3],
                        float bent[3]) noexcept
{
    auto& s = g_halo2SeatBarrel;
    if (!g_config.seat_aim_through_barrel || s.busy.test_and_set(std::memory_order_acquire))
        return;
    bool solved = false;
    if (s.stance == stance && s.gunner && (s.guns[0].model.known() || s.guns[1].model.known()))
    {
        float forward[3]{}, up[3]{};
        seat_barrel::Frame hull{};
        if (!seat_frame_rt::ReadVehicleFrame(g_halo2SeatFrame, &Halo2ObjectFromIndex, unit, forward, up) ||
            !seat_barrel::MakeFrame(forward, up, hull))
            s.noFrame.fetch_add(1, std::memory_order_relaxed);
        else
        {
            // ONE aim for every gun the seat knows: the mean of their solutions. Which gun fires next is not
            // known, a tank's two barrels sit a pace apart, and the engine turns either shot at the point from
            // inside its cone; an aim for the gun fired last would swing the turret at every change of gun and
            // leave the other gun's first shot the whole pace off.
            float sum[3]{};
            int answered = 0;
            for (const auto& gun : s.guns)
            {
                float aim[3]{};
                if (gun.model.known() && gun.model.Aim(hull, eye, point, aim))
                {
                    sum[0] += aim[0];
                    sum[1] += aim[1];
                    sum[2] += aim[2];
                    ++answered;
                }
            }
            const float length = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
            if (!answered || !std::isfinite(length) || !(length > 1.0e-3f))
                s.declined.fetch_add(1, std::memory_order_relaxed);
            else
            {
                bent[0] = sum[0] / length;
                bent[1] = sum[1] / length;
                bent[2] = sum[2] / length;
                solved = true;
            }
        }
    }
    s.lastSolved = solved;
    s.busy.clear(std::memory_order_release);
    (solved ? s.aimed : s.byEye).fetch_add(1, std::memory_order_relaxed);
}

// SEAT CONVERGENCE (seat_converge_logic.h; docs/H2-CE-SEAT-CONVERGENCE-2026-10-06.md). Halo 2 aims a seated shot as
// Halo 1 does: the fire (0x8E4940) takes the weapon's barrel marker when the weapon's owner has a gunner, else the
// unit's aim, and player_aim_projectile (0x7596A0) then traces the crosshair from the DETERMINISTIC camera of the
// player's biped (0x6D4730: first person 0x77DAA0 -- the unit camera position 0x8F9190 along the unit's aiming vector
// +0x174 -- when the selector 0x6D4C50 says so, which the mod's first-person seat view makes it say), ignoring the
// aiming unit's ultimate parent (0x8D6A50), and turns the shot at what that hits -- in a seat pinned inside the
// weapon's deviation cone about the direction it came in with (0x759A89..0x759ADE; on foot it is not pinned). The
// cone's angle is the larger of the weapon tag's deviation and autoaim angles over the zoom (0x759B70; six degrees
// for a Scorpion's cannon unzoomed). The aim call (0x8F0F70) is handed the barrel's marker and may move that
// origin to an obstruction between the firing unit's camera and it; for a barrel with flag bit 3 the fire then
// takes the marker again (0x8E5026): a Scorpion's shell starts at its muzzle either way. The
// biped's aiming vector comes from player control's desired angles, which is what this drive writes and what the
// simulation networks. So the seat's write is the direction from that camera position through the traced point --
// or, for a vehicle gun that has fired, the direction for which its BARREL runs through that point (above).
//
// The eye is asked from the engine's own evaluator in this same update (Halo2CallFireEyeEvaluator: the very call
// the fire makes, call edge proven by the dual-aim install) and used only on its first-person path
// (seat_converge::SeatEye): the following camera orbits the aim it is built from.
void Halo2NoteSeat(bool identified, uint32_t stance, const seat_converge::Role& role, seat_converge::Why why) noexcept
{
    if (identified)
        Sight_NoteSeatPoll(GameTitle::Halo2, stance, role, why);
}

// The seat's intent ray from one observer publication (the rendered centre eye along the aiming hand's carrier, the
// same carrier choice as on foot). False: no usable controller publication.
bool Halo2SeatIntentRay(float head[3], float dir[3]) noexcept
{
    Halo2ObserverPosePublication publication{};
    Halo2CameraBasis rightCarrier{}, leftCarrier{};
    const uint32_t generation = g_generation.load(std::memory_order_acquire);
    float stockEye[3]{};
    return generation && Halo2Observer6Dof_ReadPublishedPose(publication) &&
        Halo2ObserverControllerSnapshotUsable(publication, generation) &&
        BuildStableFirstPersonCarriers(publication, generation, rightCarrier, leftCarrier,
            Halo2SupportCarrierMustDetach(publication)) &&
        Halo2SightGeometry(publication, rightCarrier, head, dir, stockEye);
}

// Sim thread. `direction`: the converged aim (world, unit) when the result is Converged; `point` (optional): the
// point it is bent through; `headOut` (optional): the rendered eye that point was traced from. Any other result
// names our reason and leaves them untouched.
seat_converge::Why Halo2BuildSeatConvergedAim(uint32_t unit, uint32_t stance, float direction[3],
                                              float* point, float* headOut) noexcept
{
    using seat_converge::Why;
    if (!Sight_AimEnabled())
        return Why::AimOff;
    float head[3]{}, dir[3]{};
    if (unit == UINT32_MAX || !Halo2SeatIntentRay(head, dir))
        return Why::NoHand;
    seat_converge::EyeAnswer answer{};
    auto& camera = g_halo2Dual;
    answer.bound = camera.cameraOriginal != nullptr && camera.enabled.load(std::memory_order_acquire) &&
        !camera.faulted.load(std::memory_order_acquire) &&
        camera.generation == g_generation.load(std::memory_order_acquire);
    int32_t path = -1;
    if (answer.bound)
    {
        answer.called = Halo2CallFireEyeEvaluator(unit, answer.position, path);
        answer.perspective = path;
    }
    float eye[3]{};
    const Why eyeWhy = seat_converge::SeatEye(answer, eye);
    if (eyeWhy != Why::Converged)
        return eyeWhy;
    Halo2SeatBarrelNoteEye(eye);   // the barrel's model is measured from this eye, at a fire and here alike
    if (Sight_Stance(GameTitle::Halo2) != stance)
        return Why::StanceSettling;
    float bent[3]{}, range = 0.0f;
    converge::ConvergeOff off = converge::ConvergeOff::None;
    if (!Sight_ConvergedDirection(GameTitle::Halo2, g_generation.load(std::memory_order_acquire), head, dir, eye,
                                  bent, 90.0f, &range, nullptr, &off))
        return off == converge::ConvergeOff::NoRange ? Why::NoRange : Why::NoTrace;
    if (!std::isfinite(range) || !(range > 0.0f))
        return Why::NoRange;
    const float aimPoint[3]{head[0] + dir[0] * range, head[1] + dir[1] * range, head[2] + dir[2] * range};
    Halo2SeatBarrelAim(unit, stance, eye, aimPoint, bent);
    for (int axis = 0; axis < 3; ++axis)
    {
        direction[axis] = bent[axis];
        if (point)
            point[axis] = aimPoint[axis];
        if (headOut)
            headOut[axis] = head[axis];
    }
    return Why::Converged;
}

// The first writes of a seat's yaw read no limit off the record (the stick servo may still be deflecting the
// stick until the input thread sees the seat written), as the other titles' seats.
constexpr uint32_t kHalo2SeatPrimeUpdates = 2;
// A stint survives this many consecutive updates with its seat's view down (~4 s of 30 Hz control updates).
constexpr uint32_t kHalo2StintStaleUpdates = 120;

// Sim thread: the seat stint is over (left the seat, the seat stopped qualifying, a disarm or a fault).
void Halo2DriveForgetSeat(Halo2DriveState& st) noexcept
{
    st.stint.reset();
    st.stintActive = false;
    st.stintKey = 0;
    st.stintPitchFailCounted = false;
    st.stintSample = 0;
    st.stintStaleUpdates = 0;
    st.yawLatch.reset();
    st.yawTicks = 0;
    st.seatYawMs.store(0, std::memory_order_release);
    st.seatPitchMs.store(0, std::memory_order_release);
    st.stops.clear();
    st.stintAircraft = false;
}

// An air vehicle (Halo 2's own identity of the seat's vehicle): no rotational limit there, so its seats never clamp
// the reticle. Unknown identity: not an aircraft (a stop it never shows clamps nothing).
bool Halo2SeatIsAircraft(uint32_t parent) noexcept
{
    const char* name = halo2_vehicle_identity::Name(ReadHalo2VehicleIdentity(parent));
    if (!name)
        return false;
    const auto starts = [name](const char* prefix) { return std::strncmp(name, prefix, std::strlen(prefix)) == 0; };
    return starts("Banshee") || starts("Phantom") || starts("Pelican") || starts("Falcon") || starts("Longsword");
}

void Halo2DriveApply(int user, uint64_t returnAddress) noexcept
{
    auto& st = g_halo2Drive;
    st.callbacks.fetch_add(1, std::memory_order_acq_rel);
    bool seatKept = false;    // this update continued a seat stint (its checks survive)
    bool seatWrote = false;   // ...and wrote it (the yaw latch keeps its last value only then)
    __try
    {
        do
        {
            if (user != static_cast<int>(kOwnedUser))
                break;
            if (returnAddress != st.controlReturn)
            {
                st.kicks.fetch_add(1, std::memory_order_relaxed);
                seatKept = st.stintActive;   // another caller: not an update of the seat, nor its end
                seatWrote = seatKept;
                break;
            }
            st.calls.fetch_add(1, std::memory_order_relaxed);
            // The game ran the owned user's control update (it skips this call while the director holds the
            // player: a cinematic). One reader: the head-relative motion tracker (halo2_head_relative.inl).
            st.controlMs.store(GetTickCount64(), std::memory_order_release);
            if (!g_config.aim_direct_drive || !st.installed.load(std::memory_order_acquire) || !st.slot ||
                !g_armed.load(std::memory_order_acquire) || !g_levelLive.load(std::memory_order_acquire) ||
                g_teardownRequested.load(std::memory_order_acquire))
            {
                // Switched off live (or the observer is down): stop now, and re-validate before writing again.
                if (st.armed.exchange(false, std::memory_order_acq_rel))
                    st.disarms.fetch_add(1, std::memory_order_relaxed);
                st.agree = 0;
                break;
            }
            const uintptr_t globals = *reinterpret_cast<const uintptr_t*>(st.slot);
            if (!globals)
                break;
            const uintptr_t pc = globals + static_cast<uintptr_t>(user) * kHalo2PcStride;
            float* pair = reinterpret_cast<float*>(pc + kHalo2PcDesiredYaw);
            const float nativeYaw = pair[0], nativePitch = pair[1];
            st.nativeYaw.store(nativeYaw, std::memory_order_relaxed);
            st.nativePitch.store(nativePitch, std::memory_order_relaxed);
            uint32_t seatParent = UINT32_MAX;
            int16_t seatIndex = -1;
            const int occupancy = Halo2DriveOccupancy(seatParent, seatIndex);
            const uint64_t now = GetTickCount64();
            // The game's own aim: the stock camera, which the game builds from this record.
            Halo2ObserverPosePublication publication{};
            const uint32_t generation = g_generation.load(std::memory_order_acquire);
            const bool havePublication = generation && Halo2Observer6Dof_ReadPublishedPose(publication) &&
                publication.generation == generation;
            float aimYaw = 0.0f, aimPitch = 0.0f;
            const bool aimValid = havePublication && Halo2DriveYawPitch(publication.stock.forward, aimYaw, aimPitch);
            if (aimValid)
            {
                st.aimYaw.store(aimYaw, std::memory_order_relaxed);
                st.aimPitch.store(aimPitch, std::memory_order_relaxed);
            }
            if (occupancy != 0)
            {
                // SEATED. Every seat aims from this same record, written after this same call (the
                // seat's limits are applied inside the accumulator). The hand owns the seat as
                // seat_drive::Decide says -- Halo 2's roles are not mapped, so every seat is taken as a driver
                // whose look steers: both axes at reach_vehicle_aim 2, the servo at 1. Never while the view
                // follows the vehicle: the hand's frame would turn with the hull it steers. And only while the
                // observer publishes THIS seat's own view (the vehicle reference, whose heading never reads the
                // native look): written without it, the stock camera -- built from this record -- would turn
                // the view with the hand.
                st.seated.fetch_add(1, std::memory_order_relaxed);
                const uint64_t seatViewAt = g_seatViewEngagedMs.load(std::memory_order_acquire);
                seat_drive::Seat facts{};
                facts.identified = occupancy > 0;
                // ...and the native first-person seat view placed THIS seat: the vehicle reference keeps the
                // stock camera's POSITION, which in a chase seat is pushed out along this very aim, so a write
                // there would swing the view round the hull (vehicle_first_person = 0 keeps the servo).
                facts.view_owned = havePublication && publication.vehicleReferenceValid &&
                    publication.vehicleUnit == Halo2OwnedUnit() && publication.vehicleParent == seatParent &&
                    publication.vehicleSeat == seatIndex && seatViewAt && now >= seatViewAt &&
                    now - seatViewAt <= 100 &&
                    NativeVehicleFirstPerson_SeatViewOwned(GameTitle::Halo2, seatParent, seatIndex);
                // THE SEAT'S FRAME AND ROLE, read as the rebase reads them (see the header): the occupied seat's live
                // flags word carries the role too (seat_converge_logic.h: bit 2 driver, bit 3 gunner -- the engine's
                // own sibling predicates 0x8FC99C / 0x8FCA5C and its aiming-unit test 0x8F8C00). A seat that does NOT
                // drive (a gunner, a passenger) is the hand's at "Drivers aim with the hand" OFF as well; a driver, or
                // a seat whose flags did not read, is taken as a driver whose look steers.
                seat_frame_rt::Live seatFrame =
                    seat_frame_rt::Read(g_halo2SeatFrame, &Halo2ObjectFromIndex, Halo2OwnedUnit());
                const seat_converge::Role seatRole = seat_converge::RoleFromSeatFlags(
                    occupancy > 0 && seatFrame.flagsKnown, seatFrame.flags);
                facts.driver = facts.look_steered = !seatRole.known || seatRole.driver;
                const seat_drive::Axes axes = g_config.vehicle_view_follow ? seat_drive::Axes{}
                    : seat_drive::Decide(facts, g_config.reach_vehicle_aim, g_config.seat_direct_drive != 0);
                const uint32_t key = seat_drive::SeatKey(static_cast<int>(seatParent), seatIndex, false);
                // The seat's account for the SIGHT ... seat line (every update that does not end in a converged
                // write says why -- our reasons only).
                const uint32_t stance = seat_converge::StanceKey(seatParent, seatIndex);
                const bool seatKnown = occupancy > 0;
                Halo2SeatBarrelNoteSeat(stance, seatKnown ? seatParent : UINT32_MAX, seatKnown ? seatIndex : -1,
                                        seatKnown && seatRole.known && seatRole.gunner);
                // THE GAME'S AIM IN A SEAT IS THE UNIT'S OWN AIMING VECTOR (seat_converge::SeatAimReference). The
                // observer camera's forward -- the right reference on foot -- sits a constant ~5.1 deg of pitch above
                // the unit's aim in a seat (measured in the Ghost and as a Warthog passenger, at every heading and
                // pitch), so against it the pitch proof below never passes, the pitch stays on the stick servo and no
                // seat ever writes both axes. The unit's aiming vector is the line the
                // engine fires along and equals the record to 0.02 deg; it is read here, on the sim thread, from the
                // same unit the occupancy read above (one tick behind the record: the proof's motion term covers it).
                // A vector that does not read as one leaves the camera's forward as the reference.
                float unitAim[3]{};
                bool unitAimRead = false;
                if (occupancy > 0)
                {
                    const auto* owned = static_cast<const uint8_t*>(Halo2ObjectFromIndex(Halo2OwnedUnit()));
                    if (owned && owned[0xAA] == 0)
                    {
                        std::memcpy(unitAim, owned + kHalo2UnitAimingVectorOffset, sizeof(unitAim));
                        unitAimRead = true;
                    }
                }
                const seat_converge::AimReference seatAim =
                    seat_converge::SeatAimReference(unitAimRead, unitAim, aimValid, aimYaw, aimPitch);
                if (seatAim.valid)
                {
                    (seatAim.fromUnit ? st.seatUnitAimRefs : st.seatCameraRefs).fetch_add(1, std::memory_order_relaxed);
                    if (std::isfinite(seatAim.cameraOffDeg) &&
                        seatAim.cameraOffDeg > st.seatCameraOffMaxDeg.load(std::memory_order_relaxed))
                        st.seatCameraOffMaxDeg.store(seatAim.cameraOffDeg, std::memory_order_relaxed);
                }
                // In case (b) the game's aim goes into the record's frame for the stint check and the arm gate, and
                // the hand's below.
                float recordAimYaw = seatAim.yaw, recordAimPitch = seatAim.pitch;
                bool frameRead = true;
                if (axes.any())
                {
                    seat_frame_rt::Count(g_halo2SeatFrameCounters, seatFrame.kase);
#if HALOMCCVR_DEV_DIAGNOSTICS
                    // THE FRAME CHECK: the record through R against the game's world aim (the unit's aiming vector;
                    // the observer camera only where that did not read).
                    if (seatAim.valid)
                        seat_frame_rt::NoteFrameCheck(g_halo2SeatFrameCounters, seatFrame, nativeYaw, nativePitch,
                                                      seatAim.yaw, seatAim.pitch);
#endif
                    frameRead = seat_frame_rt::ToRecord(seatFrame, recordAimYaw, recordAimPitch, nativeYaw);
                }
                if (!frameRead)
                {
                    // No frame this update: nothing is written, the servo keeps the seat; the stint survives it as
                    // it survives a gap in the seat's view (bounded).
                    if (facts.identified && st.stintActive && st.stintKey == key &&
                        ++st.stintStaleUpdates <= kHalo2StintStaleUpdates)
                        seatKept = true;
                    st.agree = 0;
                    Halo2NoteSeat(seatKnown, stance, seatRole, seat_converge::Why::NotWritten);
                    break;
                }
                if (!axes.any())
                {
                    // A GAP in this stint's seat (the F1 menu, a pause and its fade, the theater: the observer stops
                    // publishing the seat's view) keeps the proven checks for a bounded run of updates, writing
                    // nothing -- when the seat, its policy and everything but the view are unchanged.
                    seat_drive::Seat viewed = facts;
                    viewed.view_owned = true;
                    const bool onlyTheView = !facts.view_owned && !g_config.vehicle_view_follow &&
                        seat_drive::Decide(viewed, g_config.reach_vehicle_aim, g_config.seat_direct_drive != 0).any();
                    if (onlyTheView && facts.identified && st.stintActive && st.stintKey == key &&
                        ++st.stintStaleUpdates <= kHalo2StintStaleUpdates)
                        seatKept = true;
                    st.agree = 0;
                    // The stick keeps this seat: its first-person view is not ours (the engine aims it from the
                    // chase camera), the view follows the vehicle, or the player's keys leave it to the stick.
                    Halo2NoteSeat(seatKnown, stance, seatRole,
                        !facts.identified ? seat_converge::Why::NotIdentified
                        : onlyTheView ? seat_converge::Why::ChaseCamera : seat_converge::Why::PolicyOff);
                    break;
                }
                seatKept = true;
                st.stintStaleUpdates = 0;
                if (!st.stintActive || st.stintKey != key)
                {
                    Halo2DriveForgetSeat(st);
                    st.stintActive = true;
                    st.stintKey = key;
                    st.stintAircraft = Halo2SeatIsAircraft(seatParent);
                    st.seatStints.fetch_add(1, std::memory_order_relaxed);
                }
                const bool yawWas = st.stint.yaw_ok, pitchWas = st.stint.pitch_ok;
                // The unit's aiming vector is read fresh in this very update (every one is a sample of its own; a
                // record that moved since the unit last ticked is motion the proof allows for). The camera is a
                // publication: a repeated one is no evidence.
                const bool newSample = seatAim.fromUnit || !havePublication || publication.index != st.stintSample;
                st.stintSample = havePublication ? publication.index : 0;
                st.stint.step(seatAim.valid, nativeYaw, nativePitch, recordAimYaw, recordAimPitch, now, newSample);
                if (!yawWas && st.stint.yaw_ok)
                    st.seatYawChecks.fetch_add(1, std::memory_order_relaxed);
                if (!pitchWas && st.stint.pitch_ok)
                    st.seatPitchChecks.fetch_add(1, std::memory_order_relaxed);
                if (st.stint.pitch_failed && !st.stintPitchFailCounted)
                {
                    st.stintPitchFailCounted = true;   // each failed attempt once (the check retries every 5 s)
                    st.seatPitchFails.fetch_add(1, std::memory_order_relaxed);
                }
                else if (!st.stint.pitch_failed)
                    st.stintPitchFailCounted = false;
                if (!st.armed.load(std::memory_order_acquire))
                {
                    // A seat may arm the drive on yaw agreement alone (a level that starts in a vehicle).
                    const bool agrees = seatAim.valid && std::isfinite(nativeYaw) &&
                        std::fabs(std::remainder(nativeYaw - recordAimYaw, 6.28318531f)) < kHalo2DriveAgreeRad;
                    st.agree = agrees ? st.agree + 1 : 0;
                    if (st.agree >= kHalo2DriveAgreeUpdates)
                    {
                        st.expected.store(pc, std::memory_order_release);
                        st.armed.store(true, std::memory_order_release);
                        st.arms.fetch_add(1, std::memory_order_relaxed);
                    }
                    Halo2NoteSeat(seatKnown, stance, seatRole, seat_converge::Why::NotWritten);
                    break;
                }
                if (pc != st.expected.load(std::memory_order_acquire))
                {
                    st.armed.store(false, std::memory_order_release);
                    st.agree = 0;
                    seatKept = false;
                    st.disarms.fetch_add(1, std::memory_order_relaxed);
                    Halo2NoteSeat(seatKnown, stance, seatRole, seat_converge::Why::NotWritten);
                    break;
                }
                // The hand's aim. TRACE-AND-REAIM IN THE SEAT (seat_converge_logic.h): the direction
                // from Halo 2's own deterministic camera for this biped -- the origin of its crosshair test at the
                // fire (0x6D4730 from 0x7597B8) -- through the point the hand's ray hits, as this seat's own trace
                // found it (the stance check inside: never a range measured on foot, nor in another seat). Any of
                // our own reasons falls back to the plain steer: the stock camera's line through the hand ray at the
                // reticle distance.
                float direction[3]{};
                float yaw = 0.0f, pitch = 0.0f, handWorldYaw = 0.0f, handWorldPitch = 0.0f;
                seat_converge::Why seatWhy = seat_converge::Why::NoHand;
                bool handAim = Game_Halo2SeatAimActive();
                if (handAim)
                {
                    seatWhy = Halo2BuildSeatConvergedAim(Halo2OwnedUnit(), stance, direction, nullptr);
                    if (seatWhy != seat_converge::Why::Converged)
                        handAim = BuildPublishedControllerAimDirection(direction, nullptr, false);
                }
                const bool seatConverged = handAim && seatWhy == seat_converge::Why::Converged;
                // ...in the record's frame (case b: R^-1 of it), where the latch and the pitch limits live.
                handAim = handAim && Halo2DriveYawPitch(direction, yaw, pitch);
                handWorldYaw = yaw;
                handWorldPitch = pitch;
                handAim = handAim && seat_frame_rt::ToRecord(seatFrame, yaw, pitch, nativeYaw);
                const bool writeYaw = axes.yaw && st.stint.yaw_ok && handAim;
                const bool writePitch = axes.pitch && st.stint.yaw_ok && st.stint.pitch_ok && handAim;
                // A YAW-ONLY write in case (b) keeps the record's pitch: convert the hand's heading at the pitch the
                // game will aim at (seat_frame::RecordYawForWorldYaw), not at the hand's.
                if (writeYaw && !writePitch && seatFrame.kase == seat_frame::Case::Rebased)
                {
                    float yawOnly = 0.0f;
                    if (seat_frame::RecordYawForWorldYaw(seatFrame.frame, handWorldYaw, nativeYaw, nativePitch, yawOnly))
                        yaw = yawOnly;
                }
                if (!writeYaw && !writePitch)
                {
                    st.seatWaits.fetch_add(1, std::memory_order_relaxed);
                    Halo2NoteSeat(seatKnown, stance, seatRole,
                        seatConverged ? seat_converge::Why::NotWritten
                        : seatWhy == seat_converge::Why::Converged ? seat_converge::Why::NoHand : seatWhy);
                    break;
                }
                // Yaw: the seat-limit latch reads the seat's yaw clamp (a cone about the seat marker) off last
                // update's write and holds this one at it, after a short prime. Then [0, 2pi), as the native.
                float y = aim_write::unwrap_to_branch(yaw, nativeYaw);
                bool limited = false;
                bool yawLatchRead[2]{};
                if (writeYaw)
                {
                    const bool latch[2]{st.yawTicks >= kHalo2SeatPrimeUpdates, false};
                    yawLatchRead[0] = latch[0];
                    const float native[2]{nativeYaw, nativePitch}, target[2]{y, nativePitch};
                    float held[2]{};
                    limited = st.yawLatch.apply(native, target, held, latch);
                    if (latch[0])
                        y = aim_write::unwrap_to_branch(held[0], nativeYaw);
                }
                y = std::fmod(y, 6.28318531f);
                if (y < 0.0f)
                    y += 6.28318531f;
                if (!(y < 6.28318531f))
                    y = 0.0f;
                // Pitch: exactly the record's own seat limits, which the accumulator has just eased toward the
                // seat camera's (hull tilt included). Never latched: the accumulator's speed-scaled autolevel
                // moves the pitch as the vehicle drives, which is not a limit.
                const float low = *reinterpret_cast<const float*>(pc + kHalo2PcPitchLow);
                const float high = *reinterpret_cast<const float*>(pc + kHalo2PcPitchHigh);
                const float p = std::isfinite(low) && std::isfinite(high) && low <= high
                    ? std::clamp(pitch, low, high)
                    : std::clamp(pitch, -kHalo2DrivePitchClip, kHalo2DrivePitchClip);
                if (!std::isfinite(y) || !std::isfinite(p))
                    break;
                if (writeYaw)
                    pair[0] = y;
                if (writePitch)
                    pair[1] = p;
                seatWrote = true;
                const float written[2]{y, p};
                const bool recorded[2]{writeYaw, false};
                st.yawLatch.wrote(written, recorded);
                st.yawTicks = writeYaw ? (st.yawTicks < kHalo2SeatPrimeUpdates ? st.yawTicks + 1 : st.yawTicks) : 0;
                st.seatYawMs.store(writeYaw ? now : 0, std::memory_order_release);
                st.seatPitchMs.store(writePitch ? now : 0, std::memory_order_release);
                // The stops the reticle clamps against (seat_limit_reticle_logic.h), with the hand's world aim: the yaw
                // latch's, and the record's own eased pitch limits -- the accumulator clamps the pitch to them whoever
                // moves it, the write or the stick servo. Not in an air vehicle.
                if (st.stintAircraft)
                    st.stops.clear();
                else
                {
                    seat_limit::Stops stops{};
                    seat_limit::AddLatch(stops, st.yawLatch, yawLatchRead);
                    if (std::isfinite(low) && std::isfinite(high) && low <= high)
                        seat_limit::AddInterval(stops, 1, low, high);
                    stops.rebased = seatFrame.kase == seat_frame::Case::Rebased;
                    stops.frame = seatFrame.frame;
                    st.stops.publish(key, stops, nativeYaw, now, handWorldYaw, handWorldPitch);
                }
                st.seatWrites.fetch_add(1, std::memory_order_relaxed);
                if (limited)
                    st.seatHeld.fetch_add(1, std::memory_order_relaxed);
                // Reported as the WORLD angles the write put into effect (R e(record) in case b; an axis not written is
                // the record's own), so "last write" prints in the same frame as the camera's aim beside it.
                float worldYaw = writeYaw ? y : nativeYaw, worldPitch = writePitch ? p : nativePitch;
                seat_frame::Angles world{};
                if (seatFrame.kase == seat_frame::Case::Rebased &&
                    seat_frame::WorldFromRecord(seatFrame.frame, worldYaw, worldPitch, world))
                {
                    worldYaw = world.yaw;
                    worldPitch = world.pitch;
                }
                st.wroteYaw.store(worldYaw, std::memory_order_relaxed);
                st.wrotePitch.store(worldPitch, std::memory_order_relaxed);
                st.writes.fetch_add(1, std::memory_order_relaxed);
                // The aim now in the record is the converged one when BOTH axes carry it: the compositor draws the
                // traced reticle and the fire's backstop re-aims the engine's crosshair line. A stop the record or
                // the yaw latch held it at is natural (the reticle's own clamp shows it: seat_limit_reticle_logic.h)
                // and never takes the claim back; one axis still on the stick servo is not converged yet.
                if (seatConverged && writeYaw && writePitch)
                {
                    st.seatConvergedWrites.fetch_add(1, std::memory_order_relaxed);
                    st.convergedWrites.fetch_add(1, std::memory_order_relaxed);
                    Sight_MarkConverged(GameTitle::Halo2, Game_GetWorldScale());
                    Sight_MarkAimWritten(GameTitle::Halo2);
                    Halo2NoteSeat(seatKnown, stance, seatRole, seat_converge::Why::Converged);
                }
                else
                    Halo2NoteSeat(seatKnown, stance, seatRole,
                        seatConverged ? seat_converge::Why::NotWritten : seatWhy);
                break;
            }
            // ON FOOT. The player's control update runs for an on-foot unit: the observer may take the view's
            // heading.
            st.footControlMs.store(now, std::memory_order_release);
            if (!st.armed.load(std::memory_order_acquire))
            {
                const bool agrees = aimValid && std::isfinite(nativeYaw) && std::isfinite(nativePitch) &&
                    std::fabs(std::remainder(nativeYaw - aimYaw, 6.28318531f)) < kHalo2DriveAgreeRad &&
                    std::fabs(nativePitch - aimPitch) < kHalo2DriveAgreeRad;
                st.agree = agrees ? st.agree + 1 : 0;
                if (st.agree >= kHalo2DriveAgreeUpdates)
                {
                    st.expected.store(pc, std::memory_order_release);
                    st.armed.store(true, std::memory_order_release);
                    st.arms.fetch_add(1, std::memory_order_relaxed);
                }
                break;
            }
            if (pc != st.expected.load(std::memory_order_acquire))
            {
                st.armed.store(false, std::memory_order_release);
                st.agree = 0;
                st.disarms.fetch_add(1, std::memory_order_relaxed);
                break;
            }
            // The hand's aim, while VR owns Halo 2's aim (Insert switches it off) and a direction can be built
            // (both controllers tracked). Stamped before the view is engaged too: the observer holds the
            // heading, and keeps the right stick from the game, only while the drive has something to write.
            float direction[3]{};
            bool converged = false;
            float yaw = 0.0f, pitch = 0.0f;
            const bool handAim = Game_Halo2ControllerAimActive() &&
                BuildPublishedControllerAimDirection(direction, &converged) &&
                Halo2DriveYawPitch(direction, yaw, pitch);
            if (handAim)
                st.handAimMs.store(now, std::memory_order_release);
            // Only once the view is published at its own heading: before that, the stock camera -- built
            // from this record -- would turn the view with the hand.
            const uint64_t engaged = g_footViewEngagedMs.load(std::memory_order_acquire);
            if (!handAim || !havePublication || !publication.footReferenceValid || !engaged ||
                now < engaged || now - engaged > 100)
            {
                st.waits.fetch_add(1, std::memory_order_relaxed);
                break;
            }
            // Inside the native's own invariants: yaw in [0, 2pi), pitch inside the record's eased limits,
            // and finite -- a NaN makes the simulation drop the whole action.
            yaw = std::fmod(yaw, 6.28318531f);
            if (yaw < 0.0f)
                yaw += 6.28318531f;
            if (!(yaw < 6.28318531f))
                yaw = 0.0f;
            const float low = *reinterpret_cast<const float*>(pc + kHalo2PcPitchLow);
            const float high = *reinterpret_cast<const float*>(pc + kHalo2PcPitchHigh);
            pitch = std::isfinite(low) && std::isfinite(high) && low <= high
                ? std::clamp(pitch, low, high)
                : std::clamp(pitch, -kHalo2DrivePitchClip, kHalo2DrivePitchClip);
            if (!std::isfinite(yaw) || !std::isfinite(pitch))
                break;
            pair[0] = yaw;
            pair[1] = pitch;
            st.wroteYaw.store(yaw, std::memory_order_relaxed);
            st.wrotePitch.store(pitch, std::memory_order_relaxed);
            st.writes.fetch_add(1, std::memory_order_relaxed);
            if (converged)
                st.convergedWrites.fetch_add(1, std::memory_order_relaxed);
            st.liveMs.store(now, std::memory_order_release);
        } while (false);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // A fault disarms like a disarm: the record is proven again from a count of zero.
        if (st.armed.exchange(false, std::memory_order_acq_rel))
            st.disarms.fetch_add(1, std::memory_order_relaxed);
        st.agree = 0;
        seatKept = false;
        seatWrote = false;
        st.faults.fetch_add(1, std::memory_order_relaxed);
    }
    // A seat stint ends with any update that did not continue it (on foot, a seat the hand does not own, a
    // disarm, a fault); the yaw latch forgets with any update that did not write through it.
    if (!seatKept)
    {
        Halo2DriveForgetSeat(st);
    }
    else if (!seatWrote)
    {
        st.yawLatch.reset();
        st.yawTicks = 0;
        st.stops.clear();
        st.seatYawMs.store(0, std::memory_order_release);
        st.seatPitchMs.store(0, std::memory_order_release);
    }
    st.callbacks.fetch_sub(1, std::memory_order_acq_rel);
}

// Management thread, with the observer's install. Installed whatever the setting, so F1's Direct drive
// switches it live: it writes only while aim_direct_drive is on, and disarms whenever it is off.
bool InstallHalo2Drive(uintptr_t base, size_t size) noexcept
{
    auto& st = g_halo2Drive;
    if (st.target)
        return true;
    // WHERE EVERYTHING IS IN THIS MODULE (drive_sites.h, by the rungs of drive_known.h): verified at the addresses
    // it is known to stand at, or found by search; every proof is made on those addresses either way. Nothing
    // below trusts a recorded RVA.
    drive_cascade::Resolution<drive_sites::Halo2Control> resolution;
    const bool opened = drive_cascade::Resolve(
        base, size, drive_cascade::Drive{GameTitle::Halo2, "control-update"}, resolution,
        [](drive_resolve::Resolver& r) { return drive_sites::ResolveHalo2Control(r); });
    const drive_sites::Halo2Control& found = resolution.found;
    if (opened)
        drive_cascade::Announce(drive_cascade::kHalo2Name, resolution);
    // THE DECISION (drive_install.h; the offline check runs the same function on the game DLL as the loader maps
    // it): every anchor found, the call site's rel32 landing on the entry, the record slot inside the module --
    // the last two read again from the loaded image at the places the resolution found.
    const drive_install::Halo2Decision decision = drive_install::DecideHalo2(base, size, opened, found);
    const bool entryOk = decision.entryOk, strideOk = decision.strideOk, yawOk = decision.yawOk,
               pitchOk = decision.pitchOk, callOk = decision.callOk, slotOk = decision.slotOk;
    const uintptr_t call = decision.call, slot = decision.slot;
    if (!decision.hook)
    {
        char first[280] = "";
        if (opened)
            drive_cascade::FirstFailure(resolution.report, first, sizeof(first));
        LOG("Halo 2 DRIVE: control update NOT hooked (%s; found entry=%d stride=%d yaw=%d pitch=%d call=%d slot=%d; "
            "recorded at +0x%X); Halo 2 keeps its unit aim (the shot follows the hand; the body and the network do "
            "not)", opened ? first : (resolution.why ? resolution.why : "the module did not open"), entryOk ? 1 : 0,
            strideOk ? 1 : 0, yawOk ? 1 : 0, pitchOk ? 1 : 0, callOk ? 1 : 0, slotOk ? 1 : 0,
            drive_sites::kHalo2ControlAnchors[0].rva);
        return false;
    }
    st.armed.store(false, std::memory_order_release);
    st.expected.store(0, std::memory_order_release);
    st.agree = 0;
    Halo2DriveForgetSeat(st);
    st.footControlMs.store(0, std::memory_order_release);
    st.handAimMs.store(0, std::memory_order_release);
    st.liveMs.store(0, std::memory_order_release);
    st.controlMs.store(0, std::memory_order_release);
    st.slot = slot;
    st.controlReturn = call + 5;
    // The seat's frame: the vehicle-tilt rebase 0x8F0D20's gate and up getter, proven here (seat_frame_reader.h).
    const char* frameWhy = nullptr;
    bool frameBound = seat_frame_rt::Bind(g_halo2SeatFrame, seat_frame::kHalo2Reader, base, size, frameWhy);
    if (frameBound && g_halo2SeatFrame.objectsArray != base + kHalo2ObjectsDataArrayPointerRva)
    {
        // The reader resolves objects through Halo2ObjectFromIndex: it must be the array the rebase walks.
        seat_frame_rt::Unbind(g_halo2SeatFrame);
        frameBound = false;
        frameWhy = "its objects array is not the one the proven accessor reads";
    }
    g_halo2SeatFrameCounters.unknownLogged.store(0, std::memory_order_relaxed);
    if (frameBound)
        LOG("Halo 2 DRIVE: seat frame reader bound (the rebase +0x%X: its gate +0x%X and up getter +0x%X proven, "
            "objects through the proven accessor): a seated write follows the vehicle-tilt rebase (world / b)",
            seat_frame::kHalo2Reader.rebaseRva, seat_frame::kHalo2Reader.gateRva, seat_frame::kHalo2Reader.getterRva);
    else
        LOG("Halo 2 DRIVE: seat frame reader NOT bound (%s at +0x%X); seats are never written (the stick servo keeps "
            "every seat)", frameWhy ? frameWhy : "unknown", seat_frame::kHalo2Reader.gateRva);
    void* const target = reinterpret_cast<void*>(base + found.entry);
    const MH_STATUS created = MH_CreateHook(target, reinterpret_cast<void*>(&Halo2PcHook), &g_halo2PcOrig);
    if (created != MH_OK || MH_EnableHook(target) != MH_OK)
    {
        if (created == MH_OK)
            (void)MH_RemoveHook(target);
        g_halo2PcOrig = nullptr;
        LOG("Halo 2 DRIVE: control-update hook FAILED (create=%d); Halo 2 keeps its unit aim",
            static_cast<int>(created));
        return false;
    }
    st.target = target;
    st.entryRva = found.entry;
    st.installed.store(true, std::memory_order_release);
    LOG("Halo 2 DRIVE: control-update hook INSTALLED at +0x%X (player control globals slot +0x%llX, stride "
        "0x%X, desired angles +0x%X; written after the call at +0x%X only); disarmed until the record agrees "
        "with the game's camera", found.entry, static_cast<unsigned long long>(slot - base),
        static_cast<unsigned>(kHalo2PcStride), static_cast<unsigned>(kHalo2PcDesiredYaw), found.call);
    return true;
}

bool RemoveHalo2Drive() noexcept
{
    auto& st = g_halo2Drive;
    st.installed.store(false, std::memory_order_release);
    st.armed.store(false, std::memory_order_release);
    if (!st.target)
        return true;
    const MH_STATUS disabled = MCCVR_DisableHookForRetirement(st.target);
    if (disabled != MH_OK && disabled != MH_ERROR_DISABLED && disabled != MH_ERROR_NOT_CREATED)
    {
        LOG("Halo 2 DRIVE: disable failed (%d); ownership retained for cleanup", static_cast<int>(disabled));
        return false;
    }
    // The asm thunk and the body it reaches. Not the extern "C" entry in between: it compiles to a bare tail
    // jump with no unwind entry, which the quiescence scan cannot resolve, so listing it failed every
    // retirement (verified in the built DLL). A thread there has already left the trampoline.
    const void* functions[]{reinterpret_cast<const void*>(&Halo2PcHook),
                            reinterpret_cast<const void*>(&Halo2DriveApply)};
    const void* trampolines[]{g_halo2PcOrig, nullptr};
    if (!WaitForNativeDetourQuiescence(functions, trampolines, 2, st.callbacks))
    {
        LOG("Halo 2 DRIVE: callbacks did not drain; ownership retained for cleanup");
        return false;
    }
    const MH_STATUS removed = MH_RemoveHook(st.target);
    if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
    {
        LOG("Halo 2 DRIVE: remove failed (%d); ownership retained for cleanup", static_cast<int>(removed));
        return false;
    }
    st.target = nullptr;
    g_halo2PcOrig = nullptr;
    st.slot = 0;
    st.controlReturn = 0;
    seat_frame_rt::Unbind(g_halo2SeatFrame);
    return true;
}

// Cold worker, with the observer's telemetry.
//
// NO HOOK WATCHDOG HERE (drive_cascade.h: HookWatch), deliberately. A watchdog may count only time in which the
// hooked code must have been called. Halo 2 skips this update while its director holds the player (a cinematic),
// and the only word this mod has of that is this hook's own heartbeat (controlMs): nothing independent says when
// the game must be calling it, so a level's opening cinematic would be counted against a healthy address. A hook
// that is never called shows as no "Halo 2 DRIVE: armed=" line while playing on foot.
void ReportHalo2Drive() noexcept
{
    auto& st = g_halo2Drive;
    const uint32_t calls = st.calls.exchange(0, std::memory_order_relaxed);
    const uint32_t kicks = st.kicks.exchange(0, std::memory_order_relaxed);
    if (!calls && !kicks)
        return;
    LOG("Halo 2 DRIVE: armed=%d updates %u, wrote %u (%u converged), waited for the view or the hand %u; "
        "recoil kicks left stock %u; arms %u, disarms %u, seated %u, faults %u; record yaw %.3f pitch %.3f, "
        "camera yaw %.3f pitch %.3f, last write yaw %.3f pitch %.3f; view heading taken %u, released %u, "
        "folded %u",
        st.armed.load(std::memory_order_acquire) ? 1 : 0, calls, st.writes.exchange(0),
        st.convergedWrites.exchange(0), st.waits.exchange(0), kicks, st.arms.exchange(0),
        st.disarms.exchange(0), st.seated.exchange(0), st.faults.exchange(0), st.nativeYaw.load(),
        st.nativePitch.load(), st.aimYaw.load(), st.aimPitch.load(), st.wroteYaw.load(), st.wrotePitch.load(),
        g_footViewEngages.exchange(0), g_footViewReleases.exchange(0), g_footViewFolds.exchange(0));
    const uint32_t stints = st.seatStints.exchange(0), seatWrites = st.seatWrites.exchange(0),
                   seatWaits = st.seatWaits.exchange(0), yawChecks = st.seatYawChecks.exchange(0),
                   pitchChecks = st.seatPitchChecks.exchange(0), pitchFails = st.seatPitchFails.exchange(0),
                   held = st.seatHeld.exchange(0);
    char frames[96];
    const bool unread = seat_frame_rt::CaseText(g_halo2SeatFrameCounters, frames, sizeof(frames));
    if (stints || seatWrites || seatWaits || yawChecks || pitchChecks || pitchFails || held || frames[0])
        LOG("Halo 2 DRIVE seats: new stints %u; checks passed yaw %u pitch %u, pitch checks failed %u (retried after 5 s); "
            "writes %u (both axes converged on the traced point %u; yaw held at a seat limit %u), waited for the hand "
            "or a check %u; %s",
            stints, yawChecks, pitchChecks, pitchFails, seatWrites, st.seatConvergedWrites.exchange(0), held,
            seatWaits, frames[0] ? frames : "frame not read");
    // The gun's barrel aimed through the point (seat_barrel_logic.h): how often, from how many fires, and why not.
    auto& barrel = g_halo2SeatBarrel;
    const uint32_t barrelAimed = barrel.aimed.exchange(0, std::memory_order_relaxed),
                   barrelByEye = barrel.byEye.exchange(0, std::memory_order_relaxed),
                   barrelLearned = barrel.learned.exchange(0, std::memory_order_relaxed),
                   barrelDeclined = barrel.declined.exchange(0, std::memory_order_relaxed),
                   barrelNoFrame = barrel.noFrame.exchange(0, std::memory_order_relaxed),
                   barrelStaleEye = barrel.staleEye.exchange(0, std::memory_order_relaxed),
                   barrelOthers = barrel.othersFire.exchange(0, std::memory_order_relaxed),
                   barrelRecalled = barrel.recalled.exchange(0, std::memory_order_relaxed),
                   barrelMeasured = barrel.measured.exchange(0, std::memory_order_relaxed);
    if (barrelAimed || barrelLearned || barrelDeclined || barrelNoFrame || barrelStaleEye || barrelRecalled ||
        barrelMeasured)
        LOG("Halo 2 DRIVE seats: the gun's barrel was aimed through the point on %u updates (its place learned from "
            "%u of this seat's fires in this window; seats that took up a gun kept from an earlier seat %u, a "
            "measured gun %u); the seat's camera line was kept on %u (seat_aim_through_barrel off, nothing known of "
            "this seat's gun yet, or: the model declined %u, the vehicle's frame unread %u); fires that taught "
            "nothing: %u without a fresh eye; %u fires by other occupants of the vehicle were not this seat's",
            barrelAimed, barrelLearned, barrelRecalled, barrelMeasured, barrelByEye, barrelDeclined, barrelNoFrame,
            barrelStaleEye, barrelOthers);
#if HALOMCCVR_DEV_DIAGNOSTICS
    {
        // One line per round since the last report (the newest eight).
        Halo2SeatBarrelState::FireNote notes[8];
        uint32_t count = 0, reported = 0;
        // The fit of each gun, when a fire has moved it in this window (what a remembered barrel would hold).
        struct Fit { uint32_t weapon = UINT32_MAX, fires = 0; int16_t barrel = -1; double x[5]{}; } fits[2];
        uint64_t identity = 0;
        if (!barrel.busy.test_and_set(std::memory_order_acquire))
        {
            identity = barrel.identity;
            for (int g = 0; barrelLearned && g < 2; ++g)
            {
                fits[g].weapon = barrel.guns[g].weapon;
                fits[g].barrel = barrel.guns[g].barrel;
                fits[g].fires = barrel.guns[g].model.fires();
                barrel.guns[g].model.State(fits[g].x);
            }
            count = barrel.noteCount;
            reported = barrel.noteReported;
            std::memcpy(notes, barrel.notes, sizeof(notes));
            barrel.noteReported = count;
            barrel.busy.clear(std::memory_order_release);
        }
        for (const auto& fit : fits)
            if (fit.fires)
            {
                const char* vehicle = halo2_vehicle_identity::Name(identity);
                LOG("Halo 2 DRIVE seats (dev) gun fit: vehicle %s (identity 0x%llX), weapon 0x%08X barrel %d, %u "
                    "fires: pivot from the seat's eye forward %.4f left %.4f up %.4f wu; the barrel right %.4f up "
                    "%.4f wu of its pivot",
                    vehicle ? vehicle : "unknown", static_cast<unsigned long long>(identity), fit.weapon,
                    fit.barrel, fit.fires, fit.x[0], fit.x[1], fit.x[2], fit.x[3], fit.x[4]);
            }
        for (uint32_t i = count - reported > 8 ? count - 8 : reported; i < count; ++i)
        {
            const auto& n = notes[i % 8];
            LOG("Halo 2 DRIVE seats (dev) gun round %u: barrel %d, the write before it was the %s line, %u fires in "
                "this gun's model; the BARREL (the muzzle along the direction the fire was given) pointed %.2f deg / %.2f wu "
                "off the point at %.1f wu (-1: no point at this fire); the engine turned the round %.2f deg off the "
                "barrel (the weapon's deviation angle is the most it can); the ROUND left %.2f deg off the point, "
                "%.2f wu from the muzzle",
                i + 1, n.barrel, n.barrelWrite ? "guns' barrel" : "camera's", n.modelFires, n.barrelOffDeg,
                n.barrelMissWu, n.rangeWu, n.turnedDeg, n.roundOffDeg, n.roundFromMuzzleWu);
        }
    }
#endif
    // What the seat's proof was judged against (seat_converge::SeatAimReference), in every build: the unit's own
    // aiming vector, and how far the observer camera's forward sat off it.
    const uint32_t unitRefs = st.seatUnitAimRefs.exchange(0, std::memory_order_relaxed),
                   cameraRefs = st.seatCameraRefs.exchange(0, std::memory_order_relaxed);
    const float cameraOff = st.seatCameraOffMaxDeg.exchange(0.0f, std::memory_order_relaxed);
    if (unitRefs || cameraRefs)
        LOG("Halo 2 DRIVE seats: the seat's record was judged against the unit's own aiming vector on %u updates (the "
            "observer camera's forward sat up to %.2f deg off it: the camera is not a seat's aim) and against the "
            "observer camera on %u (the vector did not read)",
            unitRefs, cameraOff, cameraRefs);
    if (unread && !g_halo2SeatFrameCounters.unknownLogged.exchange(1, std::memory_order_relaxed))
        LOG("Halo 2 DRIVE seats: the seat's frame did not read (the rebase's links or the vehicle's up failed their "
            "checks); those updates write nothing and the stick servo keeps the seat (logged once)");
#if HALOMCCVR_DEV_DIAGNOSTICS
    char check[224];
    seat_frame_rt::FrameCheckText(g_halo2SeatFrameCounters, check, sizeof(check));
    if (check[0])
        LOG("Halo 2 DRIVE seats (dev): %s", check);   // frame check: max |R*e(record) - game aim| ...
#endif
}
