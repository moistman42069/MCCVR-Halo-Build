// H3EK A9A230/retail37C394, entered in mode0 by H3EK A61330/retail3555EC.
// Called only under the native adapter's SEH and salted local-owner guards.
// Alternate camera/up modes are deliberately not treated as world-Z crouch.
// `why` (optional, diagnostics): the guard that refused, as one bit.
//
// +0x160/+0x162 is one of the object's {int16 size, int16 offset} block references: the block holding
// the physics mode lives INSIDE the object, after its fixed fields, and retail reads it unchecked. On a
// live Forge biped it is {240, 0x2310} in a 0x2540-byte object, so the block is judged by its own
// reference, never by a guessed ceiling: a 0x1000 cap here refuses every real biped and keeps this whole
// correction dark in Halo 3.
bool Halo3PhysicalCrouchReduction(const uint8_t* unit,const uint8_t* definition,
    float& reduction,uint32_t* why=nullptr) noexcept
{
    reduction=0.f;
    const auto refuse=[why](uint32_t bit){if(why)*why=bit;return false;};
    if(!unit) return refuse(0x01);
    if(!definition) return refuse(0x02);
    if(*reinterpret_cast<const int32_t*>(unit+0x10)!=-1) return refuse(0x04);
    if(unit[0x96]!=0) return refuse(0x08);
    if((*reinterpret_cast<const uint32_t*>(unit+0x110)&4)!=0) return refuse(0x10);
    const int16_t physicsSize=*reinterpret_cast<const int16_t*>(unit+0x160);
    const int16_t physicsOffset=*reinterpret_cast<const int16_t*>(unit+0x162);
    // Absent (-1), empty, or overlapping the fixed unit fields this reader itself reads (up to +0x4e8).
    if(physicsSize<4||physicsOffset<0x4e8) return refuse(0x20);
    const uint32_t physicsMode=*reinterpret_cast<const uint32_t*>(unit+physicsOffset);
    if(physicsMode==7||physicsMode==8) return refuse(0x40);
    float fraction=*reinterpret_cast<const float*>(unit+0x384);
    if(unit[0x4de]==6&&(*reinterpret_cast<const uint32_t*>(unit+0x4e4)&0x10)) fraction=0.f;
    const float scale=*reinterpret_cast<const float*>(unit+0x8c);
    const float standing=*reinterpret_cast<const float*>(definition+0x2e8);
    const float crouched=*reinterpret_cast<const float*>(definition+0x2ec);
    if(!std::isfinite(fraction)||fraction<0.f||fraction>1.f) return refuse(0x80);
    if(!std::isfinite(scale)||scale<=0.f) return refuse(0x100);
    if(!std::isfinite(standing)||!std::isfinite(crouched)||crouched<0.f||standing<crouched)
        return refuse(0x200);
    reduction=(standing-crouched)*fraction*scale;
    return std::isfinite(reduction);
}
