#pragma once
#include "../common/roomscale_admission_logic.h"
#include "../common/runtime_types.h"
void Roomscale_Input(bool allowed,float moveX,float moveY) noexcept;
// The jump action, as this poll feeds it to the game: the view recenter's takeoff witness reads it.
void Roomscale_JumpInput(bool held) noexcept;
bool Roomscale_Move(float& moveX,float& moveY) noexcept;
bool PhysicalRunning_Move(float& moveX,float& moveY) noexcept;
// The camera hook's frame. `admission` is the title's verdict (Game_RoomscaleCameraVerdict, or the hook's own
// veto ahead of it): admitted or not, and the reading that refused, which the report counts by name.
void Roomscale_Camera(GameTitle title,const roomscale_admission::Verdict& admission,const float body[3],
    const float head[3],const float orientation[4],const float forward[3],
    float reference[3],float scale) noexcept;
// The same with a bare yes / no (a refusal is counted as "refused by the caller").
void Roomscale_Camera(GameTitle title,bool allowed,const float body[3],
    const float head[3],const float orientation[4],const float forward[3],
    float reference[3],float scale) noexcept;
// A hook's own veto as a verdict: Roomscale_Camera(title, veto ? Roomscale_Refuse(why) : Game_...Verdict(title), ...).
inline roomscale_admission::Verdict Roomscale_Refuse(roomscale_admission::Refusal why) noexcept
{
    roomscale_admission::Verdict verdict{};
    verdict.refusal=why;
    return verdict;
}

void Roomscale_Report() noexcept;
