#include "halo4_adapter.h"

#include "halo4_render_logic.h"

namespace
{
    constexpr Halo4EvidenceIdentity kHalo4RetailEvidence = {
        L"halo4.dll",
        kHalo4RetailModuleSha256[0],
        kHalo4RetailModuleSha256[1],
        kHalo4RetailPeTimestamp,
        static_cast<uint32_t>(kHalo4RetailImageSize),
        kHalo4KitBuildTag,
    };
}

Halo4AdapterStage Halo4Adapter_GetStage()
{
    // C-H4-1: shared virtual-controller transport only. This grants no engine
    // hook of any kind - see Halo4Adapter_RuntimeHooksPermitted below, which
    // stays false until C-H4-3 carries a proven camera core.
    return Halo4AdapterStage::ControllerInputOnly;
}

const Halo4EvidenceIdentity& Halo4Adapter_GetEvidenceIdentity()
{
    return kHalo4RetailEvidence;
}

bool Halo4Adapter_HookProofComplete(const Halo4HookProof& proof)
{
    return proof.retailIdentity &&
        proof.loadedImageMatchCount == 1 &&
        proof.executableRange &&
        proof.abi &&
        proof.callers &&
        proof.dataFlow &&
        proof.h4ekSemantics &&
        proof.consumedLayoutFields;
}

bool Halo4Adapter_RuntimeHooksPermitted()
{
    // No Halo 4 signature has been proven and no camera core exists. Camera,
    // render, aim, movement, HUD, IK, haptics, and lifecycle hooks stay
    // forbidden until C-H4-3's all-or-nothing install carries its proofs.
    return false;
}
