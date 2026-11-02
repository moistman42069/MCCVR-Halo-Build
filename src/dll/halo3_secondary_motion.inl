// The authored secondary wrist carries idle/fire/reload motion. Cancelling
// its current pose against the controller also cancels those animations.
// Keep a per-equipped-weapon reference and apply only its subsequent delta.
struct H3SecondaryMotion
{
    uint32_t generation{},unit=UINT32_MAX,weapon=UINT32_MAX;
    int count{},wrist=-1;
    uint64_t at{};
    weapon_animation::Reference reference;
    BoneMatrix delta{};
    bool valid=false;
};
thread_local H3SecondaryMotion g_h3SecondaryMotion;
std::atomic<uint64_t> g_h3SecondaryMotionSamples{},g_h3SecondaryMotionResets{},g_h3SecondaryMotionRefused{};
bool H3MotionFinite(const BoneMatrix& value)
{
    if(!std::isfinite(value.scale)||value.scale<=.001f)return false;
    for(float x:value.rotation)if(!std::isfinite(x))return false;
    for(float x:value.translation)if(!std::isfinite(x))return false;
    return true;
}
thread_local weapon_animation::ReferenceCache g_h3SecondaryReferenceCache;
void CaptureH3SecondaryMotion(const FpInterpolationContext& context,const BoneMatrix* authored)
{
    if(context.slot!=1||TitleAdapter_GetActiveTitle()!=GameTitle::Halo3)return;
    auto& state=g_h3SecondaryMotion;
    const uint64_t now=GetTickCount64();
    if(!context.valid||context.player!=0||context.muzzleWeapon==UINT32_MAX||
        context.muzzleUnit==UINT32_MAX||context.count<=0||context.count>64||!authored||context.lWrist<0||context.lWrist>=context.count) {
        state.valid=false;state.reference={};++g_h3SecondaryMotionRefused;return;
    }
    const auto& current=authored[context.lWrist];
    if(!H3MotionFinite(current)) {state.valid=false;state.reference={};++g_h3SecondaryMotionRefused;return;}
    weapon_animation::Pose local{},delta{};
    memcpy(local.rotation,current.rotation,sizeof(local.rotation));
    memcpy(local.translation,current.translation,sizeof(local.translation));
    const bool wasReady=state.reference.ready;
    state.valid=state.reference.Update({context.generation,
        (uint64_t(context.muzzleUnit)<<32)|context.muzzleWeapon,
        uint64_t(context.count),uint64_t(context.lWrist)},local,now,g_worldScale.load(),delta,true,&g_h3SecondaryReferenceCache);
    if(state.valid&&!wasReady)++g_h3SecondaryMotionResets;
    state.generation=context.generation;state.unit=context.muzzleUnit;state.weapon=context.muzzleWeapon;
    state.count=context.count;state.wrist=context.lWrist;state.at=now;
    state.delta.scale=1.f;
    memcpy(state.delta.rotation,delta.rotation,sizeof(delta.rotation));
    memcpy(state.delta.translation,delta.translation,sizeof(delta.translation));
    ++g_h3SecondaryMotionSamples;
}
// The visible-palette hook consumes the interpolation receipt before solving.
// Keep its validated local receipt scoped to that invocation; never re-arm the
// consumed live receipt or let another palette inherit it.
thread_local const FpInterpolationContext* g_h3SecondaryVisualContext=nullptr;
struct H3SecondaryVisualScope
{
    const FpInterpolationContext* previous;
    explicit H3SecondaryVisualScope(const FpInterpolationContext& context)
        : previous(g_h3SecondaryVisualContext) {g_h3SecondaryVisualContext=&context;}
    ~H3SecondaryVisualScope(){g_h3SecondaryVisualContext=previous;}
    H3SecondaryVisualScope(const H3SecondaryVisualScope&)=delete;
    H3SecondaryVisualScope& operator=(const H3SecondaryVisualScope&)=delete;
};
std::atomic<uint64_t> g_h3SecondaryMotionApplied{},g_h3SecondaryMotionSkipped{};
inline constexpr bool kH3SecondaryMotionEnabled=true;
void ApplyH3SecondaryMotion(BoneMatrix& visual,BoneMatrix& neutral)
{
    if(!kH3SecondaryMotionEnabled)return;
    const auto& state=g_h3SecondaryMotion;
    const auto& context=g_h3SecondaryVisualContext?*g_h3SecondaryVisualContext:g_fpInterpolationContexts[1];
    if(context.slot!=1||TitleAdapter_GetActiveTitle()!=GameTitle::Halo3||!state.valid||!context.valid||
        context.generation!=state.generation||context.count!=state.count||context.lWrist!=state.wrist||
        context.muzzleUnit!=state.unit||
        context.muzzleWeapon!=state.weapon||GetTickCount64()-state.at>100) {
        ++g_h3SecondaryMotionSkipped;return;
    }
    BoneMatrix moved{},neutralMoved{};
    if(ComposeBoneMatrices(visual,state.delta,moved)&&ComposeBoneMatrices(neutral,state.delta,neutralMoved)&&
        H3MotionFinite(moved)&&H3MotionFinite(neutralMoved)) {visual=moved;neutral=neutralMoved;++g_h3SecondaryMotionApplied;}
}

// Model-specific visual correction is shared with both native effect paths.
// The mask comes from the actual render-model remap, never a guessed subtree.
struct H3SecondaryEffectBinding
{
    std::atomic<uint32_t> sequence{0},generation{0},unit{UINT32_MAX},weapon{UINT32_MAX},checksum{0};
    std::atomic<uint64_t> mask{0};
};
H3SecondaryEffectBinding g_h3SecondaryEffectBinding;
std::atomic<uint64_t> g_h3SecondaryEffectApplied{},g_h3SecondaryEffectRefused{};
inline constexpr float kH3SecondaryLateralMetres=.06f;
inline constexpr float kH3SecondaryForwardMetres=.015f;
bool H3CorrectSecondaryMatrix(const BoneMatrix& frame,float yaw,BoneMatrix& value)
{
    if(!weapon_alignment::RotateYaw(frame.rotation,frame.translation,yaw,value.rotation,value.translation))return false;
    const float lateral=kH3SecondaryLateralMetres*g_worldScale.load();
    const float forward=kH3SecondaryForwardMetres*g_worldScale.load();
    for(int axis=0;axis<3;++axis)value.translation[axis]+=
        frame.rotation[3+axis]*lateral+frame.rotation[axis]*forward;
    return H3MotionFinite(value);
}
void ApplyH3SecondaryEffects(BoneMatrix* bones,int count,const BoneMatrix& frame,const BoneMatrix* root,float meshScale)
{
    if(!bones||count<=0||count>64||g_config.left_handed||TitleAdapter_GetActiveTitle()!=GameTitle::Halo3)return;
    const auto& context=g_fpInterpolationContexts[1];auto& binding=g_h3SecondaryEffectBinding;
    const auto sequence=binding.sequence.load(std::memory_order_acquire);
    if(!sequence||(sequence&1)||!context.valid||context.muzzleWeapon==UINT32_MAX)return;
    const auto generation=binding.generation.load(),unit=binding.unit.load(),weapon=binding.weapon.load(),checksum=binding.checksum.load();
    const uint64_t mask=binding.mask.load();
    if(sequence!=binding.sequence.load(std::memory_order_acquire)||generation!=context.generation||
        unit!=context.muzzleUnit||weapon!=context.muzzleWeapon)return;
    const float yaw=weapon_alignment::H3SecondaryYaw(1,checksum);
    if(!yaw||!mask)return;
    if(context.count!=count||context.lWrist<0||context.lWrist>=count||!std::isfinite(meshScale)||meshScale<=0)return;
    BoneMatrix nativeRoot{};
    if(root)nativeRoot=*root;
    else {
        if(!LoadCachedRenderRoot(context.player,nativeRoot))return;
        float camera[9];
        if(!g_camValid.load()||!LoadCameraBasis(camera))return;
        memcpy(nativeRoot.rotation,camera,sizeof(camera));
        nativeRoot.translation[0]=g_camX.load();nativeRoot.translation[1]=g_camY.load();nativeRoot.translation[2]=g_camZ.load();
    }
    BoneMatrix inverse{},anchor{},inverseAnchor{},carry{},candidate[64];
    const BoneMatrix* original=g_fpUnmodifiedInterpolations[1];
    if(!ComposeBoneMatrices(nativeRoot,original[context.lWrist],anchor)||!InvertBoneMatrix(anchor,inverseAnchor)||
        !ComposeBoneMatrices(frame,inverseAnchor,carry)||(root&&!InvertBoneMatrix(nativeRoot,inverse))) {
        ++g_h3SecondaryEffectRefused;return;
    }
    for(int i=0;i<count;++i) {
        candidate[i]=bones[i];if(!(mask&(uint64_t{1}<<i)))continue;
        BoneMatrix authored{},world{};
        if(!ComposeBoneMatrices(nativeRoot,original[i],authored)||!ComposeBoneMatrices(carry,authored,world)) {
            ++g_h3SecondaryEffectRefused;return;
        }
        for(int axis=0;axis<3;++axis)
            world.translation[axis]=frame.translation[axis]+(world.translation[axis]-frame.translation[axis])*meshScale;
        world.scale*=meshScale;
        if(!H3CorrectSecondaryMatrix(frame,yaw,world)||
            (root&&!ComposeBoneMatrices(inverse,world,candidate[i]))) {++g_h3SecondaryEffectRefused;return;}
        if(!root)candidate[i]=world;
    }
    memcpy(bones,candidate,size_t(count)*sizeof(BoneMatrix));++g_h3SecondaryEffectApplied;
}
