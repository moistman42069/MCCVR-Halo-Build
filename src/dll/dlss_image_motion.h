#pragma once
#include <d3d12.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

// Dense image-motion estimate on the SAME command list as DLSS. Three
// filtered pyramid levels seed a bounded coarse-to-fine patch search. Camera
// vectors remain the prior; the final full-resolution test must improve on
// them. This is image motion, not a claim of engine/skinning motion vectors.
// No optical-flow driver, BGRA conversion, extra queue or fence submission.
class DlssImageMotion
{
    template<class T> static void Release(T*& p) { if (p) p->Release(); p = nullptr; }
    static constexpr unsigned kLevels = 3, kSets = 7, kDescriptors = 5;
    ID3D12Device* device_ = nullptr; // owner outlives this object
    ID3D12RootSignature* root_ = nullptr;
    ID3D12PipelineState* downsample_ = nullptr;
    ID3D12PipelineState* search_ = nullptr;
    ID3D12PipelineState* merge_ = nullptr;
    ID3D12PipelineState* local_ = nullptr;
    ID3D12PipelineState* fine_ = nullptr;
    ID3D12DescriptorHeap* heap_ = nullptr;
    unsigned descriptorSize_ = 0;
    struct Eye
    {
        ID3D12Resource* pyramid[2][kLevels]{};
        ID3D12Resource* vectors[kLevels]{};
        ID3D12Resource* history = nullptr;
        unsigned width = 0, height = 0, cursor = 0;
        bool valid = false;
    } eyes_[2];
    struct Params
    {
        unsigned width, height, fullWidth, fullHeight;
        float scale, jitterX, jitterY, historyValid;
        unsigned coarse, pad[3];
    };
    D3D12_CPU_DESCRIPTOR_HANDLE Cpu(unsigned index)
    { auto h = heap_->GetCPUDescriptorHandleForHeapStart(); h.ptr += SIZE_T(index) * descriptorSize_; return h; }
    D3D12_GPU_DESCRIPTOR_HANDLE Gpu(unsigned index)
    { auto h = heap_->GetGPUDescriptorHandleForHeapStart(); h.ptr += UINT64(index) * descriptorSize_; return h; }
    static void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
        list->ResourceBarrier(1, &b);
    }
    bool Texture(unsigned width, unsigned height, DXGI_FORMAT format, ID3D12Resource** out)
    {
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = width; d.Height = height; d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
        d.Format = format; d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return SUCCEEDED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(out)));
    }
    void Descriptors(unsigned base, ID3D12Resource* a, ID3D12Resource* b,
        ID3D12Resource* camera, ID3D12Resource* prior, ID3D12Resource* output)
    {
        device_->CreateShaderResourceView(a, nullptr, Cpu(base));
        device_->CreateShaderResourceView(b, nullptr, Cpu(base + 1));
        device_->CreateShaderResourceView(camera, nullptr, Cpu(base + 2));
        device_->CreateShaderResourceView(prior, nullptr, Cpu(base + 3));
        device_->CreateUnorderedAccessView(output, nullptr, nullptr, Cpu(base + 4));
    }
public:
#ifdef HALOMCCVR_DLSS_PIPELINE_PROBE
    ID3D12Resource* DebugVectors(int eye, int level) { return eyes_[eye].vectors[level]; }
#endif
    bool Init(ID3D12Device* device, char* why, size_t bytes)
    {
        if (root_ && downsample_ && search_ && merge_ && local_ && fine_ && heap_) return true;
        device_ = device;
        static constexpr const char* shader = R"(
Texture2D<float4> curTex : register(t0);
Texture2D<float4> prevTex : register(t1);
Texture2D<float2> camTex : register(t2);
Texture2D<float2> priorTex : register(t3);
#ifdef DOWNSAMPLE
RWTexture2D<float4> outputTex : register(u0);
#else
RWTexture2D<float2> outputTex : register(u0);
#endif
SamplerState smp : register(s0);
cbuffer Params : register(b0)
{
    uint width, height, fullWidth, fullHeight;
    float scale, jitterX, jitterY, historyValid;
    uint coarse; uint3 padding;
};
float Error(float2 uv, float2 mv, float2 step, float limit)
{
    float2 priorUv = uv + mv * step;
    if (any(priorUv < step) || any(priorUv > 1.0 - step)) return 1000.0;
    float result = 0;
    // Centre first for early rejection; the remaining 5x5 patch gives
    // enough spatial evidence when jitter changes high-frequency texture.
    [unroll] for (int i = 0; i < 25; ++i)
    {
        int k=i-1; if(k>=12) ++k;
        int2 p=i==0 ? int2(0,0) : int2(k%5-2,k/5-2);
        float2 offset = p * step;
        float3 delta = curTex.SampleLevel(smp, uv + offset, 0).rgb -
            prevTex.SampleLevel(smp, priorUv + offset, 0).rgb;
        result += dot(delta, delta);
        if (result > limit) return result;
    }
    return result;
}
float Error(float2 uv, float2 mv, float2 step) { return Error(uv,mv,step,1000.0); }
float CameraError(float2 uv,float2 mv,float2 step,out float variance)
{
    float3 sum=0; float squares=0,error=0;
    [unroll] for(int y=-2;y<=2;++y)
    [unroll] for(int x=-2;x<=2;++x)
    {
        float2 p=uv+float2(x,y)*step;
        float3 c=curTex.SampleLevel(smp,p,0).rgb;
        float3 delta=c-prevTex.SampleLevel(smp,p+mv*step,0).rgb;
        sum+=c; squares+=dot(c,c); error+=dot(delta,delta);
    }
    variance=max(squares-dot(sum,sum)/25.0,0.00001);
    return error;
}
float2 Median3(float2 a, float2 b, float2 c) { return max(min(a,b),min(max(a,b),c)); }
float2 Prior(float2 uv, out float support)
{
    uint w,h; priorTex.GetDimensions(w,h);
    int2 p=int2(uv*float2(w,h)), last=int2(w,h)-1;
    float2 rows[3], values[9];
    [unroll] for(int y=-1;y<=1;++y)
    {
        [unroll] for(int x=-1;x<=1;++x)
            values[(y+1)*3+x+1]=priorTex.Load(int3(clamp(p+int2(x,y),int2(0,0),last),0));
        rows[y+1]=Median3(values[(y+1)*3],values[(y+1)*3+1],values[(y+1)*3+2]);
    }
    float2 result=Median3(rows[0],rows[1],rows[2]);
    support=0;
    [unroll] for(int i=0;i<9;++i) support += all(abs(values[i]-result)<0.75) ? 1.0 : 0.0;
    return result;
}
float2 Prior(float2 uv) { float unused; return Prior(uv,unused); }
[numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(width, height);
#ifdef DOWNSAMPLE
    // Exact 2x box, including odd-size edge clamping. Repeated filtering
    // avoids the aliasing of a sparse full-resolution coarse search.
    uint sw, sh; curTex.GetDimensions(sw, sh);
    int2 p = int2(id.xy) * 2, last = int2(sw, sh) - 1;
    outputTex[id.xy] = 0.25 * (curTex.Load(int3(min(p,last),0)) +
        curTex.Load(int3(min(p+int2(1,0),last),0)) +
        curTex.Load(int3(min(p+int2(0,1),last),0)) +
        curTex.Load(int3(min(p+1,last),0)));
#else
    float2 camera = camTex.SampleLevel(smp, uv, 0);
    if (!all(isfinite(camera))) { outputTex[id.xy] = float2(0,0); return; }
    float2 jitter = float2(jitterX, jitterY);
    if (historyValid == 0) { outputTex[id.xy] = camera; return; }
    float2 step = 1.0 / float2(width, height);
#if defined(LOCAL_SEARCH) || defined(FINE_SEARCH)
    step = 1.0 / float2(fullWidth, fullHeight);
#endif
    float2 rawCamera = (camera - jitter) / scale;
#if defined(MERGE) || defined(FINE_SEARCH)
    float support;
    float2 inherited = Prior(uv,support);
    float2 seed = (inherited - jitter) / scale;
    // Refinement needs no colour reads when the spatial estimate already
    // agrees with the camera. Object discovery happens on the eighth-size grid.
    if (dot(seed-rawCamera,seed-rawCamera) <= 0.25)
    { outputTex[id.xy]=camera; return; }
#endif
    float variance;
    float cameraError = CameraError(uv, rawCamera, step, variance);
    float2 best = rawCamera;
    float bestError = cameraError;
#ifdef MERGE
    // Consistent neighboring vectors describe the moving surface even
    // when one pixel's colour happens to resemble its old camera location.
    // Still reject a field that contradicts the full-resolution patch.
    if (support >= 7 && dot(seed-rawCamera,seed-rawCamera)>0.25 &&
        Error(uv,seed,step) <= cameraError*1.25 + 0.0005)
    { outputTex[id.xy]=inherited; return; }
    // Full-resolution confirmation rejects a coarse estimate that merely
    // matched a background or crossed an occlusion boundary.
    if (dot(seed - rawCamera, seed - rawCamera) > 0.25 && cameraError > 0.0002)
    {
        [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            float2 candidate = seed + float2(x, y);
            float e = Error(uv, candidate, step, bestError);
            if (e < bestError) { bestError = e; best = candidate; }
        }
    }
    outputTex[id.xy] = (bestError + 27.0 * (2.0/255.0)*(2.0/255.0) < cameraError * 0.75)
        ? best + jitter : camera;
#else
    // Normalise the mismatch by local texture contrast. Jitter changes
    // filtered detail but remains correlated with the camera prediction;
    // independently moving texture does not. Only search the latter.
    if(cameraError < variance*1.25)
    { outputTex[id.xy]=camera; return; }
#ifndef FINE_SEARCH
    float2 seed = coarse != 0 ? rawCamera :
        (Prior(uv) - jitter) / scale;
#endif
    int radius = coarse != 0 ? 4 : 1;
    // Flat matching patches retain the exact camera prior. Search bounds
    // are fixed: at 1/8, +/-32 full-resolution pixels, refined below.
    if (cameraError > 0.00005)
    {
        int origins=coarse != 0 ? 1 : 2;
#ifdef LOCAL_SEARCH
        // One camera-centred +/-16 search covers the common case. Add a
        // distant pyramid hypothesis only when it extends those bounds.
        origins=dot(seed-rawCamera,seed-rawCamera)>256.0 ? 2 : 1;
#endif
#ifdef FINE_SEARCH
        origins=1; // camera already has an exact score
#endif
        for (int origin = 0; origin < origins; ++origin)
        {
        float2 center = origin == 0 ? seed : rawCamera;
#ifdef LOCAL_SEARCH
        center=origin==0 ? rawCamera : seed;
        radius = 4;
#endif
        for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
        {
            float2 candidate = center + float2(x, y)
#ifdef LOCAL_SEARCH
                *4.0
#endif
                ;
            float e = Error(uv, candidate, step, bestError);
            if (e < bestError) { bestError = e; best = candidate; }
        }
        }
    }
#ifdef LOCAL_SEARCH
    float2 refine=best;
    [unroll] for(int y=-2;y<=2;++y)
    [unroll] for(int x=-2;x<=2;++x)
    {
        float2 candidate=refine+float2(x,y);
        float e=Error(uv,candidate,step,bestError);
        if(e<bestError) { bestError=e; best=candidate; }
    }
#endif
#ifdef LOCAL_SEARCH
    // A search always finds a minimum, including in static texture noise.
    // Do not turn that arbitrary minimum into a moving surface: demand a
    // clear improvement before scheduling the finer per-pixel refinements.
    if(bestError + 0.0001 >= cameraError*0.65)
    { outputTex[id.xy]=camera; return; }
#endif
    outputTex[id.xy] = best * scale + jitter;
#endif
#endif
}
)";
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0, 0};
        ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 4};
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants = {0, 0, sizeof(Params)/4};
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable = {2, ranges};
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        D3D12_ROOT_SIGNATURE_DESC desc{2, params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ID3DBlob* blob = nullptr; ID3DBlob* errors = nullptr;
        HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
        Release(errors);
        if (SUCCEEDED(hr)) hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_));
        Release(blob);
        auto compile = [&](const char* define, ID3D12PipelineState** pso) {
            D3D_SHADER_MACRO macros[] = {{define, "1"}, {nullptr, nullptr}};
            ID3DBlob* code = nullptr; ID3DBlob* error = nullptr;
            HRESULT result = D3DCompile(shader, strlen(shader), nullptr, macros, nullptr,
                "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error);
            if (FAILED(result)) snprintf(why, bytes, "image motion shader: %.150s",
                error ? static_cast<const char*>(error->GetBufferPointer()) : "compile failed");
            Release(error);
            if (SUCCEEDED(result))
            {
                D3D12_COMPUTE_PIPELINE_STATE_DESC d{}; d.pRootSignature = root_;
                d.CS = {code->GetBufferPointer(), code->GetBufferSize()};
                result = device->CreateComputePipelineState(&d, IID_PPV_ARGS(pso));
            }
            Release(code); return SUCCEEDED(result);
        };
        if (FAILED(hr) || !compile("DOWNSAMPLE", &downsample_) ||
            !compile("SEARCH", &search_) || !compile("MERGE", &merge_) ||
            !compile("LOCAL_SEARCH", &local_) || !compile("FINE_SEARCH", &fine_)) return false;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = 2 * 2 * kSets * kDescriptors;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap_)))) return false;
        descriptorSize_ = device->GetDescriptorHandleIncrementSize(hd.Type);
        return true;
    }
    bool CreateEye(int index, unsigned width, unsigned height, ID3D12Resource* color,
        ID3D12Resource* camera, ID3D12Resource* output)
    {
        ReleaseEye(index);
        auto& eye = eyes_[index]; eye.width = width; eye.height = height;
        if (!heap_ || !Texture(width, height, color->GetDesc().Format, &eye.history)) return false;
        for (unsigned level = 0; level < kLevels; ++level)
        {
            unsigned factor = 2u << level;
            unsigned w = (width + factor - 1) / factor, h = (height + factor - 1) / factor;
            const unsigned vectorFactor = level == 1 ? 8 : factor;
            if (!Texture((width+vectorFactor-1)/vectorFactor,(height+vectorFactor-1)/vectorFactor,
                DXGI_FORMAT_R16G16_FLOAT,&eye.vectors[level])) return false;
            for (unsigned bank = 0; bank < 2; ++bank)
                if (!Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,&eye.pyramid[bank][level])) return false;
        }
        for (unsigned bank = 0; bank < 2; ++bank)
        {
            unsigned base = (index * 2 + bank) * kSets * kDescriptors;
            for (unsigned level = 0; level < kLevels; ++level)
            {
                Descriptors(base + level * 5, level ? eye.pyramid[bank][level-1] : color,
                    eye.history, camera, camera, eye.pyramid[bank][level]);
                Descriptors(base + (3 + level) * 5, level < 2 ? color : eye.pyramid[bank][level],
                    level < 2 ? eye.history : eye.pyramid[bank^1][level], camera,
                    level == 2 ? camera : eye.vectors[level+1], eye.vectors[level]);
            }
            Descriptors(base + 6 * 5, color, eye.history, camera, eye.vectors[0], output);
        }
        return true;
    }
    void Record(int index, ID3D12GraphicsCommandList* list, ID3D12Resource* color,
        ID3D12Resource* output, float jitterX, float jitterY, bool reset)
    {
        auto& eye = eyes_[index];
        const auto read = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        const auto write = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        unsigned base = (index * 2 + eye.cursor) * kSets * kDescriptors;
        list->SetDescriptorHeaps(1, &heap_); list->SetComputeRootSignature(root_);
        auto dispatch = [&](unsigned set, unsigned w, unsigned h, float scale,
            bool coarse, ID3D12PipelineState* pso, ID3D12Resource* target, bool transition) {
            Params params{w,h,eye.width,eye.height,scale,jitterX,jitterY,
                eye.valid && !reset ? 1.0f : 0.0f,coarse ? 1u : 0u,{}};
            if (transition) Transition(list,target,read,write);
            list->SetPipelineState(pso);
            list->SetComputeRoot32BitConstants(0,sizeof(Params)/4,&params,0);
            list->SetComputeRootDescriptorTable(1,Gpu(base+set*5));
            list->Dispatch((w+7)/8,(h+7)/8,1);
            Transition(list,target,write,read);
        };
        for (unsigned level=0; level<kLevels; ++level)
        {
            unsigned factor=2u<<level;
            dispatch(level,(eye.width+factor-1)/factor,(eye.height+factor-1)/factor,
                float(factor),false,downsample_,eye.pyramid[eye.cursor][level],true);
        }
        for (int level=2; level>=0; --level)
        {
            unsigned factor=level==1 ? 8 : 2u<<level;
            dispatch(3+level,(eye.width+factor-1)/factor,(eye.height+factor-1)/factor,
                level < 2 ? 1.0f : float(factor),level==2,
                level==0 ? fine_ : (level==1 ? local_ : search_),eye.vectors[level],true);
        }
        dispatch(6,eye.width,eye.height,1.0f,false,merge_,output,false);
        Transition(list,color,read,D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(list,eye.history,read,D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(eye.history,color);
        Transition(list,color,D3D12_RESOURCE_STATE_COPY_SOURCE,read);
        Transition(list,eye.history,D3D12_RESOURCE_STATE_COPY_DEST,read);
        eye.valid=true; eye.cursor^=1;
    }
    void ReleaseEye(int index)
    {
        auto& eye=eyes_[index];
        for(auto& bank:eye.pyramid) for(auto*& p:bank) Release(p);
        for(auto*& p:eye.vectors) Release(p);
        Release(eye.history); eye={};
    }
    void Shutdown()
    {
        ReleaseEye(0); ReleaseEye(1);
        Release(heap_); Release(downsample_); Release(search_); Release(merge_); Release(local_); Release(fine_); Release(root_);
        device_=nullptr;
    }
};
