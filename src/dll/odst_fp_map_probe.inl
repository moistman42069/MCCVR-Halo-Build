// Passive evidence only: never modify a context, map, source bone or palette.
struct OdstFpProbeReceipt {
    const BoneMatrix* source=nullptr;
    OdstFpMapProbeRecord record{};
};
thread_local OdstFpProbeReceipt g_odstFpProbeReceipts[2];
OdstFpMapProbe g_odstFpMapProbe;

void OdstCaptureFpMapProbe(int view,int id,int slot,bool result,BoneMatrix** bones,int* count)
{
    if(slot<0 || slot>1)return;
    auto& receipt=g_odstFpProbeReceipts[slot];receipt={};
    if(!result || !bones || !*bones || !count || *count<=0 || *count>64)return;
    receipt.source=*bones;
    auto& r=receipt.record;
    r.view=view;r.id=id;r.slot=slot;r.sourceCount=*count;
    r.generation=g_odstRuntimeGeneration.load(std::memory_order_acquire);
    if(g_fpStereoSolveScope.armed) {
        r.serial=g_fpStereoSolveScope.anatomicalTracking.serial;
        r.epoch=g_fpStereoSolveScope.anatomicalTracking.referenceEpoch;
    }
}

void OdstObserveFpMapProbe(uint16_t tag,const BoneMatrix* source,const int32_t* map)
{
    for(const auto& receipt:g_odstFpProbeReceipts) {
        if(!source || receipt.source!=source ||
            receipt.record.generation!=g_odstRuntimeGeneration.load(std::memory_order_acquire) ||
            !g_fpStereoSolveScope.armed ||
            receipt.record.serial!=g_fpStereoSolveScope.anatomicalTracking.serial ||
            receipt.record.epoch!=g_fpStereoSolveScope.anatomicalTracking.referenceEpoch)continue;
        auto r=receipt.record;r.tag=tag;
        r.modelCount=OdstReadFpModelIdentity(tag,r.checksum);
        r.readable=r.modelCount>0 && r.modelCount<=64 && map &&
            OdstTryReadMappedPaletteBytes(map,r.remap.data(),size_t(r.modelCount)*sizeof(int32_t));
        g_odstFpMapProbe.Observe(r);
        return;
    }
}

// Only the existing cold worker calls this function; immutable entries need no reset.
void OdstLogFpMapProbe()
{
    static std::array<bool,128> logged{};
    for(size_t i=0;i<logged.size();++i) {
        const auto& entry=g_odstFpMapProbe.entries[i];
        if(logged[i] || entry.state.load(std::memory_order_acquire)!=2)continue;
        const auto& r=entry.record;
        LOG("ODST baseline map probe: generation=%u view=%d id=%d slot=%d sources=%d tag=%d checksum=%08X modelNodes=%d readable=%u serial=%llu epoch=%llu",
            r.generation,r.view,r.id,r.slot,r.sourceCount,r.tag,r.checksum,r.modelCount,r.readable?1u:0u,r.serial,r.epoch);
        if(r.readable) {
            for(int start=0;start<r.modelCount;start+=8) {
                char line[160]{};size_t used=0;
                for(int n=start;n<r.modelCount && n<start+8;++n)
                    used+=size_t(snprintf(line+used,sizeof(line)-used," %d",r.remap[n]));
                LOG("ODST baseline map probe: tag=%d outputs[%d..] -> sources:%s",r.tag,start,line);
            }
        }
        logged[i]=true;
    }
}
