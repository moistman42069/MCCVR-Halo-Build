#pragma once

// The blend a host's draw over the frame is repeated with into the panel's picture (reclaimer_host_logic.h, "the
// host's UI as a panel"; d3d11_hook.cpp makes the picture, tests/host_ui_panel_pixels_tests.cpp proves the result).
//
// The picture starts transparent black. Repeated with the host's own COLOUR blend, a draw leaves colour already
// multiplied by its coverage, whichever of the two usual blends the host uses:
//     colour x alpha + what was there x (1 - alpha)        (straight)
//     colour         + what was there x (1 - alpha)        (premultiplied)
// The ALPHA channel is ours: a host drawing onto an opaque frame need not keep one (it may mask alpha writes off,
// or blend it to nothing). Here it is always written and always accumulates coverage,
//     alpha + what was there x (1 - alpha),
// so the picture laid over any scene as premultiplied colour gives what the host's draw gave over the frame.
// A draw with blending off is repeated as it is: it writes its texture's own alpha.

#include <d3d11.h>

namespace reclaimer_host
{
inline bool LayerDrawBlended(const D3D11_BLEND_DESC* hosts)
{
    return hosts && hosts->RenderTarget[0].BlendEnable != FALSE;
}

// `hosts`: the blend state bound at the host's draw; null for the default state (no blending).
inline D3D11_BLEND_DESC LayerBlend(const D3D11_BLEND_DESC* hosts)
{
    D3D11_BLEND_DESC blend{};
    if (hosts)
        blend = *hosts;
    blend.AlphaToCoverageEnable = FALSE;
    for (D3D11_RENDER_TARGET_BLEND_DESC& target : blend.RenderTarget)
    {
        if (!hosts)
        {
            target.BlendEnable = FALSE;
            target.SrcBlend = D3D11_BLEND_ONE;
            target.DestBlend = D3D11_BLEND_ZERO;
            target.BlendOp = D3D11_BLEND_OP_ADD;
        }
        if (target.BlendEnable)
        {
            target.SrcBlendAlpha = D3D11_BLEND_ONE;
            target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        }
        else
        {
            target.SrcBlendAlpha = D3D11_BLEND_ONE;
            target.DestBlendAlpha = D3D11_BLEND_ZERO;
        }
        target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    }
    return blend;
}
} // namespace reclaimer_host
