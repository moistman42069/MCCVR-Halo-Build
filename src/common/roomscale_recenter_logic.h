#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// ROOMSCALE VIEW RECENTER: slide the play area so the view comes back over the character.
//
// Roomscale keeps the view on the real head and walks the BODY to it. Two moments it cannot: in the
// air (a jump: nothing to walk on), and while the player moves with the stick (physical steps wait for
// native quiet). For as long as either lasts the rendered eye sits wherever the head went, away from
// the engine's own eye -- and the shot is solved from the engine's eye while the sight trace starts at
// the rendered one, so convergence and "is that cover in the way" both get worse with the offset.
//
// So, optionally, close the offset from the other side: move the tracking REFERENCE (the play-area
// origin every room -> world mapping subtracts) toward the head, horizontally. The view is built from
// head minus reference each frame, and so are the hands, the reticle ray and the contact frames, so all
// of them move together in the frame that shifted. Nothing here turns the view or changes its height.
//
// Three options, each off unless the player turns it on:
//  - Jump: a short linear blend to zero offset, started when a jump is seen to leave the ground.
//  - MoveStart: the same blend, started when the movement stick is first deflected.
//  - Moving: while the stick is held, close the offset at a constant speed (m/s).
// The motion they ride (the jump, the walk) is what masks the slide. Whatever is left when it ends is
// ordinary owed distance: the body follow walks the legs to the head as before.
//
// This file is pure: no clock, no engine, no config. roomscale.cpp feeds it and applies the result.
namespace roomscale_recenter
{
struct Settings
{
    bool jump=false;
    float jumpSeconds=0.20f;
    bool moveStart=false;
    float moveStartSeconds=0.20f;
    bool moving=false;
    float movingSpeed=0.50f;   // metres of offset closed per second
    bool Any() const noexcept { return jump || moveStart || moving; }
};

inline constexpr float kMinSeconds=0.05f, kMaxSeconds=1.0f;
inline constexpr float kMinSpeed=0.1f, kMaxSpeed=3.0f;
inline float ClampSeconds(float value,float fallback) noexcept
{ return std::isfinite(value) ? std::clamp(value,kMinSeconds,kMaxSeconds) : fallback; }
inline float ClampSpeed(float value,float fallback) noexcept
{ return std::isfinite(value) ? std::clamp(value,kMinSpeed,kMaxSpeed) : fallback; }

// A JUMP, SEEN FROM OUTSIDE THE ENGINE. No title here reads the character's ground state, and a guessed
// field per engine is exactly what this mod does not ship. What every title does have: the jump action
// the player pressed, and the character's eye (the engine camera before the head pose) every frame. A
// press followed within kArmUs by the eye rising kRiseM is a jump that left the ground. Vertical motion
// alone is not (a ramp, stairs, a lift); the press alone is not (a press the game ignored).
// Landing is the eye holding still vertically for kLandQuietUs: at the top of the arc the eye crosses
// kQuietM in well under that (about 100 ms at Halo's gravity, about 190 ms at a quarter of it).
struct JumpWitness
{
    static constexpr uint64_t kArmUs=400'000, kLandQuietUs=250'000, kMaxAirUs=4'000'000;
    static constexpr float kRiseM=0.04f, kQuietM=0.01f;
    bool held=false, armed=false, airborne=false;
    uint64_t armedAt=0, airSince=0, quietSince=0;
    float baseHeight=0, quietHeight=0;

    void Reset() noexcept { *this=JumpWitness{}; }

    // True on the one update that confirms a takeoff.
    bool Update(uint64_t nowUs,bool jumpHeld,float eyeHeightM) noexcept
    {
        if (!std::isfinite(eyeHeightM)) { Reset(); return false; }
        // A new press always arms, in the air or not: a landing seen late (walking on downhill after
        // it) must not cost the next jump its recenter.
        if (jumpHeld && !held) { armed=true; armedAt=nowUs; baseHeight=eyeHeightM; }
        held=jumpHeld;
        bool takeoff=false;
        if (armed)
        {
            // The lowest point since the press: a jump that dips first still rises kRiseM from there.
            baseHeight=std::min(baseHeight,eyeHeightM);
            if (nowUs<armedAt || nowUs-armedAt>kArmUs) armed=false;
            else if (eyeHeightM-baseHeight>=kRiseM)
            {
                armed=false; takeoff=true;
                airborne=true; airSince=quietSince=nowUs; quietHeight=eyeHeightM;
            }
        }
        if (airborne && !takeoff)
        {
            if (std::fabs(eyeHeightM-quietHeight)>kQuietM) { quietHeight=eyeHeightM; quietSince=nowUs; }
            if (nowUs<airSince || nowUs-airSince>kMaxAirUs ||
                nowUs<quietSince || nowUs-quietSince>=kLandQuietUs) airborne=false;
        }
        return takeoff;
    }
};

enum class Mode : uint8_t { None, Jump, MoveStart, Moving };
// Why a wanted recenter did not run: the offset source was not valid. Counted by the caller.
enum class Refusal : uint8_t
{
    None,
    NotOnFoot,   // the title's own admission: a seat, a cinematic, third person, death, a menu, a pause
    Untracked,   // no fresh tracked head
    NoInput,     // no fresh gameplay input (a real gamepad's stick, the D-pad gesture, a stale poll)
    NoFollow,    // the body follow refused this update (non-finite pose, no title generation)
    Count
};

struct Result
{
    float dx=0, dz=0;      // add to the tracking reference's horizontal axes (tracking metres)
    float keep=1;          // the share of the offset left: 1 - the fraction closed this update
    Mode mode=Mode::None;  // what closed it
    Mode started=Mode::None;   // a quick blend began on this update
    bool takeoff=false;    // the jump witness confirmed a takeoff on this update
    Refusal refused=Refusal::None;   // set once per wanted-but-blocked run
};

struct ViewRecenter
{
    // A longer gap between two updates than this is not one frame: the constant-speed option moves at
    // most this much time's worth, so the first update after a hitch cannot leap.
    static constexpr uint64_t kMaxStepUs=100'000;
    // Under this the offset is closed: no shift, nothing to report.
    static constexpr float kDoneM=0.0005f;

    JumpWitness jumpWitness;
    // The quick blend in progress. It runs while ANY of the motions that started it still hides it:
    // a jump begun mid-walk carries on in the air after the stick lets go, and the other way round.
    Mode blend=Mode::None;          // the trigger that last (re)started the timer; None = no blend
    bool blendForJump=false, blendForMove=false;
    uint64_t blendElapsedUs=0, blendTotalUs=0;
    uint64_t lastUs=0;
    bool haveLast=false, stickWasHeld=false, jumpWasHeld=false, refusalCounted=false;

    void Reset() noexcept { *this=ViewRecenter{}; }

    static uint64_t SecondsToUs(float seconds) noexcept
    { return uint64_t(double(ClampSeconds(seconds,0.2f))*1'000'000.0+0.5); }

    void EndBlend() noexcept
    { blend=Mode::None; blendForJump=blendForMove=false; blendElapsedUs=blendTotalUs=0; }

    // True when the timer (re)started: a blend already running is kept unless the new one would
    // finish sooner. Either way the new trigger's motion now also keeps it alive.
    bool Begin(Mode mode,float seconds) noexcept
    {
        (mode==Mode::Jump ? blendForJump : blendForMove)=true;
        const uint64_t total=SecondsToUs(seconds);
        if (blend!=Mode::None && blendTotalUs-blendElapsedUs<=total) return false;
        blend=mode; blendElapsedUs=0; blendTotalUs=total;
        return true;
    }

    // offsetX/offsetZ: head minus reference, horizontal, tracking metres -- how far the rendered eye is
    // from the engine's eye. blocked: why that offset cannot be trusted or acted on right now (None =
    // valid). stickHeld: the movement stick is deflected (the body follow's own "manual" signal).
    // jumpHeld: the jump action is down. eyeHeightM: the character's eye height, metres, any origin.
    Result Update(uint64_t nowUs,const Settings& settings,Refusal blocked,bool stickHeld,bool jumpHeld,
        float eyeHeightM,float offsetX,float offsetZ) noexcept
    {
        Result result{};
        const bool stickEdge=stickHeld && !stickWasHeld;
        const bool jumpEdge=jumpHeld && !jumpWasHeld;
        stickWasHeld=stickHeld; jumpWasHeld=jumpHeld;
        if (blocked==Refusal::None &&
            !(std::isfinite(offsetX) && std::isfinite(offsetZ) && std::isfinite(eyeHeightM)))
            blocked=Refusal::NoFollow;
        if (!settings.Any() || blocked!=Refusal::None)
        {
            // Not valid, or everything switched off: end whatever was running. A recenter that was
            // wanted and could not run is reported once per press (per stick hold, per jump press).
            const bool wanted=(settings.moving && stickHeld) || (settings.moveStart && stickEdge) ||
                (settings.jump && jumpEdge);
            if (!stickHeld && !jumpHeld) refusalCounted=false;
            if (wanted && blocked!=Refusal::None && !refusalCounted)
            { result.refused=blocked; refusalCounted=true; }
            jumpWitness.Reset(); EndBlend(); haveLast=false;
            return result;
        }
        refusalCounted=false;
        const uint64_t elapsedUs=haveLast && nowUs>lastUs ? nowUs-lastUs : 0;
        lastUs=nowUs; haveLast=true;
        const bool takeoff=jumpWitness.Update(nowUs,jumpHeld,eyeHeightM);
        result.takeoff=takeoff;

        // What ends a quick blend early: the motion that hides it is over (landed, stick released), or
        // its option was switched off.
        if (blendForJump && (!settings.jump || !jumpWitness.airborne)) blendForJump=false;
        if (blendForMove && (!settings.moveStart || !stickHeld)) blendForMove=false;
        if (blend!=Mode::None && !blendForJump && !blendForMove) EndBlend();
        bool began=false;
        if (settings.jump && takeoff && Begin(Mode::Jump,settings.jumpSeconds)) began=true;
        if (settings.moveStart && stickEdge && Begin(Mode::MoveStart,settings.moveStartSeconds)) began=true;
        if (began) result.started=blend;

        // The quick blend: linear in time. Closing (dt / time left) of what is left each update lands
        // on zero exactly at the end, whatever the frame rate, and keeps landing there if the head
        // moves meanwhile. It starts on the update after the one that began it.
        float blendFraction=0;
        const Mode blending=blend;
        if (blend!=Mode::None && !began)
        {
            const uint64_t left=blendTotalUs-blendElapsedUs;
            const uint64_t step=std::min(elapsedUs,left);
            blendFraction=left ? float(double(step)/double(left)) : 1.0f;
            blendElapsedUs+=step;
            if (blendElapsedUs>=blendTotalUs) EndBlend();
        }

        const float distance=std::hypot(offsetX,offsetZ);
        if (distance<kDoneM) return result;
        // The constant-speed option: while the stick is held, never further than the offset.
        float movingFraction=0;
        if (settings.moving && stickHeld)
        {
            const float seconds=float(std::min(elapsedUs,kMaxStepUs))*1e-6f;
            movingFraction=std::min(1.0f,ClampSpeed(settings.movingSpeed,0.5f)*seconds/distance);
        }
        const float fraction=std::clamp(std::max(blendFraction,movingFraction),0.0f,1.0f);
        if (fraction<=0) return result;
        result.dx=offsetX*fraction; result.dz=offsetZ*fraction;
        result.keep=1.0f-fraction;
        result.mode=blendFraction>=movingFraction ? blending : Mode::Moving;
        return result;
    }
};
}
