// Included in the CE controls namespace. Read-only optional profile identity;
// no new hook, tag mutation, allocation or render-thread logging.
std::atomic<bool> vehicleIdentityReady{};
void PrepareVehicleIdentity(uintptr_t base,size_t size,uint32_t gen) noexcept
{
    const char* failure{};
    const NativeContractSet proof{vehicle_identity_contract::entries,
        vehicle_identity_contract::witnesses,vehicle_identity_contract::relatives,
        vehicle_identity_contract::pointers};
    const bool ready=VerifyNativeFeatureBindings(base,size,gen,proof,failure);
    vehicleIdentityReady.store(ready,std::memory_order_release);
    LOG("CE vehicle profiles: %s%s%s",ready?"verified ordered model-node identity":"per-game offsets only",
        failure?"; ":"",failure?failure:"");
}
struct VehicleModelReceipt
{
    uintptr_t definition{},model{},nodes{},cacheTable{};
    intptr_t virtualBase{},mappedBase{};
    uint32_t definitionTag{},modelTag{};
    int32_t count{},cachedNodes{};
    bool operator==(const VehicleModelReceipt&) const = default;
};
bool ReadVehicleModelReceipt(uintptr_t parent,VehicleModelReceipt& out) noexcept
{
    if(!parent)return false;
    using TagGet=uintptr_t(__fastcall*)(uint32_t);
    const auto get=reinterpret_cast<TagGet>(moduleBase+0xA9B648);
    VehicleModelReceipt row{};
    row.definitionTag=*reinterpret_cast<const uint32_t*>(parent);
    if(row.definitionTag==UINT32_MAX||(row.definitionTag&0xffffu)>=0x8000u)return false;
    row.cacheTable=*reinterpret_cast<const uintptr_t*>(moduleBase+0x1C34FB0);
    row.virtualBase=*reinterpret_cast<const intptr_t*>(moduleBase+0x2EA3410);
    row.mappedBase=*reinterpret_cast<const intptr_t*>(moduleBase+0x2D9CE10);
    if(!row.cacheTable||row.mappedBase<=0||row.virtualBase<0)return false;
    row.definition=get(row.definitionTag);
    if(!row.definition)return false;
    row.modelTag=*reinterpret_cast<const uint32_t*>(row.definition+0x34);
    if(row.modelTag==UINT32_MAX||(row.modelTag&0xffffu)>=0x8000u)return false;
    row.model=get(row.modelTag);
    if(!row.model)return false;
    row.count=*reinterpret_cast<const int32_t*>(row.model+0xB8);
    row.cachedNodes=*reinterpret_cast<const int32_t*>(row.model+0xBC);
    // Conservative optional-reader budget, not a claim about the engine limit.
    if(row.count<=0||row.count>64||row.cachedNodes<=0||row.cachedNodes<row.virtualBase)return false;
    const uintptr_t relative=static_cast<uintptr_t>(row.cachedNodes-row.virtualBase);
    const uintptr_t extent=static_cast<uintptr_t>(row.count)*0x9C;
    if(relative>0x40000000||static_cast<uintptr_t>(row.mappedBase)>UINTPTR_MAX-relative-extent)return false;
    row.nodes=static_cast<uintptr_t>(row.mappedBase)+relative;
    out=row;return true;
}
uint64_t HashVehicleNodeNames(const VehicleModelReceipt& row) noexcept
{
    uint64_t hash=14695981039346656037ull;
    // Versioned CE model-node namespace, independent of datum/cache address.
    for(const unsigned char c:std::array<unsigned char,5>{'C','E','V',1,static_cast<unsigned char>(row.count)})
    {hash^=c;hash*=1099511628211ull;}
    for(int i=0;i<row.count;++i)
    {
        const auto* name=reinterpret_cast<const unsigned char*>(row.nodes+size_t(i)*0x9C);
        bool ended=false;
        for(unsigned j=0;j<32;++j)
        {
            const unsigned char c=name[j];
            if(!c){if(!j)return 0;hash^=0;hash*=1099511628211ull;ended=true;break;}
            if(c<32||c>126)return 0;
            hash^=c;hash*=1099511628211ull;
        }
        if(!ended)return 0;
    }
    return hash;
}
uint64_t ReadVehicleModelIdentity(uintptr_t parent) noexcept
{
    if(!vehicleIdentityReady.load(std::memory_order_acquire)||!StateCurrent())return 0;
    const auto gen=generation.load(std::memory_order_acquire);
    __try
    {
        VehicleModelReceipt first{},after{};
        if(!ReadVehicleModelReceipt(parent,first))return 0;
        const uint64_t identity=HashVehicleNodeNames(first);
        if(!identity||!ReadVehicleModelReceipt(parent,after)||!(first==after)||
            identity!=HashVehicleNodeNames(after)||!StateCurrent()||gen!=generation.load(std::memory_order_acquire))return 0;
        return identity;
    }
    __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
