#include "../src/dll/physical_crouch_camera.h"
#include "../src/common/physical_crouch_cached_read.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include "../src/dll/halo3_physical_crouch_reader.inl"

namespace {
unsigned checks;
void Check(bool result,const char* name)
{++checks;if(!result){std::fprintf(stderr,"FAIL: %s\n",name);std::exit(1);}}
bool Near(float a,float b){return std::abs(a-b)<.00001f;}
template<class T,size_t N> void Put(std::array<uint8_t,N>& data,size_t offset,T value)
{std::memcpy(data.data()+offset,&value,sizeof(value));}
}

int main()
{
    for(unsigned title=1;title<=6;++title)
    {
        const auto game=static_cast<GameTitle>(title);
        PhysicalCrouchCameraRequest request{game,7,9,1000,true,true};
        PhysicalCrouchCameraLease lease;
        auto update=[&](uint64_t now,float native,float physical,uint32_t unit=0x120003u,
            uintptr_t storage=0x10000u){return lease.Update(request,game,7,9,now,unit,storage,native,physical);};
        Check(Near(update(1000,.4f,.22f),.22f),"correction cannot exceed tracked lowering");
        Check(Near(update(1000,.1f,.5f),.1f),"partial native blend uses actual amount");
        request.requested=false;
        Check(Near(update(1010,.08f,.2f),.08f),"native release blend retains owned tail");
        Check(Near(update(1020,.08f,0.f),0.f),"standing head receives no upward correction");
        Check(Near(update(1101,.08f,.2f),0.f),"stale input cancels tail");
        request.at=1101;
        Check(Near(update(1101,.08f,.2f),0.f),"fresh release cannot revive cancelled tail");
        request.requested=true;update(1101,.08f,.2f);request.requested=false;
        Check(Near(update(1101,.08f,.2f,0x130003u),0.f),"new salted owner drops tail");
        request.requested=true;update(1101,.08f,.2f);request.requested=false;
        Check(Near(update(1101,.08f,.2f,0x120003u,0x20000u),0.f),"recycled unit storage drops tail");
        request.requested=true;update(1101,.08f,.2f);request.available=false;
        Check(Near(update(1101,.08f,.2f),0.f),"tracking admission loss cancels immediately");
        request.available=true;request.requested=false;
        Check(Near(update(1101,.08f,.2f),0.f),"tracking return cannot replay old gesture");
        request.requested=true;request.epoch=10;
        Check(Near(update(1101,.08f,.2f),0.f),"recenter identity mismatch refused");
        request.epoch=9;request.generation=8;
        Check(Near(update(1101,.08f,.2f),0.f),"title generation mismatch refused");
        request.generation=7;
        Check(Near(update(1101,std::numeric_limits<float>::quiet_NaN(),.2f),0.f),"nonfinite native amount refused");
        PhysicalCrouchCamera_Publish(game,7,9,2000,true,true);
        PhysicalCrouchCameraRequest read{};
        Check(PhysicalCrouchCamera_Read(read)&&read.title==game&&read.generation==7&&
            read.epoch==9&&read.at==2000&&read.requested,"actual coherent publication preserves receipt");
        PhysicalCrouchCamera_Invalidate();
        Check(!PhysicalCrouchCamera_Read(read),"actual invalidation retires snapshot");
        PhysicalCrouchCamera_Publish(game,7,9,2001,true,false);
        Check(PhysicalCrouchCamera_Read(read)&&!read.requested,"later valid release receipt admitted");
    }
    for(const auto& layout:{physical_crouch_cached::reach,physical_crouch_cached::halo4})
    {
        std::array<uint8_t,0x6000> bytes{};
        std::array<uint8_t,0x3000> tags{};
        std::array<uint8_t,32> table{};
        uintptr_t pages[16]{};pages[0]=reinterpret_cast<uintptr_t>(tags.data());
        Put(bytes,layout.tag,uint32_t(1));Put(table,12,uint32_t(0x100));
        Put(bytes,layout.seat,int16_t(-1));Put(bytes,layout.physics,uint16_t(0x1800));
        Put(bytes,layout.animation,uint16_t(0x2000));Put(bytes,layout.scale,1.f);
        Put(tags,0x400+layout.standing,1.f);Put(tags,0x400+layout.crouched,.4f);
        Put(bytes,layout.height,.7f);
        const uint8_t* definition=nullptr;float amount=-1;
        auto read=[&](){return physical_crouch_cached::Reduction(layout,bytes.data(),table.data(),pages,amount,definition);};
        Check(read()&&Near(amount,.3f)&&definition==tags.data()+0x400,"actual cached-height reader follows native blend");
        Put(bytes,layout.scale,2.f);Check(read()&&Near(amount,.6f),"cached native height applies own model scale");
        Put(bytes,layout.scale,1.f);Put(tags,0x400+layout.overrides,int32_t(2));
        Put(tags,0x404+layout.overrides,uint32_t(0x400));
        Put(tags,0x1000,uint32_t(41));Put(tags,0x1004,.9f);Put(tags,0x1008,.3f);
        Put(tags,0x100c,uint32_t(42));Put(tags,0x1010,1.2f);Put(tags,0x1014,.6f);
        Put(bytes,0x2000+layout.animationCamera,uint32_t(42));Put(bytes,layout.height,1.1f);
        Check(read()&&Near(amount,.1f),"native animation camera override chooses matching second record");
        Put(bytes,0x2000+layout.animationCamera,uint32_t(99));
        Check(!read(),"camera transition beyond selected native range refused");
        Put(bytes,layout.height,.7f);Check(read()&&Near(amount,.3f),"unknown camera SID preserves native base-height fallback");
        Put(tags,0x400+layout.overrides,int32_t(257));Check(!read(),"unbounded custom camera table refused");
        Put(tags,0x400+layout.overrides,int32_t(0));
        Put(bytes,0x1800,uint32_t(7));Check(!read(),"native tilted-up cached camera refused");Put(bytes,0x1800,uint32_t(0));
        Put(bytes,layout.cameraFlags,uint32_t(4));Check(!read(),"native alternate camera selector refused");Put(bytes,layout.cameraFlags,uint32_t(0));
        Put(bytes,layout.seat,int16_t(0));Check(!read(),"seated cached camera refused");Put(bytes,layout.seat,int16_t(-1));
        Put(bytes,layout.height,std::numeric_limits<float>::quiet_NaN());Check(!read(),"nonfinite native cache refused");Put(bytes,layout.height,.7f);
        Put(bytes,layout.animation,uint16_t(0));Check(!read(),"absent animation component refused");Put(bytes,layout.animation,uint16_t(0x2000));
        Put(table,12,uint32_t(0x10000100));pages[1]=reinterpret_cast<uintptr_t>(tags.data())-uintptr_t(0x10000000)*4;
        Check(read()&&Near(amount,.3f),"native encoded page high bits select bank and preserve full address arithmetic");
        pages[1]=0;Check(!read(),"unloaded tag page refused");
    }
    std::array<uint8_t,0x1800> unit{};
    std::array<uint8_t,0x400> tag{};
    Put(unit,0x10,int32_t(-1));Put(unit,0x162,int16_t(0x600));
    for(float scale:{.5f,1.f,2.f}) for(float standing:{.4f,.7f,1.4f})
        for(float fraction:{0.f,.01f,.25f,.5f,.75f,1.f})
    {
        Put(unit,0x8c,scale);Put(unit,0x384,fraction);
        Put(tag,0x2e8,standing);Put(tag,0x2ec,standing*.55f);
        float reduction=-1;
        Check(Halo3PhysicalCrouchReduction(unit.data(),tag.data(),reduction)&&
            Near(reduction,standing*.45f*fraction*scale),"actual H3 native layout/magnitude");
    }
    auto valid=[&](){float reduction;return Halo3PhysicalCrouchReduction(unit.data(),tag.data(),reduction);};
    Put(unit,0x10,int32_t(0x10004));Check(!valid(),"parented H3 owner refused");Put(unit,0x10,int32_t(-1));
    unit[0x96]=1;Check(!valid(),"non-biped refused");unit[0x96]=0;
    Put(unit,0x110,uint32_t(4));Check(!valid(),"native alternate camera branch refused");Put(unit,0x110,uint32_t(0));
    for(uint32_t mode:{7,8}){Put(unit,0x600,mode);Check(!valid(),"alternate native up modes refused");}
    Put(unit,0x600,uint32_t(0));
    unit[0x4de]=6;Put(unit,0x4e4,uint32_t(0x10));float reduction=-1;
    Check(Halo3PhysicalCrouchReduction(unit.data(),tag.data(),reduction)&&reduction==0.f,"native special-state fraction suppression preserved");
    unit[0x4de]=0;Put(unit,0x384,1.01f);Check(!valid(),"fraction bounds enforced");
    Put(unit,0x384,.5f);Put(unit,0x8c,std::numeric_limits<float>::infinity());Check(!valid(),"nonfinite scale refused");
    Put(unit,0x8c,1.f);Put(tag,0x2ec,5.f);Check(!valid(),"inverted custom height range refused");
    Put(unit,0x162,int16_t(-1));Check(!valid(),"invalid physics component rejected");
    std::printf("Physical crouch camera: %u checks passed\n",checks);
}
