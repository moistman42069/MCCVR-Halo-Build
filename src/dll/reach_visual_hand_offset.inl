void ReachApplyVisualHandOffsets(uint16_t tag,const int32_t* boneMap,
    const ReachFpInterpolationContext& context,BoneMatrix* destination)
{
    if(!VisualHandOffsetsActive(g_config)||!destination||!boneMap||!context.valid||!context.layout.Valid())return;
    __try
    {
        uint32_t checksum=0;int count=0;AnatomicalPalmMarkers palms{};
        if(!ReachReadRenderModelIdentity(tag,checksum,count)||count<=0||count>64||
            !ReachAnatomicalPalmMarkers(checksum,count,palms))return;
        int32_t remap[64]{};BoneMatrix staged[64]{};
        if(!SafeReadBytes(boneMap,remap,size_t(count)*4)||!SafeReadBytes(destination,staged,size_t(count)*sizeof(BoneMatrix))||
            remap[palms.leftNode]!=context.layout.leftWristSource||remap[palms.rightNode]!=context.layout.rightWristSource)return;
        uint64_t left=0,right=0;
        for(int node=0;node<count;++node)
        {
            const int source=remap[node];if(source<0||source>=context.liveSourceCount||source>=64)return;
            const auto bit=uint64_t{1}<<source;
            if((context.layout.leftHandSourceDescendants&bit)||
                (staged[node].scale<.001f&&(context.layout.leftControllerOwnedSourceBranch&bit)))left|=uint64_t{1}<<node;
            if((context.layout.rightHandSourceDescendants&bit)||
                (staged[node].scale<.001f&&(context.layout.rightControllerOwnedSourceBranch&bit)))right|=uint64_t{1}<<node;
        }
        // The live world scale the hands themselves are placed with.
        if(ApplyBoneHandVisualOffsets(staged,count,left,right,palms.leftNode,palms.rightNode,g_worldScale.load(),
                g_config.left_handed,context.targets.handAlignment))
            (void)SafeWriteBytes(destination,staged,size_t(count)*sizeof(BoneMatrix));
    }
    __except(EXCEPTION_EXECUTE_HANDLER){}
}
