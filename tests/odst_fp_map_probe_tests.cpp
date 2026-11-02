#include "../src/common/odst_fp_map_probe.h"
#include <cassert>
#include <thread>
#include <cstdio>
#include <cstring>
struct BoneMatrix { float value=0; };
std::atomic<uint32_t> g_odstRuntimeGeneration{1};
struct Tracking { uint64_t serial=1,referenceEpoch=1; };
struct Scope { bool armed=true; Tracking anatomicalTracking; } g_fpStereoSolveScope;
int OdstReadFpModelIdentity(uint16_t,uint32_t& checksum) {checksum=123;return 3;}
bool OdstTryReadMappedPaletteBytes(const void* src,void* dst,size_t size)
{if(!src)return false;std::memcpy(dst,src,size);return true;}
template<class... Args> void ProbeTestLog(const char*,Args...) {}
#define LOG ProbeTestLog
#include "../src/dll/odst_fp_map_probe.inl"
#undef LOG

int main()
{
    BoneMatrix bones[43]{};bones[0].value=9;
    BoneMatrix* source=bones;int count=43;int32_t map[]{38,39,40};
    OdstCaptureFpMapProbe(0,4,0,true,&source,&count);
    OdstObserveFpMapProbe(9,source,map);
    assert(g_odstFpMapProbe.entries[0].state.load()==2);
    assert(g_odstFpMapProbe.entries[0].record.remap[0]==38);
    assert(bones[0].value==9 && map[0]==38);
    ++g_fpStereoSolveScope.anatomicalTracking.serial;
    OdstObserveFpMapProbe(10,source,map);
    assert(g_odstFpMapProbe.entries[1].state.load()==0);
    OdstCaptureFpMapProbe(0,5,0,false,&source,&count);
    OdstObserveFpMapProbe(11,source,map);
    assert(g_odstFpMapProbe.entries[1].state.load()==0);
    OdstLogFpMapProbe();
    OdstFpMapProbe probe;
    OdstFpMapProbeRecord body{};
    body.generation=1;body.tag=7;body.sourceCount=43;body.modelCount=37;
    body.readable=true;body.remap[1]=2;
    probe.Observe(body);
    body.id=100;body.serial=200;probe.Observe(body);
    assert(probe.entries[0].state.load()==2);
    assert(probe.entries[1].state.load()==0); // Frame changes cannot exhaust the buffer.
    body.remap[1]=1;probe.Observe(body);
    assert(probe.entries[1].state.load()==2); // Same tag with a changed map is retained.
    assert(probe.entries[0].record.remap[1]==2);
    body.readable=false;probe.Observe(body);
    assert(probe.entries[2].state.load()==2);
    std::array<std::thread,4> producers;
    for(int t=0;t<4;++t)producers[t]=std::thread([&,t] {
        for(int i=0;i<100;++i) {
            OdstFpMapProbeRecord r{};r.tag=100+t*100+i;r.remap[0]=r.tag;
            probe.Observe(r);
        }
    });
    for(auto& thread:producers)thread.join();
    for(const auto& e:probe.entries) {
        assert(e.state.load()==2);
        if(e.record.tag>=100)assert(e.record.remap[0]==e.record.tag);
    }
    assert(probe.entries[0].record.remap[1]==2); // Full buffers never overwrite evidence.
    std::puts("ODST passive map probe: deduplication, changed maps, unreadable data, concurrent publication and capacity passed");
}
