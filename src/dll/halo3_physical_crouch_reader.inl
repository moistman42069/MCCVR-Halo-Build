// H3EK A9A230/retail37C394, entered in mode0 by H3EK A61330/retail3555EC.
// Called only under the native adapter's SEH and salted local-owner guards.
// Alternate camera/up modes are deliberately not treated as world-Z crouch.
bool Halo3PhysicalCrouchReduction(const uint8_t* unit,const uint8_t* definition,
    float& reduction) noexcept
{
    reduction=0.f;
    if(!unit||!definition) return false;
    if(*reinterpret_cast<const int32_t*>(unit+0x10)!=-1||unit[0x96]!=0||
        (*reinterpret_cast<const uint32_t*>(unit+0x110)&4)!=0) return false;
    const int16_t physicsOffset=*reinterpret_cast<const int16_t*>(unit+0x162);
    if(physicsOffset<0||physicsOffset>0x1000) return false;
    const uint32_t physicsMode=*reinterpret_cast<const uint32_t*>(unit+physicsOffset);
    if(physicsMode==7||physicsMode==8) return false;
    float fraction=*reinterpret_cast<const float*>(unit+0x384);
    if(unit[0x4de]==6&&(*reinterpret_cast<const uint32_t*>(unit+0x4e4)&0x10)) fraction=0.f;
    const float scale=*reinterpret_cast<const float*>(unit+0x8c);
    const float standing=*reinterpret_cast<const float*>(definition+0x2e8);
    const float crouched=*reinterpret_cast<const float*>(definition+0x2ec);
    if(!std::isfinite(fraction)||fraction<0.f||fraction>1.f||
        !std::isfinite(scale)||scale<=0.f||
        !std::isfinite(standing)||!std::isfinite(crouched)||crouched<0.f||standing<crouched)
        return false;
    reduction=(standing-crouched)*fraction*scale;
    return std::isfinite(reduction);
}
