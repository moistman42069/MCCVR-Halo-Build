#pragma once
#include <d3d11.h>
#include "../common/runtime_types.h"

using BloomSetResourcesFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,ID3D11ShaderResourceView* const*);
void Bloom_SetAvailable(bool available,BloomSetResourcesFn original) noexcept;
void Bloom_ObserveSrvCreated(ID3D11Device*,ID3D11ShaderResourceView*) noexcept;
void Bloom_ObserveShaderCreated(ID3D11PixelShader*,const void*,size_t) noexcept;
void Bloom_ObserveShader(ID3D11DeviceContext*,ID3D11PixelShader*,UINT classCount) noexcept;
void Bloom_ObserveResources(ID3D11DeviceContext*,UINT start,UINT count,ID3D11ShaderResourceView* const*) noexcept;
void Bloom_Invalidate(ID3D11DeviceContext*,bool shaderToo=true) noexcept;
void Bloom_ColdTick(GameTitle,uint32_t generation) noexcept;
void Bloom_BeforeResize() noexcept;

class BloomDrawScope {
public:
    explicit BloomDrawScope(ID3D11DeviceContext*) noexcept;
    ~BloomDrawScope();
    BloomDrawScope(const BloomDrawScope&)=delete;
    BloomDrawScope& operator=(const BloomDrawScope&)=delete;
private:
    ID3D11DeviceContext* context{};
    ID3D11ShaderResourceView* original[2]{};
    unsigned mask{};
    bool reading{};
};
