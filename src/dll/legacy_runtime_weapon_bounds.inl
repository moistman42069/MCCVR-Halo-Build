// Cold proof windows; no new hook or engine call. See runtime-bounds evidence.
struct LegacyBoundsProofWindow { GameTitle title; uint32_t rva; const char* pattern; };
static constexpr LegacyBoundsProofWindow kLegacyBoundsProofWindows[]{
    {GameTitle::Halo3,0x2B5862,"48 8B 05 AF 37 79 00 46 8B 54 C8 04 4C 8B 0D 53 9C D1 01 45 85 D2 74 04 4B 8D 1C 91"},
    {GameTitle::Halo3,0x2B58B7,"44 39 5B 78 75 05 49 8B CB EB 07 8B 43 78 49 8D 0C 81 E8 12 90 FD FF"},
    {GameTitle::Halo3,0x28E8E9,"F3 0F 10 51 04 48 8D 55 D0 F3 0F 10 59 0C 0F 57 C0 F3 0F 10 49 08 41 B8 30 00 00 00 F3 0F 10 61 14 F3 0F 5C CA F3 0F 11 45 EC F3 0F 10 41 10 F3 0F 5C C3 F3 0F 11 55 E0 F3 0F 10 51 24 F3 0F 11 4D D0 F3 0F 10 49 18 F3 0F 5C CC F3 0F 11 5D E4 F3 0F 10 59 1C"},
    {GameTitle::Halo3ODST,0x2DD79D,"4C 8B 05 04 53 D4 01 0F B7 50 04 48 8B 05 F9 18 7C 00 8B 4C D0 04 85 C9 75 04 33 D2 EB 04 49 8D 14 88 83 7A 78 00 75 05 45 33 C9 EB 07 8B 42 78 4D 8D 0C 80"},
    {GameTitle::Halo3ODST,0x2DD7ED,"49 8B C9 E8 7B C3 FD FF"},
    {GameTitle::Halo3ODST,0x2B9B79,"F3 0F 10 51 04 48 8D 55 D0 F3 0F 10 59 0C 0F 57 C0 F3 0F 10 49 08 41 B8 30 00 00 00 F3 0F 10 61 14 F3 0F 5C CA F3 0F 11 45 EC F3 0F 10 41 10 F3 0F 5C C3 F3 0F 11 55 E0 F3 0F 10 51 24 F3 0F 11 4D D0 F3 0F 10 49 18 F3 0F 5C CC F3 0F 11 5D E4 F3 0F 10 59 1C"},
    {GameTitle::HaloReach,0x29A00B,"41 8B 55 6C 4C 8D 35 0A FF B9 04 49 0F BF C7 48 6B C8 17 89 6C 24 30 48 03 CA 89 74 24 28 48 C1 EA 1C 49 8B 04 D6 49 8D 55 64 4C 8D 0C 88"},
    {GameTitle::HaloReach,0x2560A3,"48 8B 47 18 8B D6 8B 0F 8B 58 14 E8 0D E4 DB FF 0F B7 88 42 01 00 00 48 6B D1 0D 48 03 D3 48 C1 EB 1C 49 8B 84 DE 20 9F E3 04 48 8D 0C 90 E8 06 EF 02 00"},
    {GameTitle::HaloReach,0x28501B,"F3 0F 10 53 18 F3 0F 10 4B 10 F3 0F 10 43 08 F3 0F 5C 4B 0C F3 0F 5C 43 04 F3 0F 5C 53 14 C7 40 0C 00 00 80 3F"},
};

bool LegacyProveRuntimeWeaponBounds(GameTitle title, uintptr_t base, size_t size)
{
    int checked=0;
    for (const auto& window:kLegacyBoundsProofWindows)
    {
        if (window.title!=title) continue;
        const uintptr_t found=sig::Find(base,size,window.pattern);
        if (!found || found-base!=window.rva ||
            sig::Find(found+1,base+size-found-1,window.pattern)) return false;
        ++checked;
    }
    return checked==3;
}

// The caller's title/generation and the native tag globals are independently
// proven by that title's camera admission. All dereferences stay fault guarded.
bool LegacyResolveBoundsDescriptor(GameTitle title, uint16_t tag,
    const uint8_t*& descriptor)
{
    descriptor=nullptr;
    if (tag==0xFFFFu) return false;
    __try
    {
        if (title==GameTitle::HaloReach)
            return ReachResolveRenderModelDescriptor(tag,descriptor);
        descriptor=title==GameTitle::Halo3 ? Halo3LoadedTagDefinition(tag) :
            title==GameTitle::Halo3ODST ? OdstLoadedTagDefinition(tag) : nullptr;
        return descriptor!=nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool LegacyReadRuntimeWeaponBounds(GameTitle title, uint16_t tag,
    uint32_t checksum, Halo4WeaponCollisionBounds& output)
{
    auto* feature=LegacyCollisionForTitle(title);
    if (!feature || !feature->runtimeBoundsProven.load(std::memory_order_acquire))
        return false;
    const uint8_t* descriptor=nullptr;
    if (!LegacyResolveBoundsDescriptor(title,tag,descriptor) ||
        reinterpret_cast<uintptr_t>(descriptor)>
            (std::numeric_limits<uintptr_t>::max)()-0x80u) return false;
    // Each kit proves render_model geometry +0x64, compression block +0x10.
    // Each retail renderer matches its packed address at model +0x78.
    struct Header { int32_t count; uint32_t packed; };
    Header header{};
    if (!SafeReadBytes(descriptor+0x74,&header,sizeof(header)) ||
        header.count<=0 || header.count>64 || !header.packed ||
        header.packed==0xFFFFFFFFu) return false;
    uintptr_t biasedBase=0;
    if (title==GameTitle::HaloReach)
    {
        if (!g_reachCamera.base || !SafeReadBytes(reinterpret_cast<const void*>(
                g_reachCamera.base+kReachNodeRecordBlockTableRva+
                static_cast<uintptr_t>(header.packed>>28)*8),
                &biasedBase,sizeof(biasedBase))) return false;
    }
    else
    {
        const void* baseSlot=title==GameTitle::Halo3 ? g_halo3TagDataBase : g_odstTagDataBase;
        if (!baseSlot || !SafeReadBytes(baseSlot,&biasedBase,sizeof(biasedBase))) return false;
    }
    if (!biasedBase) return false;
    const LegacyBoundsLayout layout=title==GameTitle::Halo3 ? LegacyBoundsLayout::Halo3 :
        title==GameTitle::Halo3ODST ? LegacyBoundsLayout::Odst : LegacyBoundsLayout::Reach;
    const size_t stride=LegacyCompressionRecordBytes(layout);
    const uintptr_t address=biasedBase+static_cast<uintptr_t>(header.packed)*4;
    const size_t bytes=static_cast<size_t>(header.count)*stride;
    if (address<biasedBase || address>(std::numeric_limits<uintptr_t>::max)()-bytes)
        return false;
    Halo4WeaponCollisionBounds combined{};
    combined.runtimeImportChecksum=checksum;
    for (int record=0;record<header.count;++record)
    {
        uint8_t raw[52]{};
        Halo4WeaponCollisionBounds bounds{};
        if (!SafeReadBytes(reinterpret_cast<const void*>(address+record*stride),raw,stride) ||
            !LegacyDecodeCompressionBounds(layout,std::span(raw,stride),bounds)) return false;
        for (int axis=0;axis<3;++axis)
        {
            combined.minimum[axis]=record ?
                std::fmin(combined.minimum[axis],bounds.minimum[axis]) : bounds.minimum[axis];
            combined.maximum[axis]=record ?
                std::fmax(combined.maximum[axis],bounds.maximum[axis]) : bounds.maximum[axis];
            if (!std::isfinite(combined.maximum[axis]-combined.minimum[axis])) return false;
        }
    }
    Header current{};
    uint32_t currentChecksum=0;
    const uint8_t* currentDescriptor=nullptr;
    if (!LegacyResolveBoundsDescriptor(title,tag,currentDescriptor) || currentDescriptor!=descriptor ||
        !SafeReadBytes(descriptor+0x08,&currentChecksum,sizeof(currentChecksum)) ||
        currentChecksum!=checksum || !SafeReadBytes(descriptor+0x74,&current,sizeof(current)) ||
        current.count!=header.count || current.packed!=header.packed) return false;
    output=combined;
    return true;
}

bool LegacyClassifyRuntimeWeapon(GameTitle title, uint16_t tag,
    uint32_t generation, const BoneMatrix* source, const int32_t* boneMap)
{
    if (!source || !boneMap) return false;
    int count=0, cameraControl=-1, right=-1, left=-1, bodyEnd=-1, slot=0;
    const ReachFpBodyLayout* reachLayout=nullptr;
    if (title==GameTitle::HaloReach)
    {
        for (const auto& context:g_reachFpInterpolations)
            if (context.source==source && context.generation==generation && context.layout.Valid())
            {
                count=context.liveSourceCount;
                bodyEnd=static_cast<int>(context.layout.paletteBodyNodeCount);
                cameraControl=context.layout.cameraControlSource;
                reachLayout=&context.layout;
                break;
            }
    }
    else if (title==GameTitle::Halo3 || title==GameTitle::Halo3ODST)
    {
        for (const auto& context:g_fpInterpolationContexts)
            if (context.source==source && context.generation==generation &&
                context.slot>=0 && context.slot<=1)
            {
                count=context.count; cameraControl=context.cameraControl;
                right=context.wrist; left=context.lWrist; slot=context.slot;
                break;
            }
    }
    const int capacity=title==GameTitle::HaloReach ? int(kReachFpMaxSourceNodeCount):64;
    if (count<=0 || count>capacity || cameraControl<0 || cameraControl>=count) return false;
    int nodes=0;
    if (title==GameTitle::HaloReach)
    { if (!ReachResolvePaletteNodeCount(tag,nodes)) return false; }
    else nodes=LegacyAnatomicalRenderNodeCount(title,tag);
    if (nodes<=0 || nodes>capacity) return false;
    int32_t remap[kReachFpMaxSourceNodeCount]{};
    if (!SafeReadBytes(boneMap,remap,size_t(nodes)*sizeof(*remap))) return false;
    if (reachLayout)
    {
        // Use this observed model's outputs, never all non-body graph nodes.
        FpHeldSources held{};
        return ResolveReachFpHeldSources(*reachLayout,
            std::span<const int32_t>{remap,size_t(nodes)},held);
    }
    if (title!=GameTitle::HaloReach)
    {
        struct BodyObservation
        {
            const BoneMatrix* source=nullptr;
            uint32_t generation=0;
            uint64_t atMs=0;
            int count=0, camera=-1, right=-1, left=-1, end=-1;
        };
        static thread_local BodyObservation bodies[2][2];
        auto& body=bodies[title==GameTitle::Halo3 ? 0:1][slot];
        const uint64_t now=GetTickCount64();
        const int prefix=LegacyBodyPrefixFromRemap(std::span(remap,size_t(nodes)),
            count,cameraControl,right,left);
        if (prefix>0)
        {
            body={source,generation,now,count,cameraControl,right,left,prefix};
            return false;
        }
        if (body.source!=source || body.generation!=generation || body.count!=count ||
            body.camera!=cameraControl || body.right!=right || body.left!=left ||
            !body.atMs || now<body.atMs || now-body.atMs>150) return false;
        bodyEnd=body.end;
    }
    return LegacyRemapIsAppendedWeapon(std::span(remap,size_t(nodes)),
        bodyEnd,cameraControl,count);
}
