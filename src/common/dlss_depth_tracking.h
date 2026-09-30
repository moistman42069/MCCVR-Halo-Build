#pragma once
#include <cstddef>

namespace dlss {
// Called only at frame retirement, after both eye consumers have finished.
// Keep recurring views pinned. Under pressure, let obsolete views go so a
// replacement scene buffer can be learned without a resize or title reload.
// Entry owns its view and supplies seenThisFrame; release relinquishes it.
template<class Entry, class Release>
std::size_t FinishDepthViewFrame(Entry* entries, std::size_t count,
                                bool pressure, Release release) {
    std::size_t kept=0;
    for(std::size_t i=0;i<count;++i) {
        if(pressure&&!entries[i].seenThisFrame) {
            release(entries[i]);
        } else {
            if(kept!=i) entries[kept]=entries[i];
            entries[kept++].seenThisFrame=false;
        }
    }
    for(std::size_t i=kept;i<count;++i) entries[i]={};
    return kept;
}
}
