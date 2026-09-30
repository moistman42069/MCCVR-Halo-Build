bool ApplyBoneHandVisualOffsets(BoneMatrix* palette,int count,uint64_t leftMask,uint64_t rightMask,
    int leftWrist,int rightWrist,float units,bool leftHanded,bool anatomical)
{
    if(!VisualHandOffsetsActive(g_config))return true;
    if(!palette||count<=0||count>64||leftWrist<0||rightWrist<0||leftWrist>=count||rightWrist>=count)return false;
    float leftBasis[9]{},rightBasis[9]{},left[3]{},right[3]{};
    if(!NormalizedBasis(palette[leftWrist],leftBasis)||!NormalizedBasis(palette[rightWrist],rightBasis)||
        !visual_hand::Deltas(leftBasis,rightBasis,units,VisualLeftHandOffset(g_config),VisualRightHandOffset(g_config),
            leftHanded,anatomical,left,right))return false;
    return visual_hand::Apply(size_t(count),leftMask,rightMask,left,right,
        [&](size_t node){return palette[node].translation;},
        [&](size_t node,const float* value){std::memcpy(palette[node].translation,value,12);});
}
void LegacyApplyVisualHandOffsets(GameTitle title,uint16_t tag,const int32_t* boneMap,
    const FpInterpolationContext& context,BoneMatrix* destination)
{
    if(!VisualHandOffsetsActive(g_config)||!destination||!context.valid||!boneMap||context.count<=0||context.count>64)return;
    __try
    {
        uint32_t checksum=0;const int count=LegacyAnatomicalRenderNodeCount(title,tag,&checksum);
        AnatomicalPalmMarkers palms{};
        if(count<=0||count>64||!(title==GameTitle::Halo3?Halo3AnatomicalPalmMarkers(checksum,count,palms):
            OdstAnatomicalPalmMarkers(checksum,count,palms)))return;
        int32_t remap[64]{};BoneMatrix staged[64]{};
        if(!SafeReadBytes(boneMap,remap,size_t(count)*4)||!SafeReadBytes(destination,staged,size_t(count)*sizeof(BoneMatrix))||
            remap[palms.leftNode]!=context.lWrist||remap[palms.rightNode]!=context.wrist)return;
        uint64_t left=0,right=0;
        for(int node=0;node<count;++node)
        {
            const int source=remap[node];if(source<0||source>=context.count)return;
            const auto bit=uint64_t{1}<<source;
            if(context.lWristDescendants&bit)left|=uint64_t{1}<<node;
            if(context.wristDescendants&bit)right|=uint64_t{1}<<node;
            // Preserve the already-hidden weighted arm anchors at their hand.
            if(g_config.floating_hands&&staged[node].scale<.001f)
            {
                if(source==context.lElbow||source==context.lShoulder)left|=uint64_t{1}<<node;
                if(source==context.elbow||source==context.shoulder)right|=uint64_t{1}<<node;
            }
        }
        const auto& tracking=g_fpStereoSolveScope.anatomicalTracking;
        const float units=title==GameTitle::Halo3?g_worldScale.load():kOdstWorldUnitsPerMeter;
        if(ApplyBoneHandVisualOffsets(staged,count,left,right,palms.leftNode,palms.rightNode,units,
                tracking.leftHanded,tracking.handAlignment))
            (void)SafeWriteBytes(destination,staged,size_t(count)*sizeof(BoneMatrix));
    }
    __except(EXCEPTION_EXECUTE_HANDLER){}
}

void Halo4ApplyVisualHandOffsets(BoneMatrix* solved,float units,bool leftHanded,bool anatomical)
{
    if(!solved)return;
        if(VisualHandOffsetsActive(g_config))
        {
            float leftBasis[9]{},rightBasis[9]{},left[3]{},right[3]{};
            if(NormalizedBasis(solved[kHalo4LeftHandNode],leftBasis)&&NormalizedBasis(solved[kHalo4RightHandNode],rightBasis)&&
                visual_hand::Deltas(leftBasis,rightBasis,units,VisualLeftHandOffset(g_config),VisualRightHandOffset(g_config),
                    leftHanded,anatomical,left,right))
            {
                (void)visual_hand::ApplyClassified(kHalo4StormFpBodyNodeCount,left,right,[](size_t node){
                    const auto role=Halo4ClassifyFloatingNode(int(node));
                    const int index=static_cast<int>(node);
                    if(!g_config.floating_hands&&Halo4StormNodeInSet(kHalo4LeftShoulderSubtree,index))return 1;
                    if(!g_config.floating_hands&&Halo4StormNodeInSet(kHalo4RightShoulderSubtree,index))return 2;
                    return role==Halo4FloatingNodeRole::LeftHand||role==Halo4FloatingNodeRole::CollapseAtLeftWrist?1:
                        role==Halo4FloatingNodeRole::RightHand||role==Halo4FloatingNodeRole::CollapseAtRightWrist?2:0;},
                    [&](size_t node){return solved[node].translation;},
                    [&](size_t node,const float* value){std::memcpy(solved[node].translation,value,12);});
            }
        }
}
