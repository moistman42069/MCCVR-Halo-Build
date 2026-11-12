// Included inside native_reload_policy.cpp's private namespace.
// Layouts and native ABIs are independently verified per title; see the tail
// evidence document. All of this runs within an already counted simulation hook.
struct TailLayout {
    uint32_t userStride,slotStride;
    uint16_t tlsMember,weapon,channel,graph,index,identity,frame,initialized;
};
static constexpr TailLayout kTailLayout[]{
    {0x1e94,0,0,8,0,0,0x18,0xc,0x1a,0},
    {0x20fc,0x1028,0,0x10,0x14,0,6,0x88,0x20,0},
    {0x2430,0x11bc,0x568,0x3c,0x5c,0,10,4,0x18,0x11},
    {0x4f38,0x2740,0x598,0x3c,0x5c,0,10,4,0x18,0x11},
    {0x53a8,0x2978,0x6a0,0x3c,0x60,0,10,4,0x24,0x1e},
    {0x5f48,0x2ec8,0x6a0,0x6c,0xb8,4,14,8,0x28,0x22},
};
template<class T> T TailRead(const uint8_t* p,size_t offset=0)
{ T value;std::memcpy(&value,p+offset,sizeof value);return value; }
uint8_t* NativeTailUsers(unsigned i)
{
    const auto& r=runtime[i];const auto& l=kTailLayout[i];
    if(!l.tlsMember) return *reinterpret_cast<uint8_t**>(r.base+kTailBindings[i].globals[0]);
    auto** slots=reinterpret_cast<uint8_t**>(__readgsqword(0x58));
    if(!slots || !r.tlsIndex) return nullptr;
    auto* tls=slots[*r.tlsIndex];
    return tls?TailRead<uint8_t*>(tls,l.tlsMember):nullptr;
}
uint8_t* CeTailTagBlock(uint32_t pointer)
{
    if(!pointer) return nullptr;
    const auto& r=runtime[native_reload::CE];const auto& b=kTailBindings[native_reload::CE];
    const auto start=*reinterpret_cast<intptr_t*>(r.base+b.globals[1]);
    const auto displacement=*reinterpret_cast<intptr_t*>(r.base+b.globals[2]);
    if(start<=0) return nullptr;
    const intptr_t serialized=intptr_t(int32_t(pointer));
    if(displacement>serialized || displacement<serialized-0x40000000) return nullptr;
    const intptr_t relative=serialized-displacement;
    if(relative<0 || relative>0x40000000 || start>std::numeric_limits<intptr_t>::max()-relative) return nullptr;
    return reinterpret_cast<uint8_t*>(start+relative);
}
bool TryKeepReloadTail(const TailCapture& capture,unsigned& stage)
{
    const unsigned i=capture.title;auto& r=runtime[i];const auto& l=kTailLayout[i];
    if(!r.tailReady || !r.tailUsers || !capture.played || capture.ambiguous || capture.user>=4 ||
        capture.slot>=2 || capture.magazine<0 || capture.magazine>=2 ||
        !Current(i,4) || Current(i,2)) return false;
    stage=2;
    auto* weapon=static_cast<uint8_t*>(Game_ReloadPolicyWeapon(kBindings[i].title,capture.weapon));
    if(!weapon) return false;
    auto* users=r.tailUsers(i);
    if(!users) return false;
    uint32_t userStride=l.userStride,slotStride=l.slotStride;
    if(i==native_reload::H3) Game_Halo3FpStrides(userStride,slotStride); // node capacity per build (game.cpp)
    auto* user=users+capture.user*userStride;
    auto* slot=user+capture.slot*slotStride;
    if(TailRead<uint32_t>(user,4)==UINT32_MAX || TailRead<uint32_t>(slot,l.weapon)!=capture.weapon) return false;
    stage=4;
    auto* channel=slot+l.channel;
    const auto& magazine=native_reload::layouts[i];
    const size_t magazineBase=magazine.base+capture.magazine*magazine.stride;
    std::span<uint8_t> datum{weapon,size_t(magazine.base)+2*magazine.stride};
    const int total=TailRead<int16_t>(weapon,magazineBase+2);
    if(total<2) {stage=8;return false;}
    int elapsed=0,frame=0;
    if(i==native_reload::CE) {
        if(TailRead<int16_t>(channel,l.identity)!=int16_t(capture.animation) ||
            TailRead<int16_t>(channel,l.frame)!=0) return false;
        stage=16;
        const int index=TailRead<int16_t>(channel,l.index);
        auto getTag=reinterpret_cast<uint8_t*(__fastcall*)(uint32_t)>(r.tailFunctions[0]);
        auto* tag=getTag(TailRead<uint32_t>(weapon));
        if(!tag || TailRead<uint32_t>(tag,0x478)==UINT32_MAX) return false;
        auto* graph=getTag(TailRead<uint32_t>(tag,0x478));
        if(!graph) return false;
        const int count=TailRead<int32_t>(graph,0x74);
        if(index<0 || count<=index || count>16384) return false;
        auto* animations=CeTailTagBlock(TailRead<uint32_t>(graph,0x78));
        if(!animations) return false;
        auto* animation=animations+size_t(index)*0xb4;
        const int length=TailRead<int16_t>(animation,0x22);
        if(length<2) return false;
        frame=TailRead<int16_t>(animation,0x34);
        // Some stock animations have no interior insertion keyframe. In that
        // case retain the final quarter of this exact selected animation.
        if(frame<=0 || frame>=length) frame=length*3/4;
        elapsed=total*frame/length;
        if(!native_reload::TailTicks(i,datum,capture.magazine,elapsed)) return false;
        // CE's first-person player stores its position in this single short.
        // Native play and update use the same counter; no modern channel cache.
        const int16_t position=int16_t(frame);
        std::memcpy(channel+l.frame,&position,2);
    } else {
        if(TailRead<uint32_t>(channel,l.graph)==UINT32_MAX || TailRead<int16_t>(channel,l.index)<0 ||
            TailRead<uint32_t>(channel,l.identity)!=capture.animation ||
            (l.initialized && !TailRead<uint8_t>(channel,l.initialized)) ||
            TailRead<float>(channel,l.frame)!=0.0f) return false;
        stage=16;
        auto* animation=reinterpret_cast<uint8_t*(__fastcall*)(void*)>(r.tailFunctions[0])(channel);
        if(!animation) return false;
        frame=int16_t(reinterpret_cast<int(__fastcall*)(void*,uint16_t)>(r.tailFunctions[1])(
            animation,i>=native_reload::Reach?1:0));
        elapsed=TailRead<int16_t>(weapon,magazineBase+(i==native_reload::H2?12:16));
        int length=0;
        if(i==native_reload::H2) length=TailRead<int16_t>(animation,0x14);
        else if(i==native_reload::H3 || i==native_reload::ODST) length=TailRead<int16_t>(animation,0x10);
        else if(r.tailFunctions[3]) {
            if(i==native_reload::Reach) {
                // Own native remaining-frame query; the initial frame is zero.
                const float remaining=reinterpret_cast<float(__fastcall*)(void*)>(r.tailFunctions[3])(channel);
                if(!std::isfinite(remaining) || remaining<2 || remaining>32767) return false;
                length=int(remaining);
            } else length=reinterpret_cast<int(__fastcall*)(void*)>(r.tailFunctions[3])(channel);
        }
        if(length<2 || length>32767) return false;
        // Native mode-3 duration falls back to the entire animation when the
        // insertion event is absent. Treating that as elapsed rejected every
        // such reload. Use an explicit final-section policy instead.
        if(frame<=0 || frame>=length || elapsed<=0 || elapsed>=total) {
            frame=length*3/4;
            elapsed=total*frame/length;
        }
        if(!native_reload::TailTicks(i,datum,capture.magazine,elapsed)) return false;
        stage=32;
        if(i==native_reload::H4)
            reinterpret_cast<void(__fastcall*)(void*,float,void*)>(r.tailFunctions[2])(channel,float(frame),channel);
        else reinterpret_cast<void(__fastcall*)(void*,float)>(r.tailFunctions[2])(channel,float(frame));
        if(TailRead<float>(channel,l.frame)!=float(frame)) {
            // A malformed/custom event beyond the animation was clamped by the
            // engine. Restore the initial position and retain the full reload.
            if(i==native_reload::H4)
                reinterpret_cast<void(__fastcall*)(void*,float,void*)>(r.tailFunctions[2])(channel,0.0f,channel);
            else reinterpret_cast<void(__fastcall*)(void*,float)>(r.tailFunctions[2])(channel,0.0f);
            return false;
        }
    }
    stage=8;
    return native_reload::RetainTail(i,datum,capture.magazine,elapsed);
}
void KeepReloadTail(const TailCapture& capture)
{
    auto& r=runtime[capture.title];bool kept=false;unsigned stage=1;
    __try {kept=TryKeepReloadTail(capture,stage);}
    __except(EXCEPTION_EXECUTE_HANDLER) {
        r.faults.fetch_add(1,std::memory_order_relaxed);
        r.tailFaulted.store(true,std::memory_order_release);
    }
    if(!kept) r.tailFallbackMask.fetch_or(stage,std::memory_order_relaxed);
    (kept?r.tails:r.tailFallback).fetch_add(1,std::memory_order_relaxed);
}
