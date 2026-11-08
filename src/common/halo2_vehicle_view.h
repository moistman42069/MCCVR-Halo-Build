#pragma once
#include "halo2_render_logic.h"

// H2's native observer heading is aim feedback. Using it as the controller's
// world reference feeds the commanded turn back into the target itself.
// Keep one room-to-world heading for this occupation; optional vehicle follow
// adds the chassis heading, never the cannon/native look heading.
struct Halo2VehicleViewKey
{
    uint32_t generation{},unit=UINT32_MAX,parent=UINT32_MAX,root=UINT32_MAX;
    int16_t seat=-1;
    uint64_t space{};
    bool operator==(const Halo2VehicleViewKey&) const = default;
};

struct Halo2VehicleViewState
{
    Halo2VehicleViewKey key{};
    bool active=false,follow=false;
    uint64_t timeMs{};
    float referenceYaw{},hullReferenceYaw{},lastYaw{};
    // A pause, menu or theater inside the occupation suspends the view and keeps its heading for when the same
    // occupation comes back: under the seat drive the stock heading is the HAND's last aim, so reseeding from
    // it (the gap reseed below) would snap the view onto the hand on every unpause.
    Halo2VehicleViewKey resumeKey{};
    bool resumeValid=false,resumeFollow=false;
    float resumeYaw{},resumeHullYaw{},lastHullYaw{};

    void Suspend() noexcept
    {
        if(active) {resumeKey=key;resumeYaw=lastYaw;resumeHullYaw=lastHullYaw;resumeFollow=follow;resumeValid=true;}
        active=false;
    }
    // A recenter re-zeroes the physical reference to where the head faces now: add the head's turn from the old
    // reference to the kept heading, so the seat resumes the view the player was looking along, not one swung
    // by that turn.
    void FoldResume(float headTurn) noexcept
    {
        if(resumeValid&&std::isfinite(headTurn)) resumeYaw=std::remainder(resumeYaw+headTurn,6.28318530718f);
    }
    // The player's own recenter: the seat resumes looking along `yaw` (the vehicle's nose), with the hull's
    // yaw now as the follow reference.
    void ReseedResume(float yaw,float hullYawNow) noexcept
    {
        if(resumeValid&&std::isfinite(yaw)&&std::isfinite(hullYawNow)) {resumeYaw=yaw;resumeHullYaw=hullYawNow;}
    }

    // `seedYaw`: the heading a new occupation starts from when the view already had its own -- a seat
    // taken from on foot under direct drive (halo2_foot_view.h), where the stock heading is the HAND's.
    // Without it, a new occupation starts from the stock heading, as before.
    bool Build(const Halo2VehicleViewKey& next,const Halo2CameraBasis& stock,
        const Halo2CameraBasis& hull,bool followHull,uint64_t now,
        Halo2CameraBasis& output,const float* seedYaw=nullptr) noexcept
    {
        if(!next.generation || next.unit==UINT32_MAX || next.parent==UINT32_MAX || next.root==UINT32_MAX || next.seat<0 ||
            !now || !Halo2ValidateCameraBasis(stock) || !Halo2ValidateCameraBasis(hull)) {
            active=false;return false;
        }
        const float horizontal=std::hypot(hull.forward[0],hull.forward[1]);
        if(followHull && horizontal<1.e-4f) {active=false;return false;}
        const float hullYaw=std::atan2(hull.forward[1],hull.forward[0]);
        const bool same=active&&key==next&&now>=timeMs&&now-timeMs<=250;
        if(!same) {
            const bool resume=resumeValid&&resumeKey==next&&std::isfinite(resumeYaw);
            referenceYaw=resume?resumeYaw:
                seedYaw&&std::isfinite(*seedYaw)?*seedYaw:std::atan2(stock.forward[1],stock.forward[0]);
            // Following the hull, a resume keeps the hull reference where the suspension left it, so a turn the
            // vehicle made meanwhile is still followed.
            hullReferenceYaw=resume&&resumeFollow&&followHull&&std::isfinite(resumeHullYaw)?resumeHullYaw:hullYaw;
            resumeValid=false;
        } else if(follow!=followHull) {
            // Toggling follow preserves the current view at the toggle edge.
            referenceYaw=lastYaw;hullReferenceYaw=hullYaw;
        }
        lastYaw=referenceYaw+(followHull?std::remainder(hullYaw-hullReferenceYaw,6.28318530718f):0.f);
        output=stock;
        output.forward[0]=std::cos(lastYaw);output.forward[1]=std::sin(lastYaw);output.forward[2]=0;
        output.up[0]=output.up[1]=0;output.up[2]=1;
        key=next;active=true;follow=followHull;timeMs=now;lastHullYaw=hullYaw;
        return true;
    }
};
