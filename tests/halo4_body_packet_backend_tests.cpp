#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>

#include "../src/dll/halo4_body_packet_backend.inl"

namespace {
constexpr size_t Stride=96,Model=0,Unit=4,Fill=8,Mask=12,Matrix=16,Bytes=16;
constexpr uint8_t BodyFill=1,HandsFill=2;
struct Fixture {
    std::array<uint8_t,Stride*3> rows{};
    bool ownerEvidence=true,preview=true,current=true;
    bool failRead=false;
    std::array<bool,8> failWriteAt{};
    int writes=0,reads=0,nativeCalls=0;
    uint32_t bodyMask=0x11,handsMask=0x22;
    void Row(unsigned i,uint16_t model,uint32_t unit,uint8_t fill,uint32_t mask,uint8_t seed) {
        auto* p=rows.data()+i*Stride;
        std::memcpy(p+Model,&model,sizeof(model));std::memcpy(p+Unit,&unit,sizeof(unit));
        std::memcpy(p+Fill,&fill,sizeof(fill));std::memcpy(p+Mask,&mask,sizeof(mask));
        std::memset(p+Matrix,seed,Bytes);
    }
    static bool Read(void* self,const void* src,void* dst,size_t n) {
        auto& f=*static_cast<Fixture*>(self);++f.reads;
        if(f.failRead||!src||!dst)return false;std::memcpy(dst,src,n);return true;
    }
    bool Write(void* dst,const void* src,size_t n) {
        const int ordinal=writes++;
        if(failWriteAt[static_cast<size_t>(ordinal)]) {
            std::memcpy(dst,src,n/2);return false;
        }
        std::memcpy(dst,src,n);return true;
    }
    auto Run(uint32_t user=0,uint64_t unit=0x12340005,uintptr_t emitted=2) {
        auto classify=[](uint16_t model,uint8_t fill,halo4_body_packet::RowKind& kind) {
            if(model==101&&fill==BodyFill)kind=halo4_body_packet::RowKind::Body;
            else if(model==202&&fill==HandsFill)kind=halo4_body_packet::RowKind::Hands;
            return true;
        };
        auto admit=[&](uint64_t owner){return ownerEvidence&&owner==0x12340005;};
        auto solve=[&](const uint8_t* hands,const uint8_t* before,uint32_t bm,uint32_t hm,
                       uint8_t* after,uint32_t& outBm,uint32_t& outHm) {
            if(!preview||!hands||hands[0]!=0x52||before[0]!=0x31||bm!=bodyMask||hm!=handsMask)return false;
            std::memset(after,0x7A,Bytes);outBm=0x33;outHm=0x44;return true;
        };
        auto still=[&](uint64_t owner){return current&&owner==0x12340005;};
        return halo4_body_packet::Apply<Bytes>(user,unit,3,rows.data(),emitted,3,
            Stride,Model,Unit,Fill,Mask,Matrix,
            [&](const void* s,void* d,size_t n){return Read(this,s,d,n);},
            [&](void* d,const void* s,size_t n){return Write(d,s,n);},
            classify,admit,solve,still);
    }
};
uintptr_t __fastcall NativeProducer(uint32_t,uint64_t,uint32_t,void* context) {
    auto& f=*static_cast<Fixture*>(context);++f.nativeCalls;return 2;
}
void AfterProducer(void* context,uint32_t user,uint64_t unit,uint32_t,
    void*,uintptr_t emitted) {
    auto& f=*static_cast<Fixture*>(context);
    (void)f.Run(user,unit,emitted);
}
void Init(Fixture& f) {
    f=Fixture{};f.Row(0,101,0x12340005,BodyFill,f.bodyMask,0x31);
    f.Row(1,202,0x12340005,HandsFill,f.handsMask,0x52);
    f.Row(2,101,0x99990005,BodyFill,0x99,0x99);
}
int checks=0,failures=0;
void Check(bool pass,const char* label){++checks;if(!pass){++failures;std::printf("FAIL: %s\n",label);}}
}

int main() {
    Fixture f;Init(f);
    Check(f.Run()==halo4_body_packet::Result::Applied,"exact local body and hands rows commit");
    Check(f.rows[Matrix]==0x7A,"body palette candidate written");
    Check(f.rows[Mask]==0x33&&f.rows[Stride+Mask]==0x44,"both region masks commit");

    Init(f);Check(f.Run(0,0x99990005)==halo4_body_packet::Result::MissingRows,
        "foreign owner rows are ignored");
    Init(f);f.ownerEvidence=false;Check(f.Run()==halo4_body_packet::Result::OwnerMismatch,
        "native owner mismatch rejects candidate");
    Init(f);f.preview=false;const auto before=f.rows;
    Check(f.Run()==halo4_body_packet::Result::PreviewRejected&&f.rows==before,
        "private hands preview failure leaves native packet untouched");
    Init(f);f.Row(2,101,0x12340005,BodyFill,f.bodyMask,0x31);
    Check(f.Run(0,0x12340005,3)==halo4_body_packet::Result::AmbiguousRows,
        "duplicate exact local body row is rejected");
    for(int failedWrite=0;failedWrite<3;++failedWrite) {
        Init(f);f.failWriteAt[failedWrite]=true;const auto writeBefore=f.rows;
        Check(f.Run()==halo4_body_packet::Result::WriteFailure&&f.rows==writeBefore,
            "partial matrix or mask write fully restores packet");
    }
    Init(f);f.failWriteAt[1]=true;f.failWriteAt[3]=true;
    const auto rollback= f.Run();
    Check(rollback==halo4_body_packet::Result::RollbackFailure,
        "rollback failure is surfaced as its own result");
    Init(f);const auto emitted=halo4_body_packet::InvokeNativeOnceAndThen(
        NativeProducer,AfterProducer,&f,0,0x12340005,3,f.rows.data());
    Check(emitted==2&&f.nativeCalls==1&&f.rows[Matrix]==0x7A,
        "actual producer boundary invokes native once then mutates returned packet");
    Init(f);Check(f.Run(1)==halo4_body_packet::Result::Ineligible,
        "nonlocal output user is ineligible");
    Init(f);Check(f.Run(0,0x12340005,0)==halo4_body_packet::Result::Ineligible&&f.reads==0,
        "zero emitted rows do not inspect memory");
    Init(f);Check(f.Run(0,0x12340005,4)==halo4_body_packet::Result::Ineligible&&f.reads==0,
        "over-counted native output is rejected before reading");
    Init(f);f.failRead=true;Check(f.Run()==halo4_body_packet::Result::ReadFailure&&f.rows[Matrix]==0x31,
        "unreadable row refuses before packet writes");
    Init(f);f.current=false;const auto stale=f.rows;
    Check(f.Run()==halo4_body_packet::Result::StalePair&&f.rows==stale,
        "stale stereo pair refuses before packet writes");
    Init(f);f.Row(2,202,0x12340005,HandsFill,f.handsMask,0x52);
    Check(f.Run(0,0x12340005,3)==halo4_body_packet::Result::AmbiguousRows,
        "duplicate exact local hands row is rejected");
    std::printf("%d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
