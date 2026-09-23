#pragma once
#include "runtime_types.h"
#include <cstddef>
#include <cstdint>
#include <atomic>

namespace bloom_override {
inline constexpr uint32_t kReachConsumer=0x8A9EA430u;
inline constexpr uint32_t kHalo3OdstConsumer=0x2D28CEF0u;
inline constexpr uint64_t kCacheBudget=64ull*1024*1024;
struct View {uint32_t width{},height{},format{},bytesPerPixel{};};
inline uint32_t Crc32(const void* data,size_t size) noexcept
{
    uint32_t crc=0xFFFFFFFFu;
    const auto* bytes=static_cast<const uint8_t*>(data);
    for(size_t i=0;i<size;++i) {
        crc^=bytes[i];
        for(unsigned bit=0;bit<8;++bit) crc=(crc>>1)^(0xEDB88320u&(0u-(crc&1u)));
    }
    return ~crc;
}
inline bool NearFour(uint32_t large,uint32_t minor) noexcept
{const int64_t delta=int64_t(large)-4ll*minor;return delta>=-3&&delta<=3;}
// Contributor martysl1's final consumer substitution, not the rejected producer
// clear/draw-skip experiments. Return the slots which must change together.
inline unsigned Slots(GameTitle title,uint32_t shader,const View& s0,const View& s1) noexcept
{
    if(title==GameTitle::HaloReach)
        return shader==kReachConsumer&&s1.width==72&&s1.height==45&&s1.bytesPerPixel?2:0;
    if((title!=GameTitle::Halo3&&title!=GameTitle::Halo3ODST)||shader!=kHalo3OdstConsumer)
        return 0;
    return s0.bytesPerPixel&&s1.bytesPerPixel&&s0.format==s1.format&&
        s0.width>=32&&s0.height>=32&&s1.width>=8&&s1.height>=8&&
        NearFour(s0.width,s1.width)&&NearFour(s0.height,s1.height)?3:0;
}
inline uint64_t TextureBytes(uint32_t width,uint32_t height,uint32_t levels,uint32_t bpp) noexcept
{
    if(!width||!height||!levels||levels>15||!bpp||bpp>16||width>16384||height>16384) return 0;
    uint64_t bytes=0;
    for(uint32_t mip=0;mip<levels;++mip) {
        bytes+=uint64_t(width)*height*bpp;
        width=width>1?width/2:1;height=height>1?height/2:1;
    }
    return bytes;
}
// A writer never frees/changes cache entries while a draw can restore one.
// Writers serialize separately on the cold path; neither admission waits.
struct ReaderGate {
    std::atomic<bool> writing{false};std::atomic<unsigned> readers{0};
    bool Enter() noexcept {
        if(writing.load(std::memory_order_seq_cst))return false;
        readers.fetch_add(1,std::memory_order_seq_cst);
        if(!writing.load(std::memory_order_seq_cst))return true;
        readers.fetch_sub(1,std::memory_order_seq_cst);return false;
    }
    void Leave() noexcept {readers.fetch_sub(1,std::memory_order_seq_cst);}
    bool Write() noexcept {
        writing.store(true,std::memory_order_seq_cst);
        if(readers.load(std::memory_order_seq_cst)==0)return true;
        writing.store(false,std::memory_order_seq_cst);return false;
    }
    void EndWrite() noexcept {writing.store(false,std::memory_order_seq_cst);}
};
}
