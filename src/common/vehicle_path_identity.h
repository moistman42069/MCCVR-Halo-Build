#pragma once
#include <cstddef>
#include <cstdint>

namespace vehicle_identity {
// Bounded native tag names, never object/definition handles. Case and slash
// normalization makes the same authored path stable across cache rebuilds.
inline uint64_t Path(const char* name,size_t capacity=256) noexcept
{
    if(!name||!capacity||capacity>256)return 0;
    uint64_t hash=14695981039346656037ull;
    for(size_t i=0;i<capacity;++i)
    {
        unsigned char c=static_cast<unsigned char>(name[i]);
        if(!c)return i?hash:0;
        if(c<32||c>126)return 0;
        if(c>='A'&&c<='Z')c+=32;
        if(c=='/')c='\\';
        hash^=c;hash*=1099511628211ull;
    }
    return 0;
}
}
