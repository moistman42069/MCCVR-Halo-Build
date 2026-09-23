// Optional DLSS bank. The existing color-cache borrow pins both bank types;
// native callbacks only copy from proven live resources and never own COM refs.
void EyeCache::ReleaseDepthResources() noexcept
{
    for(int eye=0;eye<2;++eye) {
        if(depthViews_[eye]) depthViews_[eye]->Release();
        if(completedDepthViews_[eye]) completedDepthViews_[eye]->Release();
        if(depth_[eye]) depth_[eye]->Release();
        if(completedDepth_[eye]) completedDepth_[eye]->Release();
        depthViews_[eye]=completedDepthViews_[eye]=nullptr;
        depth_[eye]=completedDepth_[eye]=nullptr;
        depthCameras_[eye]=completedDepthCameras_[eye]={};
    }
    depthSource_={};depthDescriptor_={};depthMask_=0;completedDepthValid_=false;
    depthHistoryEpoch_=completedDepthHistoryEpoch_=0;
}
bool EyeCache::PrepareDepth(ID3D11Device* device,ID3D11DeviceContext* context,
    const D3D11_TEXTURE2D_DESC& source,uint32_t generation) noexcept
{
    if(!Enter()) return false;
    uint32_t copyFormat=0,viewFormat=0;
    bool valid=device&&context==context_&&generation&&generation==generation_&&
        source.Width&&source.Width<=16384&&source.Height&&source.Height<=16384&&
        source.MipLevels==1&&source.ArraySize==1&&source.SampleDesc.Count==1&&
        !source.SampleDesc.Quality&&source.Usage==D3D11_USAGE_DEFAULT&&
        !source.CPUAccessFlags&&!source.MiscFlags&&(source.BindFlags&D3D11_BIND_DEPTH_STENCIL)&&
        dlss::DepthCopyFormats(source.Format,copyFormat,viewFormat);
    if(valid&&depth_[0]&&depth_[1]&&SameSource(source,depthSource_)) {Leave();return true;}
    ID3D11Device* contextDevice=nullptr;
    if(valid) {
        context->GetDevice(&contextDevice);
        valid=contextDevice==device;
        if(contextDevice) contextDevice->Release();
    }
    ID3D11Texture2D* textures[4]{};
    ID3D11ShaderResourceView* views[4]{};
    D3D11_TEXTURE2D_DESC descriptor=source;
    descriptor.Format=static_cast<DXGI_FORMAT>(copyFormat);
    descriptor.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format=static_cast<DXGI_FORMAT>(viewFormat);view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
    view.Texture2D.MipLevels=1;
    for(int i=0;i<4&&valid;++i)
        valid=SUCCEEDED(device->CreateTexture2D(&descriptor,nullptr,&textures[i]))&&
            SUCCEEDED(device->CreateShaderResourceView(textures[i],&view,&views[i]));
    ReleaseDepthResources();
    if(valid) {
        depthSource_=source;depthDescriptor_=descriptor;
        for(int eye=0;eye<2;++eye) {
            depth_[eye]=textures[eye];depthViews_[eye]=views[eye];
            completedDepth_[eye]=textures[eye+2];completedDepthViews_[eye]=views[eye+2];
        }
    } else for(int i=0;i<4;++i) {
        if(views[i]) views[i]->Release();
        if(textures[i]) textures[i]->Release();
    }
    Leave();return valid;
}
bool EyeCache::CaptureDepth(Key key,int eye,ID3D11DeviceContext* context,
    ID3D11Resource* source,const D3D11_TEXTURE2D_DESC& descriptor,
    const dlss::CameraSample& camera,uint64_t historyEpoch) noexcept
{
    if(!Enter()) return false;
    const bool valid=eye>=0&&eye<2&&key.serial&&key==key_&&!complete_&&
        context&&context==context_&&source&&camera.valid&&historyEpoch&&depth_[0]&&depth_[1]&&
        (eye==0||historyEpoch==depthHistoryEpoch_)&&
        depthMask_==(eye==0?0u:1u)&&SameSource(descriptor,depthSource_)&&
        source!=depth_[0]&&source!=depth_[1]&&source!=completedDepth_[0]&&source!=completedDepth_[1];
    if(valid) {
        // D3D requires a whole-subresource copy for native depth sources.
        context->CopySubresourceRegion(depth_[eye],0,0,0,0,source,0,nullptr);
        depthCameras_[eye]=camera;depthMask_|=1u<<eye;depthHistoryEpoch_=historyEpoch;
    } else depthMask_=0;
    Leave();return valid;
}
