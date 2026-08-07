#include "halo4_adapter.h"

#include "../common/halo4_render_logic.h"

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
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    // C-H4-3: everything C-H4-2 established, plus the per-eye camera core.
    return Halo4AdapterStage::ControllerInputAndStereoCamera;
#else
    // C-H4-2: the C-H4-1 virtual-controller transport, plus the Halo 4
    // level-load gate and the one-shot cold observation that verifies the
    // pinned identity and the E-H4-4 anchors against the loaded image. This
    // grants no engine hook of any kind.
    return Halo4AdapterStage::ControllerInputAndColdObservation;
#endif
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
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    // C-H4-3. This permits ONLY the camera core's two hooks, and only after
    // its own all-or-nothing install proof passes: C-H4-2's cold observation
    // must have PASSED for this exact module generation, all four E-H4-6
    // camera anchors must be unique at their pinned RVAs, and the per-window
    // loop's own two call instructions must target the two functions being
    // hooked. Aim, movement, HUD, IK and haptics remain unhooked - they have
    // no Halo 4 evidence and are not published as capabilities.
    return true;
#else
    // No camera core is compiled in. Camera, render, aim, movement, HUD, IK,
    // haptics and lifecycle hooks all stay forbidden.
    return false;
#endif
}
