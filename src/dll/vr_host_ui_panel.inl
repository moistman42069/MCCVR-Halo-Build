// A HOST'S OWN UI OVER THE STEREO PICTURE (reclaimer_host_logic.h, "the host's UI as a panel"). d3d11_hook.cpp
// makes the picture of what the host drew over the frame; here it is drawn into an image of its own and shown as
// one layer fixed in front of the eyes: the UI at the place it has on the frame, the game in stereo behind it.
// Included in vr.cpp after the blit pipeline, the swapchain helpers and MakeQuad.
//
// Only in a frame the host drew over, and only where the stereo picture is shown; nothing in MCC. COST in such a
// frame: one draw of the picture into the panel's image (the frame's size, or half of it past
// reclaimer_host::kPanelMaxWidth). Whatever fails gives the panel up for the session (D3D_HostUiPanelGiveUp),
// and the flat picture carries the host's screens as it did before.
XrSwapchain g_hostUiChain = XR_NULL_HANDLE;
std::vector<ID3D11Texture2D*> g_hostUiImages;
std::vector<ID3D11RenderTargetView*> g_hostUiRtvs;
uint32_t g_hostUiImageW = 0, g_hostUiImageH = 0;
uint32_t g_hostUiImageMisses = 0;
ID3D11PixelShader* g_hostUiPs = nullptr;
ID3D11Buffer* g_hostUiCb = nullptr;

bool EnsureHostUiPanelPipeline()
{
    if (g_hostUiPs && g_hostUiCb)
        return true;
    if (!g_device || !EnsureBlitPipeline())
        return false;
    if (!g_hostUiPs)
    {
        ID3DBlob* blob = nullptr;
        ID3DBlob* errors = nullptr;
        const HRESULT compiled = D3DCompile(kHostUiPanelShader, strlen(kHostUiPanelShader), nullptr, nullptr, nullptr,
                                            "ps_panel", "ps_5_0", 0, 0, &blob, &errors);
        if (errors)
            errors->Release();
        const bool made = SUCCEEDED(compiled) && blob &&
            SUCCEEDED(g_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
                                                  &g_hostUiPs));
        if (blob)
            blob->Release();
        if (!made)
        {
            g_hostUiPs = nullptr;
            return false;
        }
    }
    if (!g_hostUiCb)
    {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 16;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(g_device->CreateBuffer(&desc, nullptr, &g_hostUiCb)))
        {
            g_hostUiCb = nullptr;
            return false;
        }
    }
    return true;
}

// The picture drawn over the whole of the acquired image (kHostUiPanelShader says what the two modes are).
bool DrawHostUiPanel(ID3D11Texture2D* picture, const D3D11_TEXTURE2D_DESC& pictureDesc,
                     ID3D11RenderTargetView* target, uint32_t width, uint32_t height, bool opaque)
{
    if (!g_context || !picture || !target || !EnsureHostUiPanelPipeline())
        return false;
    ID3D11ShaderResourceView* source = AcquireSrcSrv(picture, pictureDesc);
    if (!source)
        return false;
    const bool toLinear = !IsSrgb(pictureDesc.Format) && IsSrgb(static_cast<DXGI_FORMAT>(g_xrFormat));
    const float mode[4]{toLinear ? 1.0f : 0.0f, opaque ? 1.0f : 0.0f, 0.0f, 0.0f};
    g_context->UpdateSubresource(g_hostUiCb, 0, nullptr, mode, 0, 0);
    D3DStateBackup backup;
    backup.Capture(g_context);
    ID3D11Buffer* savedConstants = nullptr;   // D3DStateBackup does not carry the pixel shader's constants
    g_context->PSGetConstantBuffers(0, 1, &savedConstants);
    g_context->OMSetRenderTargets(1, &target, nullptr);
    D3D11_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    g_context->RSSetViewports(1, &viewport);
    g_context->RSSetState(g_blitRasterizer);
    g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    g_context->OMSetDepthStencilState(g_blitDepthOff, 0);
    g_context->IASetInputLayout(nullptr);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->VSSetShader(g_blitVs, nullptr, 0);
    g_context->GSSetShader(nullptr, nullptr, 0);
    g_context->PSSetShader(g_hostUiPs, nullptr, 0);
    g_context->PSSetShaderResources(0, 1, &source);
    g_context->PSSetSamplers(0, 1, &g_blitSampler);
    g_context->PSSetConstantBuffers(0, 1, &g_hostUiCb);
    g_context->Draw(3, 0);
    g_context->PSSetConstantBuffers(0, 1, &savedConstants);
    if (savedConstants)
        savedConstants->Release();
    backup.Restore(g_context);
    return true;
}

// True: `quad` is this frame's panel, ready to be queued. The present's thread, inside the frame's layer list.
bool PrepareHostUiPanel(XrCompositionLayerQuad& quad)
{
    if (!g_hostOwnsRenderSize || !g_config.host_ui_panel || !g_device || !g_context || g_viewSpace == XR_NULL_HANDLE)
        return false;
    D3DHostUiPicture picture{};
    if (!D3D_HostUiPicture(picture) || !picture.texture)
        return false;
    ID3D11Device* pictureDevice = nullptr;
    picture.texture->GetDevice(&pictureDevice);
    const bool sameDevice = pictureDevice == g_device;
    if (pictureDevice)
        pictureDevice->Release();
    if (!sameDevice)
    {
        D3D_HostUiPanelGiveUp("the host's frame is on another device than the headset's images");
        return false;
    }
    D3D11_TEXTURE2D_DESC pictureDesc{};
    picture.texture->GetDesc(&pictureDesc);
    uint32_t width = 0, height = 0;
    reclaimer_host::PanelImageSize(pictureDesc.Width, pictureDesc.Height, width, height);
    if (g_hostUiChain == XR_NULL_HANDLE || g_hostUiImageW != width || g_hostUiImageH != height)
    {
        DestroyChain(g_hostUiChain, g_hostUiImages, g_hostUiRtvs);
        g_hostUiImageW = g_hostUiImageH = 0;
        if (!CreateChain(width, height, g_hostUiChain, g_hostUiImages, g_hostUiRtvs, "host UI panel"))
        {
            D3D_HostUiPanelGiveUp("the headset's runtime made no image for it");
            return false;
        }
        g_hostUiImageW = width;
        g_hostUiImageH = height;
    }
    uint32_t index = 0;
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = 100000000;   // 100 ms in ns: a panel is not worth a stalled frame
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    const XrResult acquired = xrAcquireSwapchainImage(g_hostUiChain, &acquire, &index);
    const XrResult waited = XR_SUCCEEDED(acquired) ? xrWaitSwapchainImage(g_hostUiChain, &wait) : acquired;
    if (waited != XR_SUCCESS)
    {
        // An image that did not come stays acquired, so this set of images is done: a new one next frame, and
        // after three the panel is given up.
        DestroyChain(g_hostUiChain, g_hostUiImages, g_hostUiRtvs);
        g_hostUiImageW = g_hostUiImageH = 0;
        if (++g_hostUiImageMisses >= 3)
            D3D_HostUiPanelGiveUp("its image did not become ready three times");
        return false;
    }
    const bool drawn = index < g_hostUiImages.size() &&
        DrawHostUiPanel(picture.texture, pictureDesc, GetRtv(g_hostUiImages, g_hostUiRtvs, index, true), width,
                        height, picture.opaque);
    const XrResult released = xrReleaseSwapchainImage(g_hostUiChain, &release);
    if (!drawn || XR_FAILED(released))
    {
        D3D_HostUiPanelGiveUp(drawn ? "its image could not be released"
                                    : "the picture could not be drawn into its image");
        return false;
    }
    // Colour already multiplied by alpha: the source-alpha flag alone.
    quad = MakeQuad(g_hostUiChain, static_cast<int32_t>(width), static_cast<int32_t>(height),
                    g_config.host_ui_panel_width_m, g_config.host_ui_panel_distance_m, 0.0f,
                    XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT, true);
    static bool logged = false;
    if (!logged)
    {
        logged = true;
        LOG("host UI panel: shown over the stereo picture: the host's %ux%u drawing as a %ux%u image, %.2f m wide "
            "at %.2f m, fixed in front of the eyes (host_ui_panel = 0 gives the flat picture back)",
            pictureDesc.Width, pictureDesc.Height, width, height, g_config.host_ui_panel_width_m,
            g_config.host_ui_panel_distance_m);
    }
    return true;
}
