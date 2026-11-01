// Production blit body shared with the focused pixel fixture.
// Copy src into dst (an XR swapchain image). Uses a plain GPU copy when
// the formats/sizes allow it, otherwise draws a fullscreen quad, fixing
// gamma along the way.
bool Blit(ID3D11Texture2D* src, const D3D11_TEXTURE2D_DESC& srcDesc,
          ID3D11Texture2D* dst, uint32_t dstW, uint32_t dstH,
          ID3D11RenderTargetView* dstRtv, bool authoredReticleUpload = false)
{
    const bool reticleAlphaRepair =
        VrBlitNeedsHalo4ReticleAlphaRepair(
            TitleAdapter_GetActiveTitle() == GameTitle::Halo4,
            dstW, dstH) || halo_ce::ReticleUploadNeedsAlphaRepair(
            TitleAdapter_GetActiveTitle() == GameTitle::HaloCE,
            authoredReticleUpload,srcDesc.Width,srcDesc.Height,dstW,dstH) ||
        VrBlitNeedsReachReticleStraightAlpha(
            TitleAdapter_GetActiveTitle() == GameTitle::HaloReach,
            authoredReticleUpload, srcDesc.Width, srcDesc.Height, dstW, dstH);
    const bool fastPath = !reticleAlphaRepair &&
        VrBlitCanUseDirectCopy(
        srcDesc.Width, srcDesc.Height, srcDesc.Format,
        srcDesc.SampleDesc.Count, dstW, dstH,
        static_cast<DXGI_FORMAT>(g_xrFormat));
    if (!g_context || !VrBlitResourcesReady(
            src != nullptr, dst != nullptr, dstRtv != nullptr, fastPath))
    {
        LOG("blit: missing %s resource (context=%p src=%p dst=%p rtv=%p)",
            fastPath ? "direct-copy" : "shader-path",
            static_cast<void*>(g_context), static_cast<void*>(src),
            static_cast<void*>(dst), static_cast<void*>(dstRtv));
        return false;
    }
    // One-time: confirm the cheap CopyResource path is taken (the slow path
    // makes an intermediate texture + full-screen draw every eye blit).
    // Log every TRANSITION, not just the first blit. Logging once meant the
    // menu's cheap backbuffer blit reported FAST and the log then went silent
    // -- so a switch to the slow path on level load (the in-game scene target
    // is multisampled, the menu backbuffer is not) was invisible. The slow
    // path builds an intermediate and runs a full-screen draw PER EYE PER
    // FRAME at full render size, which is exactly the kind of cost that
    // halves the frame rate the moment a level loads.
    static int loggedPath = -1;
    if (!reticleAlphaRepair &&
        loggedPath != (fastPath ? 1 : 0))
    {
        loggedPath = fastPath ? 1 : 0;
        LOG("PERF: eye blit uses %s path (src %ux%u fmt %d samples %u -> "
            "dst %ux%u xrfmt %d)",
            fastPath?"FAST CopyResource":"SLOW shader",
            srcDesc.Width,srcDesc.Height,(int)srcDesc.Format,
            srcDesc.SampleDesc.Count,
            dstW,dstH,(int)g_xrFormat);
    }
    if (fastPath)
    {
        g_context->CopyResource(dst, src);
        return true;
    }

    if (!EnsureBlitPipeline())
        return false;

    ID3D11ShaderResourceView* srv = AcquireSrcSrv(src, srcDesc);
    if (!srv)
        return false;

    // If the source is already an sRGB view (sampling gives linear) or the
    // destination isn't sRGB, a raw copy through the shader is correct.
    // Otherwise decode gamma in the shader so the sRGB target re-encodes it.
    const bool linearize=!IsSrgb(srcDesc.Format) && IsSrgb((DXGI_FORMAT)g_xrFormat);
    ID3D11PixelShader* ps=linearize?g_blitPsLinearize:g_blitPsPass;

    D3DStateBackup backup;
    backup.Capture(g_context);

    g_context->OMSetRenderTargets(1, &dstRtv, nullptr);
    D3D11_VIEWPORT vp{0, 0, (float)dstW, (float)dstH, 0, 1};
    g_context->RSSetViewports(1, &vp);
    g_context->RSSetState(g_blitRasterizer);
    g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    g_context->OMSetDepthStencilState(g_blitDepthOff, 0);
    g_context->IASetInputLayout(nullptr);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->VSSetShader(g_blitVs, nullptr, 0);
    // Halo may leave a geometry shader bound at the end of an eye pass.
    // The fullscreen triangle has no compatible GS stage; clear it for the
    // blit and let D3DStateBackup restore the game's shader afterward.
    g_context->GSSetShader(nullptr, nullptr, 0);
    g_context->PSSetShader(ps, nullptr, 0);
    g_context->PSSetShaderResources(0, 1, &srv);
    g_context->PSSetSamplers(0, 1, &g_blitSampler);
    g_context->Draw(3, 0);

    backup.Restore(g_context);
    return true;
}
