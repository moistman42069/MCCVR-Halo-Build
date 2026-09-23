// Included in haloce_controls.cpp's owned native-state namespace.
std::atomic<bool> crouchCameraReady{};
thread_local PhysicalCrouchCameraLease crouchCameraLease;
void PrepareCrouchCamera(uintptr_t base,size_t size,uint32_t gen) noexcept
{
    const char* failure{};
    const NativeContractSet proof{crouch_contract::entries,crouch_contract::witnesses,
        crouch_contract::relatives,crouch_contract::pointers};
    const bool ready=VerifyNativeFeatureBindings(base,size,gen,proof,failure);
    crouchCameraReady.store(ready,std::memory_order_release);
    LOG("CE physical crouch camera: %s%s%s",ready?"verified native interpolated blend":"StockFallback",
        failure?"; ":"",failure?failure:"");
}
float CrouchCameraBody(uint32_t expectedGeneration,uint64_t epoch,float physicalDown,bool allowed) noexcept
{
    PhysicalCrouchCameraRequest request{};
    HaloCELocalPlayerState state{};
    if(!allowed||!crouchCameraReady.load(std::memory_order_acquire)||!StateCurrent()||
       !PhysicalCrouchCamera_Read(request)||!ReadLocalPlayerState(state)||
       !state.hasControlledUnit||!state.onFoot||!state.nativePreparesFirstPerson||
       state.nativeInputBlocked||state.nativeLookBlocked||state.nativePaused||state.nativeCinematicFlag||
       state.generation!=expectedGeneration)
    {crouchCameraLease.Reset();return 0;}
    __try
    {
        const auto get=reinterpret_cast<ObjectGetFn>(moduleBase+contract::player_state::state_object_try_get);
        const auto* unit=reinterpret_cast<const uint8_t*>(get(state.unit,1));
        if(!unit||physical_crouch_native::Read<uint32_t>(unit,0xD8)!=UINT32_MAX||
           (unit[0xC2]&4)||physical_crouch_native::Read<uint16_t>(unit,0x70)!=0)
        {crouchCameraLease.Reset();return 0;}
        const auto tag=physical_crouch_native::Read<uint32_t>(unit,0);
        const auto* definition=reinterpret_cast<const uint8_t*(__fastcall*)(uint32_t)>(moduleBase+0xA9B648)(tag);
        if(!definition) {crouchCameraLease.Reset();return 0;}
        float fraction=physical_crouch_native::Read<float>(unit,0x518);
        // Exact CE evaluator order: native render interpolation, then the
        // partial-tick approach. Reading raw simulation crouch alone judders.
        if(!*reinterpret_cast<const uint8_t*>(moduleBase+0x1C34ED2))
        {
            const int output=reinterpret_cast<int(__fastcall*)(uint32_t)>(moduleBase+0xAE48C8)(state.unit);
            float interpolated{};
            if(output>=0&&output<4&&reinterpret_cast<bool(__fastcall*)(int,float*)>(moduleBase+0xBA834C)(output,&interpolated))
                fraction=interpolated;
            if(!(unit[0x4D8]&1)&&fraction>0&&fraction<1)
            {
                const auto* clock=*reinterpret_cast<const uint8_t* const*>(moduleBase+0x2E9FD68);
                if(!clock) {crouchCameraLease.Reset();return 0;}
                const float elapsed=physical_crouch_native::Read<float>(clock,0x1C);
                const float step=*reinterpret_cast<const float*>(moduleBase+0x194F150);
                const float speed=physical_crouch_native::Read<float>(definition,0x4CC);
                if(!std::isfinite(elapsed)||elapsed<0||!std::isfinite(step)||step<=0||
                   !std::isfinite(speed)||speed<0) {crouchCameraLease.Reset();return 0;}
                const float delta=(elapsed/step)*speed;
                fraction+=unit[0x287]==3?delta:-delta;
            }
        }
        float reduction{};
        if(!physical_crouch_native::Reduction(physical_crouch_native::Read<float>(definition,0x400),
                physical_crouch_native::Read<float>(definition,0x404),fraction,1.f,reduction)||
           get(state.unit,1)!=reinterpret_cast<uintptr_t>(unit)||
           physical_crouch_native::Read<uint32_t>(unit,0)!=tag||!StateCurrent()||generation.load()!=expectedGeneration)
        {crouchCameraLease.Reset();return 0;}
        return crouchCameraLease.Update(request,GameTitle::HaloCE,expectedGeneration,epoch,GetTickCount64(),
            state.unit,reinterpret_cast<uintptr_t>(unit),reduction,physicalDown);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {crouchCameraLease.Reset();return 0;}
}
