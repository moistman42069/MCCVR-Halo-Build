#pragma once
#include <array>
#include <atomic>
#include <cstdint>

// Immutable, bounded observations. Producers never wait for the log worker.
struct OdstFpMapProbeRecord {
    uint32_t generation=0,checksum=0;
    int view=0,id=0,slot=0,sourceCount=0,tag=0,modelCount=0;
    uint64_t serial=0,epoch=0;
    bool readable=false;
    std::array<int32_t,64> remap{};
    bool SameMap(const OdstFpMapProbeRecord& other) const {
        return generation==other.generation && view==other.view && slot==other.slot &&
            sourceCount==other.sourceCount && tag==other.tag && checksum==other.checksum &&
            modelCount==other.modelCount && readable==other.readable && remap==other.remap;
    }
};
struct OdstFpMapProbe {
    struct Entry { std::atomic<unsigned> state{0}; OdstFpMapProbeRecord record{}; };
    std::array<Entry,128> entries{};
    void Observe(const OdstFpMapProbeRecord& record) {
        for(auto& entry:entries) {
            unsigned state=entry.state.load(std::memory_order_acquire);
            if(state==2 && entry.record.SameMap(record))return;
            if(state==0 && entry.state.compare_exchange_strong(state,1)) {
                entry.record=record;
                entry.state.store(2,std::memory_order_release);
                return;
            }
        }
    }
};
