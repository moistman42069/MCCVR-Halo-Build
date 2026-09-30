// Optional H3 full-world avatar. Native cached palettes are never modified.
// The only skinning replacement is the exact mesh upload's private Map buffer.
#include "../common/halo3_avatar_logic.h"
namespace halo3_avatar_native {
using ExcludeFn=uint64_t(__fastcall*)(int32_t,uint32_t);
using ProducerFn=uint8_t(__fastcall*)(uint32_t,float,uint32_t,uint32_t,void*);
using RegionsFn=void(__fastcall*)(void*,void*);
using UploadFn=void(__fastcall*)(void*,uint8_t);
using MapFn=void*(__fastcall*)(int32_t,uint32_t,uint32_t*);
using CommitFn=void(__fastcall*)(int32_t,uint32_t);
using WorldFn=void(__fastcall*)(uint32_t,uint8_t,uint32_t*,const void**,uint32_t*);
using SubmitAFn=void(__fastcall*)(uintptr_t,void*,uint64_t,uint8_t,uint8_t,uint8_t);
using SubmitBFn=void(__fastcall*)(uintptr_t,void*,uint8_t,uint8_t);
struct Runtime {
    std::atomic<bool> enabled{false},faulted{false};
    std::atomic<uint32_t> callbacks{0},generation{0};
    std::atomic<uint64_t> posed{0},uploads{0},refusals{0},ordering{0},faults{0};
    uintptr_t base{};size_t size{};void* targets[8]{};
    ExcludeFn exclude{};ProducerFn producer{};RegionsFn regions{};
    UploadFn upload{};MapFn map{};CommitFn commit{};WorldFn world{};
    SubmitAFn submitA{};SubmitBFn submitB{};
} runtime;
struct Candidate {
    void* info{};uint32_t unit=UINT32_MAX,tag=UINT32_MAX,designator=UINT32_MAX;
    halo3_avatar::Profile profile{};
    halo3_avatar::Pose source{},inverse{};
    halo3_avatar::GpuPose gpu{};
    bool occupied=false,ready=false;
};
struct Pair {
    bool leased=false,active=false,rejected=false,ready=false,fpAdmitted=false;
    uint32_t generation=0,unit=UINT32_MAX;
    uint64_t serial=0,epoch=0,nonce=0;
    halo3_avatar::Request request{};
    uint32_t fpChecksum{};unsigned fpCount{};
    bool wristsValid=false,hideLower=false;
    unsigned nestedDepth=0;
    Candidate rows[4]{};
};
thread_local Pair pair;
thread_local uint64_t pairSequence=0;
struct FpReceipt {
    uint32_t unit=UINT32_MAX,generation{},checksum{};unsigned count{};
    uint64_t serial{},epoch{},nonce{};int64_t timeNs{};
    halo3_avatar::Matrix wrists[2]{};bool valid=false;
};
void AdoptReceipt(const FpReceipt& receipt) {
    if(!pair.active||!pair.leased||pair.nestedDepth||pair.ready||!pair.nonce||receipt.nonce!=pair.nonce||
       receipt.unit!=pair.unit||receipt.generation!=pair.generation||
       receipt.serial!=pair.serial||receipt.epoch!=pair.epoch||!receipt.valid)return;
    pair.fpChecksum=receipt.checksum;pair.fpCount=receipt.count;
    std::memcpy(pair.request.fpWristWorld,receipt.wrists,sizeof(receipt.wrists));pair.wristsValid=true;
}
struct UploadScope {Candidate* candidate{};unsigned first{},count{};void* mapped{};uint32_t bytes{};};
thread_local UploadScope uploadScope;
bool BeginPairState() noexcept {
    if(pair.leased){++pair.nestedDepth;return false;}
    pair={};runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);pair.leased=true;
    if(++pairSequence==0)++pairSequence;
    pair.nonce=pairSequence;return true;
}
void EndPairState() noexcept {
    if(pair.nestedDepth){--pair.nestedDepth;return;}
    const bool leased=pair.leased;pair={};uploadScope={};if(leased)runtime.callbacks.fetch_sub(1,std::memory_order_acq_rel);
}
struct RegionScope {
    void* info{};uint32_t tag{},unit{},designator{},beforeCount{},afterCount{};
    std::array<int16_t,halo3_avatar::RegionCapacity> before{},after{};
    unsigned meshCount{};bool changed=false;
};
thread_local RegionScope regionScope;
bool Current() noexcept;
bool Capture(uint32_t unit,void* info,Candidate& out);
bool IsOwnFpRow(void* model,void* info);
bool SelectUpload(void* mesh,UploadScope& scope);
bool Prepare(Candidate& candidate);
void Fault() noexcept {runtime.faulted.store(true,std::memory_order_release);runtime.faults.fetch_add(1,std::memory_order_relaxed);pair.rejected=true;}
bool PairCurrent() noexcept {
    return pair.active&&!pair.rejected&&!pair.nestedDepth&&Current()&&pair.generation==runtime.generation.load(std::memory_order_acquire);
}
__declspec(noinline) bool SafeCapture(uint32_t unit,void* info,Candidate& out) {
    volatile bool fault=false;bool result=false;
    __try{result=Capture(unit,info,out);}__except(EXCEPTION_EXECUTE_HANDLER){fault=true;}
    if(fault)Fault();return result;
}
__declspec(noinline) bool SafePrepare(Candidate& candidate) {
    volatile bool fault=false;bool result=false;
    __try{result=Prepare(candidate);}__except(EXCEPTION_EXECUTE_HANDLER){fault=true;}
    if(fault)Fault();return result;
}
__declspec(noinline) bool SafeFp(void* model,void* info) {
    volatile bool fault=false;bool result=false;
    __try{result=IsOwnFpRow(model,info);}__except(EXCEPTION_EXECUTE_HANDLER){fault=true;}
    if(fault)Fault();return result;
}
__declspec(noinline) bool SafeSelect(void* mesh,UploadScope& scope) {
    volatile bool fault=false;bool result=false;
    __try{result=SelectUpload(mesh,scope);}__except(EXCEPTION_EXECUTE_HANDLER){fault=true;}
    if(fault)Fault();return result;
}
bool StageRegionChange(void* info,unsigned meshCount) {
    if(regionScope.info!=info||regionScope.changed||meshCount>halo3_avatar::RegionCapacity)return false;
    const auto* bytes=static_cast<const uint8_t*>(info);
    std::memcpy(&regionScope.tag,bytes+4,4);std::memcpy(&regionScope.unit,bytes+0x48,4);
    std::memcpy(&regionScope.designator,bytes+8,4);std::memcpy(&regionScope.beforeCount,bytes,4);
    regionScope.afterCount=regionScope.beforeCount;regionScope.meshCount=meshCount;
    std::memcpy(regionScope.before.data(),bytes+0xE,meshCount*2);
    regionScope.after=regionScope.before;regionScope.changed=true;return true;
}
__declspec(noinline) void RestoreRegions(bool interrupted=false);
__declspec(noinline) bool Hide(void* info) {
    volatile bool fault=false;bool result=false;
    __try{if(StageRegionChange(info,0)){regionScope.afterCount=0;*static_cast<volatile uint32_t*>(info)=0;result=true;}}
    __except(EXCEPTION_EXECUTE_HANDLER){fault=true;}
    if(fault){RestoreRegions(true);Fault();}return result;
}
__declspec(noinline) bool Mask(Candidate& c) {
    volatile bool fault=false;bool result=false;
    __try {
        const auto* bytes=static_cast<const uint8_t*>(c.info);
        std::array<int16_t,halo3_avatar::RegionCapacity> meshes{};
        const auto count=*reinterpret_cast<const volatile uint32_t*>(bytes);
        if(count==c.profile.regions&&count<=meshes.size()) {
            std::memcpy(meshes.data(),bytes+0xE,count*2);
            if(halo3_avatar::MaskRegions(c.profile,pair.hideLower,count,meshes)&&StageRegionChange(c.info,count)) {
                regionScope.after=meshes;
                std::memcpy(static_cast<uint8_t*>(c.info)+0xE,meshes.data(),count*2);result=true;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){fault=true;}
    if(fault){RestoreRegions(true);Fault();}return result;
}
__declspec(noinline) void RestoreRegions(bool interrupted) {
    volatile bool fault=false;
    __try {
        const auto& s=regionScope;
        if(s.changed&&s.info) {
            auto* bytes=static_cast<uint8_t*>(s.info);
            uint32_t tag{},unit{},designator{},count{};
            std::memcpy(&tag,bytes+4,4);std::memcpy(&unit,bytes+0x48,4);
            std::memcpy(&designator,bytes+8,4);std::memcpy(&count,bytes,4);
            if(tag==s.tag&&unit==s.unit&&designator==s.designator&&
               ((count==s.afterCount&&std::memcmp(bytes+0xE,s.after.data(),s.meshCount*2)==0)||
                (interrupted&&(count==s.beforeCount||count==s.afterCount)))) {
                // The interrupted-write path runs immediately in the same
                // callback, before any native consumer/reuse. Restore changed
                // words only: an unchanged read-only page needs no write.
                auto* meshes=reinterpret_cast<volatile int16_t*>(bytes+0xE);
                for(unsigned i=0;i<s.meshCount;++i)if(meshes[i]!=s.before[i])meshes[i]=s.before[i];
                if(count!=s.beforeCount)*reinterpret_cast<volatile uint32_t*>(bytes)=s.beforeCount;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){fault=true;}
    if(fault)Fault();
}
void __fastcall SubmitAHook(uintptr_t view,void* info,uint64_t flags,uint8_t a,uint8_t b,uint8_t c) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    const RegionScope previous=regionScope;regionScope={};regionScope.info=info;
    __try {runtime.submitA(view,info,flags,a,b,c);}
    __finally {RestoreRegions();regionScope=previous;runtime.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
}
void __fastcall SubmitBHook(uintptr_t view,void* info,uint8_t a,uint8_t b) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    const RegionScope previous=regionScope;regionScope={};regionScope.info=info;
    __try {runtime.submitB(view,info,a,b);}
    __finally {RestoreRegions();regionScope=previous;runtime.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
}
uint64_t __fastcall ExcludeHook(int32_t user,uint32_t object) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    uint64_t result=0;
    __try {
        result=runtime.exclude(user,object);
        // Preserve every other predicate caller, all attached weapons and all
        // remote/split-screen users. Full object salt was validated at Begin.
        if(result&&reinterpret_cast<uintptr_t>(_ReturnAddress())==runtime.base+0x252C7A&&
           user==0&&object==pair.unit&&PairCurrent())result=0;
    } __finally {runtime.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
    return result;
}
uint8_t __fastcall ProducerHook(uint32_t object,float distance,uint32_t flags,uint32_t user,void* info) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);uint8_t result=0;
    __try {
        result=runtime.producer(object,distance,flags,user,info);
        if(!result&&pair.active&&!pair.nestedDepth&&object==pair.unit&&user==0)
            runtime.refusals.fetch_add(1,std::memory_order_relaxed);
        if(result&&pair.active&&!pair.nestedDepth&&object==pair.unit&&user==0) {
            if(!PairCurrent()){result=0;}
            else {
                Candidate* destination=nullptr;
                for(auto& c:pair.rows)if(c.occupied&&c.info==info){destination=&c;break;}
                if(!destination)for(auto& c:pair.rows)if(!c.occupied){destination=&c;break;}
                if(!destination||!SafeCapture(object,info,*destination)) {
                    runtime.refusals.fetch_add(1,std::memory_order_relaxed);result=0;pair.rejected=true;
                }
            }
        }
    } __finally {runtime.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
    return result;
}
void __fastcall RegionsHook(void* model,void* info) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try {
        runtime.regions(model,info); // customization is native's last region writer
        if(pair.active&&!pair.nestedDepth) {
            Candidate* candidate=nullptr;
            for(auto& c:pair.rows)if(c.occupied&&c.info==info){candidate=&c;break;}
            if(candidate) {
                if(!PairCurrent()||pair.fpAdmitted||
                   (!candidate->ready&&!SafePrepare(*candidate))||!Mask(*candidate)) {
                    Hide(info);pair.rejected=true;
                    runtime.refusals.fetch_add(1,std::memory_order_relaxed);
                    if(!pair.wristsValid)runtime.ordering.fetch_add(1,std::memory_order_relaxed);
                } else {candidate->ready=true;pair.ready=true;}
            } else if(SafeFp(model,info)) {
                if(PairCurrent()&&pair.ready)Hide(info);
                else {pair.fpAdmitted=true;pair.rejected=true;}
            }
        }
    } __finally {runtime.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
}
void __fastcall UploadHook(void* mesh,uint8_t flags) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    const UploadScope previous=uploadScope;uploadScope={};
    __try {
        if(PairCurrent())(void)SafeSelect(mesh,uploadScope);
        runtime.upload(mesh,flags);
    } __finally {uploadScope=previous;runtime.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
}
void* __fastcall MapHook(int32_t slot,uint32_t bytes,uint32_t* output) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);void* result=nullptr;
    __try {
        result=runtime.map(slot,bytes,output);
        if(reinterpret_cast<uintptr_t>(_ReturnAddress())==runtime.base+0x2B5512&&
           slot==0&&uploadScope.candidate&&PairCurrent()&&bytes==uploadScope.count*0x30) {
            uploadScope.mapped=result;uploadScope.bytes=bytes;
        }
    } __finally {runtime.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
    return result;
}
__declspec(noinline) bool CopyPrivate() {
    volatile bool fault=false,backedUp=false;bool result=false;
    std::array<uint8_t,halo3_avatar::Capacity*0x30> original{};
    __try {
        const auto& u=uploadScope;
        if(u.candidate&&u.mapped&&u.bytes&&u.bytes<=original.size()&&
           u.count<=halo3_avatar::Capacity&&u.first<=halo3_avatar::Capacity-u.count&&
           u.first<=u.candidate->profile.count&&
           u.count<=u.candidate->profile.count-u.first&&u.bytes==u.count*0x30) {
            std::memcpy(original.data(),u.mapped,u.bytes);backedUp=true;
            std::memcpy(u.mapped,u.candidate->gpu.data()+u.first,u.bytes);result=true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){fault=true;}
    if(fault) {
        if(backedUp) {__try {std::memcpy(uploadScope.mapped,original.data(),uploadScope.bytes);}
            __except(EXCEPTION_EXECUTE_HANDLER) {}}
        Fault();
    }
    return result;
}
void __fastcall CommitHook(int32_t slot,uint32_t baseRegister) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try {
        if(reinterpret_cast<uintptr_t>(_ReturnAddress())==runtime.base+0x2B5538&&
           slot==0&&baseRegister==12&&uploadScope.candidate&&PairCurrent()) {
            if(CopyPrivate())runtime.uploads.fetch_add(1,std::memory_order_relaxed);
            uploadScope.mapped=nullptr; // one private copy for this Map/Unmap
        }
        runtime.commit(slot,baseRegister); // preserve stock exceptions/effects
    } __finally {runtime.callbacks.fetch_sub(1,std::memory_order_acq_rel);}
}
}
