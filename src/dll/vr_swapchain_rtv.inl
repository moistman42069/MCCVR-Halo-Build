// Production XR image-view creation; shared with RGB reticle pixel tests.
ID3D11RenderTargetView* GetRtv(
    std::vector<ID3D11Texture2D*>& images,
    std::vector<ID3D11RenderTargetView*>& rtvs, uint32_t idx,
    bool rgbReticleUpload = false)
{
    if (!g_device || idx >= images.size() || idx >= rtvs.size())
        return nullptr;
    if (!rtvs[idx])
    {
#ifdef VR_BEFORE_IMAGE_VIEW
        VR_BEFORE_IMAGE_VIEW(images[idx]);
#endif
        // SteamVR exposes TYPELESS OpenXR images. Such a resource has no default view: asking for one is an
        // invalid call, which d3d11 refuses with E_INVALIDARG (and the view was asked for again every frame),
        // and which a program that has hooked the device may not survive. The negotiated XR format is the exact
        // concrete view format the runtime expects, so a typeless image is given it from the start.
        D3D11_RENDER_TARGET_VIEW_DESC desc{};
        desc.Format = static_cast<DXGI_FORMAT>(g_xrFormat);
        desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = 0;
        D3D11_TEXTURE2D_DESC imageDesc{};
        images[idx]->GetDesc(&imageDesc);
        const bool typeless = VrImageFormatIsTypeless(imageDesc.Format);
        HRESULT result = E_FAIL;
        if (typeless)
        {
            result = g_device->CreateRenderTargetView(images[idx], &desc, &rtvs[idx]);
        }
        else
        {
            result = g_device->CreateRenderTargetView(images[idx], nullptr, &rtvs[idx]);
            // Halo 4 and CE: a reticle image whose default view failed all the same takes the XR format.
            if (VrHalo4ReticleRtvNeedsTypedFallback(rgbReticleUpload, FAILED(result), !rtvs[idx]))
                result = g_device->CreateRenderTargetView(images[idx], &desc, &rtvs[idx]);
        }
#ifdef VR_AFTER_IMAGE_VIEW
        VR_AFTER_IMAGE_VIEW(images[idx], result, typeless);
#endif
    }
    return rtvs[idx];
}
