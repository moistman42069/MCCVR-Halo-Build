#include <windows.h>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
static uintptr_t testReturnAddress=0;
#define _ReturnAddress() reinterpret_cast<void*>(testReturnAddress)
#include "../src/dll/halo3_avatar_backend.inl"
#undef _ReturnAddress
namespace {
unsigned checks=0,failures=0;
void Check(bool value,const char* name){++checks;if(!value){++failures;std::printf("FAIL: %s\n",name);}}
using namespace halo3_avatar_native;
std::array<uint8_t,0x60> body{},fp{},other{};
std::array<uint8_t,halo3_avatar::Capacity*0x30> mapped{},submitted{};
bool live=true,prepare=true,sourceFault=false,sourceResult=true,stockThrow=false,identityReplace=false;
unsigned stockRegions=0,stockProducer=0,stockSubmit=0,stockUpload=0,stockMap=0,stockCommit=0;
uint32_t drawRegionCount=0;int16_t drawHead=0;
constexpr DWORD NativeException=0xE042A713;
template<class T>void Put(std::array<uint8_t,0x60>& data,size_t at,T value){std::memcpy(data.data()+at,&value,sizeof(value));}
template<class T>T Get(const void* data,size_t at=0){T v{};std::memcpy(&v,static_cast<const uint8_t*>(data)+at,sizeof(v));return v;}
uint64_t __fastcall NativeExclude(int32_t,uint32_t){return 1;}
uint8_t __fastcall NativeProducer(uint32_t,float,uint32_t,uint32_t,void*){++stockProducer;return 1;}
void __fastcall NativeRegions(void*,void*){++stockRegions;}
void* __fastcall NativeMap(int32_t,uint32_t,uint32_t*){++stockMap;return mapped.data();}
void __fastcall NativeCommit(int32_t,uint32_t){++stockCommit;submitted=mapped;}
void __fastcall NativeUpload(void*,uint8_t) {
    ++stockUpload;uint32_t output=0;
    const auto before=testReturnAddress;testReturnAddress=runtime.base+0x2B5512;
    auto* dst=MapHook(0,2*0x30,&output);std::memset(dst,0x17,2*0x30);
    testReturnAddress=runtime.base+0x2B5538;CommitHook(0,12);testReturnAddress=before;
}
void SubmitBody(void* info) {
    ++stockSubmit;RegionsHook(nullptr,info);
    drawRegionCount=Get<uint32_t>(info);drawHead=Get<int16_t>(info,0x12);
    if(drawRegionCount)UploadHook(info,0);
    if(identityReplace) *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(info)+0x48)=0xDEAD0004;
    if(stockThrow)RaiseException(NativeException,0,0,nullptr);
}
void __fastcall NativeA(uintptr_t,void* info,uint64_t,uint8_t,uint8_t,uint8_t){SubmitBody(info);}
void __fastcall NativeB(uintptr_t,void* info,uint8_t,uint8_t){SubmitBody(info);}
void Reset() {
    pair={};uploadScope={};regionScope={};runtime.base=0x180000000;runtime.generation.store(7);
    runtime.enabled.store(true);runtime.faulted.store(false);runtime.callbacks.store(0);
    runtime.exclude=NativeExclude;runtime.producer=NativeProducer;runtime.regions=NativeRegions;
    runtime.upload=NativeUpload;runtime.map=NativeMap;runtime.commit=NativeCommit;
    runtime.submitA=NativeA;runtime.submitB=NativeB;
    live=prepare=sourceResult=true;sourceFault=stockThrow=identityReplace=false;
    stockRegions=stockProducer=stockSubmit=stockUpload=stockMap=stockCommit=0;
    body={};fp={};other={};mapped={};submitted={};
    for(auto* data:{&body,&fp,&other}) {
        Put(*data,0,uint32_t(6));Put(*data,4,uint32_t(0xA5000009));Put(*data,8,uint32_t(0x700));
        Put(*data,0x48,uint32_t(0xAA000005));
        for(unsigned i=0;i<6;++i)Put(*data,0xE+2*i,int16_t(i));
    }
    Put(fp,0x58,uint32_t(0x10));Put(other,0x48,uint32_t(0xBB000005));
    pair.active=true;pair.unit=0xAA000005;pair.generation=7;pair.serial=100;pair.epoch=3;pair.wristsValid=true;
    testReturnAddress=runtime.base+0x252C7A;
}
bool NativeSubmitThrows() {
    volatile bool observed=false;
    __try{SubmitAHook(0,body.data(),0,0,0,0);}
    __except(GetExceptionCode()==NativeException?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){observed=true;}
    return observed;
}
}
namespace halo3_avatar_native {
bool Current() noexcept{return live&&runtime.enabled.load()&&!runtime.faulted.load();}
bool Capture(uint32_t unit,void* info,Candidate& out) {
    if(sourceFault){volatile auto ignored=*reinterpret_cast<volatile uint32_t*>(uintptr_t(1));(void)ignored;}
    if(!sourceResult)return false;
    Candidate c{};c.info=info;c.unit=unit;c.tag=Get<uint32_t>(info,4);c.designator=Get<uint32_t>(info,8);
    if(!halo3_avatar::SelectProfile(0x17121B0D,51,c.profile))return false;
    c.occupied=true;out=c;return true;
}
bool Prepare(Candidate& c) {
    if(!prepare||!pair.wristsValid)return false;
    for(auto& m:c.gpu)for(auto& x:m)x=4.25f;
    c.ready=true;return true;
}
bool IsOwnFpRow(void*,void* info){return (Get<uint32_t>(info,0x58)&0x10)!=0&&Get<uint32_t>(info,0x48)==pair.unit;}
bool SelectUpload(void* mesh,UploadScope& scope) {
    for(auto& c:pair.rows)if(c.info==mesh&&c.ready){scope={&c,1,2,nullptr,0};return true;}
    return false;
}
}
int main() {
    using namespace halo3_avatar_native;
    Reset();Check(BeginPairState(),"first render pair lease acquired");
    pair.active=true;pair.unit=0xAA000005;pair.generation=7;pair.serial=100;pair.epoch=3;
    FpReceipt receipt{};receipt.unit=pair.unit;receipt.generation=7;receipt.serial=100;receipt.epoch=3;
    receipt.nonce=pair.nonce;receipt.valid=true;receipt.checksum=504041493;receipt.count=37;
    AdoptReceipt(receipt);Check(pair.wristsValid,"current leased observer receipt admitted");
    EndPairState();Check(runtime.callbacks.load()==0,"pair lease drained");
    Check(BeginPairState(),"second render pair lease acquired");
    pair.active=true;pair.unit=0xAA000005;pair.generation=7;pair.serial=100;pair.epoch=3;
    AdoptReceipt(receipt);Check(!pair.wristsValid,"same tracking serial cannot revive earlier render pair wrists");
    receipt.nonce=pair.nonce;AdoptReceipt(receipt);Check(pair.wristsValid,"new pair own observer resumes avatar");
    Check(!BeginPairState()&&!PairCurrent(),"nested render cannot use outer avatar ownership");
    EndPairState();Check(PairCurrent(),"nested exit restores outer pair");
    EndPairState();Check(runtime.callbacks.load()==0&&!pair.leased,"all pair leases released");
    Reset();Check(ExcludeHook(0,pair.unit)==0,"exact own full local handle admitted");
    Check(ExcludeHook(0,0xBB000005)==1&&ExcludeHook(1,pair.unit)==1,"same low index remote salt and other user preserved");
    testReturnAddress=runtime.base+0x252C79;Check(ExcludeHook(0,pair.unit)==1,"unrelated predicate call site retained");
    Reset();const auto nativeBody=body;
    Check(ProducerHook(pair.unit,1,0,0,body.data())==1,"successful local world row captured after native producer");
    SubmitAHook(0,body.data(),0,0,0,0);
    Check(stockProducer==1&&stockRegions==1&&stockSubmit==1&&stockUpload==1&&stockMap==1&&stockCommit==1,"every original called once");
    Check(drawRegionCount==6&&drawHead==-1,"head removed only inside native region submission");
    Check(body==nativeBody,"all avatar region writes restored after submission");
    float uploaded{};std::memcpy(&uploaded,submitted.data(),4);
    Check(uploaded==4.25f,"native Map buffer replaced before native commit");
    Check(pair.ready&&runtime.callbacks.load()==0,"prepared avatar and drained callbacks");
    const auto nativeFp=fp;SubmitBHook(0,fp.data(),0,0);
    Check(drawRegionCount==0&&fp==nativeFp,"only same-pair FP geometry hidden then count restored");
    const auto beforeOther=other;SubmitAHook(0,other.data(),0,0,0,0);
    Check(drawRegionCount==6&&other==beforeOther,"remote draw remains stock");
    Check(submitted[0]==0x17,"unowned Map/commit retains native upload");
    Check(ProducerHook(pair.unit,1,0,0,body.data())==1,"second eye captures regenerated local row");
    SubmitBHook(0,body.data(),0,0);Check(body==nativeBody&&drawHead==-1,"second eye native state not contaminated");
    Reset();pair.wristsValid=false;ProducerHook(pair.unit,1,0,0,body.data());SubmitAHook(0,body.data(),0,0,0,0);
    Check(pair.rejected&&drawRegionCount==0&&body==nativeBody,"missing current wrists drop only optional world body");
    SubmitBHook(0,fp.data(),0,0);Check(drawRegionCount==6,"refused avatar preserves working FP geometry");
    Reset();SubmitBHook(0,fp.data(),0,0);Check(pair.fpAdmitted&&pair.rejected,"FP-first ordering refuses whole-pair avatar");
    Check(ProducerHook(pair.unit,1,0,0,body.data())==0,"later world row cannot create doubled hands");
    Reset();sourceFault=true;
    Check(ProducerHook(pair.unit,1,0,0,body.data())==0&&runtime.faulted.load()&&stockProducer==1,"actual source AV publishes optional fault after stock producer");
    Check(runtime.callbacks.load()==0,"faulted producer releases callback");
    Reset();ProducerHook(pair.unit,1,0,0,body.data());stockThrow=true;
    Check(NativeSubmitThrows(),"native submit exception propagates unchanged");
    Check(body==nativeBody&&runtime.callbacks.load()==0&&!regionScope.info,"native exception restores region bytes and all scopes");
    Reset();ProducerHook(pair.unit,1,0,0,body.data());identityReplace=true;SubmitAHook(0,body.data(),0,0,0,0);
    Check(Get<uint32_t>(body.data(),0x48)==0xDEAD0004&&Get<int16_t>(body.data(),0x12)==-1,"reused native row is never overwritten during restoration");
    Reset();Check(!SafeFp(nullptr,reinterpret_cast<void*>(uintptr_t(1)))&&runtime.faulted.load(),"actual identity AV isolated");
    Reset();regionScope.info=reinterpret_cast<void*>(uintptr_t(1));
    Check(!Hide(regionScope.info)&&runtime.faulted.load(),"actual hide AV isolated");
    Reset();Candidate c{};c.info=reinterpret_cast<void*>(uintptr_t(1));halo3_avatar::SelectProfile(0x17121B0D,51,c.profile);
    Check(!Mask(c)&&runtime.faulted.load(),"actual mask AV isolated");
    Reset();ProducerHook(pair.unit,1,0,0,body.data());runtime.generation.store(8);
    SubmitAHook(0,body.data(),0,0,0,0);Check(drawRegionCount==0&&!pair.ready,"generation rollover cannot consume prior pose");
    Reset();ProducerHook(pair.unit,1,0,0,body.data());SafePrepare(pair.rows[0]);
    SYSTEM_INFO system{};GetSystemInfo(&system);const size_t page=system.dwPageSize;
    auto* memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(memory!=nullptr,"allocate actual mixed-protection GPU fixture");
    if(memory) {
        auto* destination=memory+page-48;std::memset(destination,0x65,96);DWORD old=0;
        Check(VirtualProtect(memory+page,page,PAGE_READONLY,&old)!=0,"protect second GPU page");
        uploadScope={&pair.rows[0],1,2,destination,96};
        Check(!CopyPrivate()&&runtime.faulted.load(),"partial private GPU AV isolates optional body");
        bool original=true;for(unsigned i=0;i<96;++i)original&=destination[i]==0x65;
        Check(original,"bounded GPU before-image restores writable partial copy");
        VirtualFree(memory,0,MEM_RELEASE);
    }
    Reset();memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(memory!=nullptr,"allocate actual mixed-protection region fixture");
    if(memory) {
        auto* info=memory+page-22;std::memcpy(info,body.data(),body.size());DWORD old=0;
        Check(VirtualProtect(memory+page,page,PAGE_READONLY,&old)!=0,"protect region tail and identity page");
        Candidate interrupted{};interrupted.info=info;
        halo3_avatar::SelectProfile(0x17121B0D,51,interrupted.profile);
        regionScope={};regionScope.info=info;pair.hideLower=true;
        Check(!Mask(interrupted)&&runtime.faulted.load(),"partial region write AV isolated");
        Check(std::memcmp(info,body.data(),body.size())==0,"interrupted region write restores entire before-image and count");
        RestoreRegions();Check(std::memcmp(info,body.data(),body.size())==0,"later finally preserves already-restored row");
        VirtualFree(memory,0,MEM_RELEASE);
    }
    std::printf("H3 avatar runtime: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
