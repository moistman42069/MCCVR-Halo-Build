// Included by halo2_dual_wield_runtime.inl, before halo2_independent_shots.inl (whose camera and aim detours call
// Halo2SeatFireReaim and Halo2SeatNoteFireLine). Kept apart so that file's fixture can stub the two entries.
//
// SEAT CONVERGENCE, the fire's half (seat_converge_logic.h; halo2_direct_drive.inl has the write). At a seated
// fire player_aim_projectile (0x7596A0) asks this evaluator for the crosshair line of the player's biped: its seat
// camera along the biped's aiming vector, which follows the record the seat drive wrote. The engine starts its
// crosshair test on that line, as far out as the unit's origin is from the camera (0x7598A4..0x759988), and turns
// the shot at what the test hits -- pinned inside the weapon's deviation cone about the barrel (0x759AAC..0x759ADE,
// seats only), which is the seat's natural limit.
//
// The answer is replaced by the PLAYER'S line: from the rendered eye the seat's point was traced from, through that
// point (seat_fire_from_eye; the on-foot barrel shot replaces the same answer with the weapon's line,
// Halo2IndependentCameraDetour). The seat's camera is not where the player's eye is -- a head anywhere in the room
// but at the seat's camera -- so what the player aims at OVER something low and near, the seat camera's line can
// hit, and the engine then turns the shot at the obstacle. With the option off the answer is re-aimed from its own
// position through the point, which holds the line on the point whatever the unit's aiming rate.
//
// How far the engine's own line was off the point is counted either way (SHOT REAIM's seated part: what an unmodded
// host, tracing along the record's aim alone, would miss by). Only our seated unit, only while the seat's last
// write converged.
//
// The fire's own line -- the muzzle as it came into the aim call, along the direction the call answered -- is the
// barrel, and is handed to the barrel's lane (halo2_direct_drive.inl), which learns the gun from it
// (seat_barrel_logic.h).
std::atomic<uint64_t> g_halo2SeatFireApplied{0},g_halo2SeatFireThrough{0},g_halo2SeatFireSkipped{0},
    g_halo2SeatFireFromEye{0};
// Our unit's seat while its aim is converged: the rendered eye its point was traced from, and the point (fire
// thread; guarded reads).
bool Halo2SeatShotLine(uint32_t unit,float head[3],float point[3])
{
    __try
    {
        uint32_t parent=UINT32_MAX;int16_t seat=-1;
        if(unit==UINT32_MAX||unit!=Halo2OwnedUnit()||Halo2DriveOccupancy(parent,seat)!=1||
            !Game_Halo2SeatAimActive())return false;
        const uint32_t stance=seat_converge::StanceKey(parent,seat);
        float aim[3]{};
        return Sight_SeatConvergedLive(GameTitle::Halo2,stance,kSightFreshMs)&&
            Halo2BuildSeatConvergedAim(unit,stance,aim,point,head)==seat_converge::Why::Converged;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool Halo2SeatShotPoint(uint32_t unit,float point[3])
{
    float head[3]{};
    return Halo2SeatShotLine(unit,head,point);
}
// Our unit is seated at all (the counters below count seated fires only).
bool Halo2OwnedUnitSeated()
{
    __try
    {
        uint32_t parent=UINT32_MAX;int16_t seat=-1;
        return Halo2DriveOccupancy(parent,seat)==1;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void Halo2SeatFireReaim(uint32_t unit,float* position,float* direction)
{
    if(unit!=Halo2OwnedUnit()||!Halo2OwnedUnitSeated())return;
    float point[3]{},head[3]{};
    if(!Halo2SeatShotLine(unit,head,point)||!shot_lag::Finite3(position)||!shot_lag::Finite3(direction))
    {g_halo2SeatFireSkipped.fetch_add(1,std::memory_order_relaxed);return;}
    // The engine's own crosshair line against the point, before anything moves it (dev).
    Sight_NoteSeatShot(GameTitle::Halo2,SightSeatLine::Crosshair,position,direction,point);
    // Counted from the engine's own start, whichever line is then used.
    float line[3]{direction[0],direction[1],direction[2]};
    const bool bent=Sight_ReaimThroughPoint(GameTitle::Halo2,SightShotSite::CrosshairAssist,position,line,point,true);
    if(g_config.seat_fire_from_eye&&Sight_FireTimeChangesAllowed()&&shot_lag::Finite3(head))
    {
        const float toPoint[3]{point[0]-head[0],point[1]-head[1],point[2]-head[2]};
        const float range=shot_lag::Length(toPoint),length=shot_lag::Length(direction);
        if(std::isfinite(range)&&range>shot_lag::ReaimRails{}.minRangeWu&&std::isfinite(length)&&length>1.0e-6f)
        {
            const float scale=length/range;
            for(int axis=0;axis<3;++axis)
            {
                position[axis]=head[axis];
                direction[axis]=toPoint[axis]*scale;
            }
            g_halo2SeatFireFromEye.fetch_add(1,std::memory_order_relaxed);
            return;
        }
    }
    if(!bent)
    {g_halo2SeatFireThrough.fetch_add(1,std::memory_order_relaxed);return;}   // already through, or counted refused
    std::memcpy(direction,line,sizeof(line));
    g_halo2SeatFireApplied.fetch_add(1,std::memory_order_relaxed);
}
// The shot as the fire took it for our seat -- the barrel marker for a vehicle gun (its owner is the vehicle or a
// turret on it), the unit's aim for a passenger's own weapon. Nothing of the shot is changed: `direction` is the
// centre of the cone the engine pins the seated shot to. `muzzle` is the fire's start as it came into the aim call
// (null: unknown), `origin` as the call left it. A vehicle gun's line is handed to the barrel's lane; the line
// against the traced point is the dev witness.
void Halo2SeatNoteFireLine(uint32_t firer,uint32_t weapon,int16_t barrel,const float* muzzle,const float* origin,
    const float* direction)
{
    const uint32_t unit=Halo2OwnedUnit();
    if(unit==UINT32_MAX||!Halo2OwnedUnitSeated()||!shot_lag::Finite3(origin)||!shot_lag::Finite3(direction))return;
    const float* start=muzzle&&shot_lag::Finite3(muzzle)?muzzle:origin;
    Halo2SeatBarrelLearn(firer,weapon,barrel,start,direction);
#if HALOMCCVR_DEV_DIAGNOSTICS
    float point[3]{};
    if(Halo2SeatBarrelOurFire(firer,unit)&&Halo2SeatShotPoint(unit,point))
        Sight_NoteSeatShot(GameTitle::Halo2,SightSeatLine::Barrel,start,direction,point);
#endif
}
