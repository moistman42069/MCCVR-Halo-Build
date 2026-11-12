// Shared VR space conversion deliberately matches H3's controller trajectories.
// ODST supplies its own admitted stereo boundary, local unit and inventory.
void PublishOdstIndependentAimBody()
{
    if constexpr(!kEnableOdstIndependentAim)return;
    auto& feature=g_odstMuzzle;
    if(!feature.enabled.load()||feature.faulted.load()||!Sight_IndependentDualAim()||
        !g_vrAim.load()||!g_enabled.load()||!g_baseCamValid.load()||
        feature.generation!=g_odstRuntimeGeneration.load())return;
    __try
    {
        int32_t scene=-1,shot=-1;
        if(ReadOdstCinematicControl(scene,shot)!=CinematicControlState::PlayerControlled||
            !g_odstPlayerUnitGetter||!g_odstUnitInVehicle)return;
        OdstIndependentAimSnapshot publication{};auto& sample=publication.aim;
        publication.leftHanded=g_config.left_handed;
        sample.unit=uint32_t(g_odstPlayerUnitGetter(0));
        if(!OdstReadMuzzleWeapons(sample.unit,sample.weapons)||sample.weapons[1]==UINT32_MAX||
            g_odstUnitInVehicle(int32_t(sample.unit)))return;
        VrContactTrackingSnapshot tracking{};
        if(!VR_GetContactTrackingSnapshot(tracking)||!tracking.referenceEpoch||
            !tracking.hands[0].valid||!tracking.hands[1].valid)return;
        sample.generation=feature.generation;sample.trackingEpoch=tracking.referenceEpoch;
        sample.timeNs=tracking.timeNs;sample.sampleMs=GetTickCount64();
        const float scale=g_worldScale.load();
        if(!std::isfinite(scale)||scale<=0)return;
        const float sh=sinf(g_headYawRef),ch=cosf(g_headYawRef);
        const float cg=cosf(g_gameYawRef),sg=sinf(g_gameYawRef);
        const float base[3]{g_baseCamX.load(),g_baseCamY.load(),BaseCamHandsZ()};
        for(int slot=0;slot<2;++slot)
        {
            // Tracking is already routed into primary/support roles by VR.
            const auto& hand=tracking.hands[slot==0?1:0];
            float basis[9]{};
            BuildTrackedGameBasisFromFrame(hand.orientation,false,false,0,0,basis);
            if(slot==1)
            {
                float mount[9]{},trimmed[9]{};
                BasisFromAngles(-g_config.gun_yaw_deg*.01745329252f,
                    g_config.gun_pitch_deg*.01745329252f,-g_config.gun_roll_deg*.01745329252f,mount);
                MultiplyBases(basis,mount,trimmed);std::memcpy(basis,trimmed,sizeof(basis));
            }
            const float dx=hand.position[0]-g_headPosRef[0],dy=hand.position[1]-g_headPosRef[1],
                dz=hand.position[2]-g_headPosRef[2];
            const float forward=dx*sh-dz*ch,right=dx*ch+dz*sh;
            sample.positions[slot][0]=base[0]+(cg*forward+sg*right)*scale;
            sample.positions[slot][1]=base[1]+(sg*forward-cg*right)*scale;
            sample.positions[slot][2]=base[2]+dy*scale;
            std::memcpy(sample.directions[slot],basis,12);
        }
        (void)feature.controllerAim.Publish(publication);
        VR_ObserveSecondaryWeaponPresentation(GameTitle::Halo3ODST,feature.generation);
    }
    __except(EXCEPTION_EXECUTE_HANDLER){MarkOdstMuzzleFault();}
}
__declspec(noinline) void PublishOdstIndependentAim()
{
    g_odstMuzzle.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try {PublishOdstIndependentAimBody();}
    __finally {g_odstMuzzle.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
}
