// Included before the ODST interpolation/palette hooks. All storage is bounded
// and thread-local; only existing ODST model identity readers are used.
int OdstReadFpModelIdentity(uint16_t tag,uint32_t& checksum);
struct OdstFpMappedTransaction
{
    OdstFpLayoutKey key{};
    OdstFpLayoutEntry frozen{};
    uint64_t serial=0,epoch=0;
    bool transformed=false;
};
thread_local OdstFpLayoutCache g_odstMappedFpCache;
thread_local OdstFpMappedTransaction g_odstMappedFpTransactions[2];

// Cold-worker diagnostics only; render callbacks publish bounded atomics.
struct OdstMappedFpDiagnostics {
    std::atomic<uint64_t> bodyObservations{0},gunObservations{0},acceptedPalettes{0};
    std::atomic<int> tag{-1},modelCount{0},stage{0};
};
OdstMappedFpDiagnostics g_odstMappedFpDiagnostics;

void OdstApplyMappedLayout(const OdstMappedFpLayout& mapped,FpInterpolationContext& context)
{
    const auto& layout=mapped.skeleton;
    context.wrist=layout.rightWrist; context.elbow=layout.rightElbow;
    context.shoulder=layout.rightShoulder;
    context.lWrist=layout.leftWrist; context.lElbow=layout.leftElbow;
    context.lShoulder=layout.leftShoulder;
    context.cameraControl=layout.cameraControl;
    context.wristDescendants=layout.rightHandAndWeaponDescendants;
    context.lWristDescendants=layout.leftHandDescendants;
    context.heldSources={};
    context.heldSources.explicitMap=true;
}

void OdstCaptureMappedInterpolation(int view,int id,int slot,bool result,
    BoneMatrix** outBones,int* outCount,bool muzzleOwner,uint32_t muzzleUnit,uint32_t muzzleWeapon)
{
    auto& transaction=g_odstMappedFpTransactions[slot];
    transaction={};
    const auto& scope=g_fpStereoSolveScope;
    const uint32_t generation=g_odstRuntimeGeneration.load(std::memory_order_acquire);
    const bool pairReady=g_odstMappedFpCache.Begin(
        scope.armed?scope.anatomicalTracking.serial:0,
        scope.armed?scope.anatomicalTracking.referenceEpoch:0,generation);
    const bool haveBones=result && outBones && outCount && *outBones &&
        *outCount>0 && *outCount<=64;
    if (haveBones && pairReady)
    {
        const int count=*outCount;
        transaction.key={generation,view,id,slot,count};
        transaction.serial=scope.anatomicalTracking.serial;
        transaction.epoch=scope.anatomicalTracking.referenceEpoch;
        transaction.frozen=g_odstMappedFpCache.Find(transaction.key);
        FpInterpolationContext& context=g_fpInterpolationContexts[slot];
        context.source=*outBones; context.count=count;
        context.generation=generation; context.player=view; context.slot=slot;
        context.wrist=-1; context.lWrist=-1;
        context.valid=true;
        uint32_t currentUnit=UINT32_MAX,currentWeapon=UINT32_MAX;
        if(muzzleOwner&&OdstCaptureMuzzleOwner(view,slot,currentUnit,currentWeapon)&&
            currentUnit==muzzleUnit&&currentWeapon==muzzleWeapon)
        {context.muzzleUnit=muzzleUnit;context.muzzleWeapon=muzzleWeapon;}
        memcpy(g_fpUnmodifiedInterpolations[slot],*outBones,
            size_t(count)*sizeof(BoneMatrix));
        if (slot==1 && view==0)
            VR_ObserveSecondaryWeaponPresentation(GameTitle::Halo3ODST,generation);
        if (transaction.frozen.valid)
        {
            OdstApplyMappedLayout(transaction.frozen.layout,context);
            MeasureOdstAuthoredBarrel(slot,g_fpUnmodifiedInterpolations[slot],context.wrist);
            BoneMatrix root{};
            if (BuildOdstCenterFpRoot(slot,root))
            {
                transaction.transformed=ApplyControllerToMarkerBonesFromRoot(root,*outBones,count,
                    context.wrist,context.lWrist,slot==1);
            }
        }
    }
    else PublishOdstFpLayoutSelfCheck(slot,haveBones?*outCount:-1,false,{});
}

// Native palette pointers are not trusted: contain access violations to this read.
bool OdstTryReadMappedPaletteBytes(const void* source,void* destination,size_t size)
{
#ifdef ODST_MODDED_FP_TESTING
    // The unit-test seam simulates unreadable native pointers without causing an AV.
    return SafeReadBytes(source,destination,size);
#else
    if(!source || !destination || size==0) return false;
    __try
    {
        memcpy(destination,source,size);
        return true;
    }
    // EXCEPTION_EXECUTE_HANDLER is 1 in WinNT.h; keep this unit-testable without windows.h.
    __except(1)
    {
        return false;
    }
#endif
}

FpInterpolationContext OdstMappedPaletteContext(const BoneMatrix* source)
{
    // Body and weapon palettes share a native interpolation. Consuming this
    // receipt on the first palette prevents learning when the gun comes first.
    for(const auto& candidate:g_fpInterpolationContexts)
        if(candidate.valid && source && candidate.source==source)return candidate;
    return {};
}

bool OdstResolveMappedPalette(uint16_t tag,const int32_t* boneMap,
    FpInterpolationContext& context)
{
    g_odstMappedFpDiagnostics.tag.store(tag,std::memory_order_relaxed);
    g_odstMappedFpDiagnostics.stage.store(1,std::memory_order_relaxed);
    if (!context.valid || context.slot<0 || context.slot>1 || !context.source ||
        context.count<=0 || context.count>64) return false;
    auto& transaction=g_odstMappedFpTransactions[context.slot];
    const auto& scope=g_fpStereoSolveScope;
    const bool current=scope.armed &&
        transaction.serial==scope.anatomicalTracking.serial &&
        transaction.epoch==scope.anatomicalTracking.referenceEpoch &&
        context.generation==g_odstRuntimeGeneration.load(std::memory_order_acquire);
    uint32_t checksum=0;
    const int count=current?OdstReadFpModelIdentity(tag,checksum):0;
    g_odstMappedFpDiagnostics.modelCount.store(count,std::memory_order_relaxed);
    int32_t remap[64]{};
    OdstMappedFpLayout observed{};
    const bool readable=count>0 && count<=64 && boneMap &&
        OdstTryReadMappedPaletteBytes(boneMap,remap,size_t(count)*sizeof(*remap));
    const bool body=readable && ResolveOdstMappedFpLayout(checksum,
        std::span<const int32_t>{remap,size_t(count)},context.count,observed);
    const auto& frozen=transaction.frozen;
    bool accepted=false;
    if (body)
    {
        ++g_odstMappedFpDiagnostics.bodyObservations;
        // Compare with the camera derived from BOTH maps, not count - 1.
        observed.skeleton.cameraControl=frozen.layout.skeleton.cameraControl;
        accepted=frozen.valid && frozen.bodyTag==tag && frozen.layout==observed;
        g_odstMappedFpCache.Learn(transaction.key,tag,observed);
    }
    else if (readable)
    {
        uint64_t mask=0;
        if(OdstCollectSourceMask(std::span<const int32_t>{remap,size_t(count)},context.count,mask)) {
            ++g_odstMappedFpDiagnostics.gunObservations;
            accepted=frozen.valid && frozen.heldTag==tag && frozen.heldMask==mask &&
                ResolveOdstHeldSources(frozen.layout,
                    std::span<const int32_t>{remap,size_t(count)},context.count,context.heldSources);
            g_odstMappedFpCache.LearnHeld(transaction.key,tag,mask);
        }
    }
    if (!accepted)
    {
        if (!readable) g_odstMappedFpCache.Reject(transaction.key);
        // A changed body map cannot leave speculative marker transforms in the
        // native source. Restore only this invocation's untouched graph.
        if (transaction.transformed)
            std::memcpy(const_cast<BoneMatrix*>(context.source),
                g_fpUnmodifiedInterpolations[context.slot],size_t(context.count)*sizeof(BoneMatrix));
        context.valid=false;
    }
    if(accepted)++g_odstMappedFpDiagnostics.acceptedPalettes;
    g_odstMappedFpDiagnostics.stage.store(accepted?7:!current?2:!readable?3:
        frozen.valid?6:body?5:4,std::memory_order_relaxed);
    PublishOdstFpLayoutSelfCheck(context.slot,context.count,accepted,
        accepted?frozen.layout.skeleton:OdstFpSkeletonLayout{},
        body?observed.bodyCount:frozen.layout.bodyCount,
        body?observed.checksum:frozen.layout.checksum);
    return accepted;
}
