// Production read-only native action parser. Included inside the caller namespace.
// The resolver worker and offline fixture share this exact implementation.
struct GestureBindingDescriptor
{
    GameTitle title;
    unsigned action, actionCount, stride, remap, controllerOffset;
    uintptr_t readerRva, stateLoadRva;
    const char* readerPattern;
    const char* statePattern;
};
constexpr GestureBindingDescriptor kGestureBindings[] = {
    {GameTitle::HaloCE,4,0x21,0x8E2,0x12A,0,
     0xADE503,0xADC5A4,
     "49 6B C7 21 44 8B EE 48 89 44 24 40 4D 63 FD 49 03 C7 45 0F B7 A4 46 2A 01 00 00 83 C8 FF 66 44 3B E0 0F 84 1B 03 00 00",
     "4C 8B CA 48 8D 15 ?? ?? ?? ?? 83 F9 04 7D 0D 48 63 C1 4C 69 C0 A0 08 00 00 49 03 D0 41 B8 A0 08 00 00 49 8B C9 E9 ?? ?? ?? ??"},
    {GameTitle::Halo2,5,0x3C,0x17C4,0x1C,0,
     0x6D9A00,0x6D9B16,
     "48 89 5C 24 08 48 89 7C 24 10 4D 85 C9 48 C7 01 03 00 00 00 4D 8B D9 C7 41 08 00 00 00 00 48 8D 05 ?? ?? ?? ?? 4C 8B D1 4C 0F 44 D8 83 FA 3B 0F 87 A5 00 00 00 48 63 C2 48 6B F8 64 41 83 F8 FF 75 69",
     "48 69 D0 C4 17 00 00 48 8D 05 ?? ?? ?? ?? 41 B8 C4 17 00 00"},
    {GameTitle::Halo3,5,0x43,0x518,0xC0,0x514,
     0x187028,0xF751A,
     "48 63 81 14 05 00 00 4C 8B C1 4C 63 CA 83 F8 03 77 0C 48 8D 0D ?? ?? ?? ?? 8B 0C 81 EB 06 8B 0D ?? ?? ?? ?? 83 F9 01 75 10 4A 8D 04 4D 73 00 00 00 49 03 C1 49 8D 04 80 C3 4B 63 84 88 C0 00 00 00 48 8D 0C 40 45 88 4C 88 08 4B 63 84 88 C0 00 00 00 48 8D 0C 40 49 8D 04 88 C3",
     "48 69 D8 18 05 00 00 48 8D 05 ?? ?? ?? ?? 48 03 D8"},
    {GameTitle::Halo3ODST,5,0x47,0x558,0xC0,0x554,
     0x1B8420,0xBFC81,
     "48 63 81 54 05 00 00 4C 8B C1 4C 63 CA 83 F8 03 77 0C 48 8D 0D ?? ?? ?? ?? 8B 0C 81 EB 06 8B 0D ?? ?? ?? ?? 83 F9 01 75 10 4A 8D 04 4D 77 00 00 00 49 03 C1 49 8D 04 80 C3 4B 63 84 88 C0 00 00 00 48 8D 0C 40 45 88 4C 88 08 4B 63 84 88 C0 00 00 00 48 8D 0C 40 49 8D 04 88 C3",
     "49 69 DE 58 05 00 00 48 8D 05 ?? ?? ?? ?? 48 03 D8"},
    {GameTitle::HaloReach,4,0x49,0x700,0x100,0x6FC,
     0xD1070,0x62C2D,
     "48 63 81 FC 06 00 00 4C 8B C1 83 F8 03 77 0D 48 8D 0D ?? ?? ?? ?? 44 8B 0C 81 EB 07 44 8B 0D ?? ?? ?? ?? 48 63 CA 41 83 F9 01 75 0F 48 C1 E1 04 49 8D 80 24 02 00 00 48 03 C1 C3 49 63 84 88 00 01 00 00 48 03 C0 41 89 54 C0 08 49 63 84 88 00 01 00 00 48 C1 E0 04 49 03 C0 C3",
     "48 69 D8 00 07 00 00 48 8D 05 ?? ?? ?? ?? 48 03 D8"},
    {GameTitle::Halo4,3,0x55,0x7E8,0x100,0x7E4,
     0x13F5CC,0xA2818,
     "48 63 81 E4 07 00 00 4C 8B C1 83 F8 03 77 0D 48 8D 0D ?? ?? ?? ?? 44 8B 0C 81 EB 07 44 8B 0D ?? ?? ?? ?? 48 63 CA 41 83 F9 01 75 0F 48 C1 E1 04 49 8D 80 54 02 00 00 48 03 C1 C3 49 63 84 88 00 01 00 00 48 03 C0 41 89 54 C0 08 49 63 84 88 00 01 00 00 48 C1 E0 04 49 03 C0 C3",
     "4C 69 EB E8 07 00 00 48 8D 05 ?? ?? ?? ?? 4C 03 E8"},
};

// Transport masks independently confirmed in each kit's XInput converter.
constexpr uint16_t kGesturePadMasks[14] = {
    1,2,4,8,0x10,0x20,0x40,0x80,0x1000,0x2000,0x4000,0x8000,0x100,0x200};
constexpr uint16_t kGestureHalo2PadMasks[14] = {
    1,2,4,8,0x10,0x20,0x40,0x80,0x100,0x200,0x1000,0x2000,0x4000,0x8000};
constexpr uint32_t kGestureLeftTrigger = 1u << 16;
constexpr uint32_t kGestureRightTrigger = 1u << 17;

uint32_t GestureTransport(GameTitle title,unsigned button) noexcept
{
    if(button==0) return kGestureLeftTrigger;
    if(button==1) return kGestureRightTrigger;
    if(button>=16) return 0;
    return title==GameTitle::Halo2 ? kGestureHalo2PadMasks[button-2] : kGesturePadMasks[button-2];
}

bool GestureReadNativeBindings(const GestureBindingDescriptor& d,uintptr_t state,
    uint32_t (&transport)[4], unsigned actionId) noexcept
{
    if(actionId>=d.actionCount) return true;
    __try
    {
        for(unsigned controller=0;controller<4;++controller)
        {
            const uintptr_t record=state+controller*d.stride;
            if(d.title==GameTitle::HaloCE)
            {
                // CE kit prefs+12A: one signed 16-bit gamepad index per
                // action, controller rows advance 8A0+42 (8E2 total).
                const int button=*reinterpret_cast<const int16_t*>(record+d.remap+actionId*2);
                if(button>=0&&button<16) transport[controller]=GestureTransport(d.title,static_cast<unsigned>(button));
            }
            else if(d.title==GameTitle::Halo2)
            {
                // Native binding records: count then up to eight {type,index,hold}.
                // Type 2 is gamepad. Axis and held bindings need a separate
                // proven transport; declining them must not invent a button.
                const uintptr_t action=record+actionId*100;
                const int count=*reinterpret_cast<const int*>(action+0x1C);
                if(count<1 || count>8) continue;
                for(int i=0;i<count;++i)
                {
                    const int* binding=reinterpret_cast<const int*>(action+0x20+i*12);
                    if(binding[0]!=2) continue;
                    if(binding[2]==0) transport[controller]=GestureTransport(d.title,static_cast<unsigned>(binding[1]));
                    break;
                }
            }
            else
            {
                if(*reinterpret_cast<const unsigned*>(record+d.controllerOffset)!=controller) continue;
                const unsigned button=*reinterpret_cast<const unsigned*>(record+d.remap+actionId*4);
                transport[controller]=GestureTransport(d.title,button);
            }
        }
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

