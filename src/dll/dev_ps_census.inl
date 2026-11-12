// PIXEL SHADER CENSUS (dev builds only; key dev_ps_census, pad switches dev_ps_census and dev_ps_blank).
//
// Finds the draw behind a full-screen effect in Halo 4. Every pixel shader the game creates while the key is on
// is remembered by its 3Dmigoto FNV-1 hash, the identity this file's shader bridges use. The first time a shader
// is bound for a SMALL draw (six vertices or indices at most: a full-screen pass or a HUD quad) it gets a number
// and a block of lines: the draw's target, viewport, blend and depth state, and the textures it reads, each named
// for what it is to this mod (an eye's image, the game's own scene colour, the crosshair capture). Its
// disassembly is written beside the log as halomccvr_ps_<number>_<hash>.txt.
//
// Once a second, when it differs from the second before, one line lists the numbered shaders that drew, the
// copies and resolves into or out of the game's scene colour and the eye images, and the clears of them: an
// effect that starts or stops shows as a change, and so does a pass that reads the scene colour the eyes are not
// drawn into. Each distinct copy is described once.
//
// `set dev_ps_blank <number>` (pad) binds no pixel shader in place of that shader, so its draws write nothing:
// whether an effect's own draw is what spoils the picture is then one more try away. 0 blanks nothing.
//
// While the key is on, every small draw and every pixel-shader bind takes a lock and a map lookup, and three
// more context functions are hooked. With the key at 0 at startup none of it exists.
#if HALOMCCVR_DEV_DIAGNOSTICS
#include <d3dcompiler.h>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace dev_ps_census
{
struct Shader
{
    uint64_t hash = 0;
    uint32_t number = 0;    // 0 until its first small draw
    uint32_t draws = 0;     // small draws in the current second
    bool bound = false;     // bound at least once
    std::vector<uint8_t> bytecode;
};

constexpr int kRoles = 5;   // VR_DevResourceRole: other, scene colour, eye 0, eye 1, crosshair capture
const char* const kRoleNames[kRoles] = {"other", "scene", "eye0", "eye1", "crosshair"};

SRWLOCK g_lock = SRWLOCK_INIT;
std::unordered_map<uintptr_t, Shader> g_shaders;    // by the shader object's address
uint32_t g_nextNumber = 1;
uint64_t g_secondStartMs = 0;
std::vector<uint32_t> g_drewBefore;                 // the numbers that drew in the second before
uint32_t g_copies[kRoles][kRoles] = {};             // [into][from], this second
uint32_t g_copiesBefore[kRoles][kRoles] = {};
uint32_t g_clears[kRoles] = {};
uint32_t g_clearsBefore[kRoles] = {};
uint32_t g_firstBinds = 0;                          // shaders bound for the first time, this second
uint64_t g_firstBindHashes[6] = {};
struct CopySeen { uintptr_t into, from; };
std::vector<CopySeen> g_copySeen;                   // the distinct copies already described
std::atomic<uint32_t> g_blankCount{0};
std::atomic<uintptr_t> g_blankShaders[8];

bool On() { return g_config.dev_ps_census != 0 && TitleAdapter_GetActiveTitle() == GameTitle::Halo4; }

void WriteDisassembly(uint32_t number, uint64_t hash, const std::vector<uint8_t>& bytecode)
{
    if (bytecode.empty())
        return;
    ID3DBlob* text = nullptr;
    if (FAILED(D3DDisassemble(bytecode.data(), bytecode.size(), 0, nullptr, &text)) || !text)
        return;
    wchar_t path[MAX_PATH]{};
    swprintf_s(path, L"%shalomccvr_ps_%03u_%016llX.txt", LogDirectory(), number,
               static_cast<unsigned long long>(hash));
    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"wb") == 0 && file)
    {
        size_t length = text->GetBufferSize();
        const char* chars = static_cast<const char*>(text->GetBufferPointer());
        while (length && chars[length - 1] == '\0')
            --length;
        fwrite(chars, 1, length, file);
        fclose(file);
    }
    text->Release();
}

void DescribeDraw(ID3D11DeviceContext* context, uint32_t number, uint64_t hash, size_t bytes, UINT count,
                  bool indexed)
{
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    context->OMGetRenderTargets(1, &rtv, &dsv);
    char target[160] = "none";
    if (rtv)
    {
        ID3D11Resource* resource = nullptr;
        rtv->GetResource(&resource);
        VR_DevDescribeResource(resource, target, sizeof(target));
        if (resource)
            resource->Release();
        rtv->Release();
    }
    if (dsv)
        dsv->Release();
    ID3D11BlendState* blend = nullptr;
    FLOAT factor[4]{};
    UINT sampleMask = 0;
    context->OMGetBlendState(&blend, factor, &sampleMask);
    D3D11_BLEND_DESC bd{};
    if (blend)
    {
        blend->GetDesc(&bd);
        blend->Release();
    }
    ID3D11DepthStencilState* depth = nullptr;
    UINT stencilRef = 0;
    context->OMGetDepthStencilState(&depth, &stencilRef);
    D3D11_DEPTH_STENCIL_DESC dd{};
    if (depth)
    {
        depth->GetDesc(&dd);
        depth->Release();
    }
    UINT viewports = 1;
    D3D11_VIEWPORT vp{};
    context->RSGetViewports(&viewports, &vp);
    const D3D11_RENDER_TARGET_BLEND_DESC& rt = bd.RenderTarget[0];
    LOG("PS CENSUS (dev) #%u hash %016llX (%zu bytes): first small draw (%s %u), eye %d, %s depth view; target %s; "
        "viewport %.0f,%.0f %.0fx%.0f; blend %d colour %d,%d op %d alpha %d,%d op %d mask %X factor "
        "%.2f,%.2f,%.2f,%.2f; depth test %d write %d stencil %d",
        number, static_cast<unsigned long long>(hash), bytes, indexed ? "indexed" : "plain", count,
        VR_DevRasterEye(), dsv ? "with a" : "no", target, vp.TopLeftX, vp.TopLeftY, vp.Width, vp.Height,
        rt.BlendEnable ? 1 : 0, static_cast<int>(rt.SrcBlend), static_cast<int>(rt.DestBlend),
        static_cast<int>(rt.BlendOp), static_cast<int>(rt.SrcBlendAlpha), static_cast<int>(rt.DestBlendAlpha),
        static_cast<int>(rt.BlendOpAlpha), static_cast<unsigned>(rt.RenderTargetWriteMask), factor[0], factor[1],
        factor[2], factor[3], dd.DepthEnable ? 1 : 0, static_cast<int>(dd.DepthWriteMask),
        dd.StencilEnable ? 1 : 0);
    ID3D11ShaderResourceView* views[12]{};
    context->PSGetShaderResources(0, 12, views);
    for (UINT slot = 0; slot < 12; ++slot)
    {
        if (!views[slot])
            continue;
        ID3D11Resource* resource = nullptr;
        views[slot]->GetResource(&resource);
        char text[160] = "none";
        VR_DevDescribeResource(resource, text, sizeof(text));
        LOG("PS CENSUS (dev) #%u texture slot %u: %s", number, slot, text);
        if (resource)
            resource->Release();
        views[slot]->Release();
    }
}

// Under the lock: once a second, the line of what drew, copied and cleared, when it changed.
void RollSecond(uint64_t nowMs, std::string& line)
{
    if (nowMs - g_secondStartMs < 1000)
        return;
    g_secondStartMs = nowMs;
    std::vector<std::pair<uint32_t, uint32_t>> drew;
    for (auto& entry : g_shaders)
    {
        if (entry.second.number && entry.second.draws)
            drew.emplace_back(entry.second.number, entry.second.draws);
        entry.second.draws = 0;
    }
    std::sort(drew.begin(), drew.end());
    std::vector<uint32_t> numbers;
    for (const auto& d : drew)
        numbers.push_back(d.first);
    bool copiesChanged = false;
    for (int into = 0; into < kRoles; ++into)
        for (int from = 0; from < kRoles; ++from)
            copiesChanged = copiesChanged || (g_copies[into][from] != 0) != (g_copiesBefore[into][from] != 0);
    bool clearsChanged = false;
    for (int role = 0; role < kRoles; ++role)
        clearsChanged = clearsChanged || (g_clears[role] != 0) != (g_clearsBefore[role] != 0);
    if (numbers != g_drewBefore || copiesChanged || clearsChanged || g_firstBinds)
    {
        char part[96];
        line = "PS CENSUS (dev) second: drew";
        size_t shown = 0;
        for (const auto& d : drew)
        {
            if (shown++ >= 70)
                break;
            snprintf(part, sizeof(part), " #%u x%u", d.first, d.second);
            line += part;
        }
        if (drew.size() > 70)
            line += " (and more)";
        line += "; began:";
        for (uint32_t n : numbers)
            if (std::find(g_drewBefore.begin(), g_drewBefore.end(), n) == g_drewBefore.end())
            {
                snprintf(part, sizeof(part), " #%u", n);
                line += part;
            }
        line += "; ended:";
        for (uint32_t n : g_drewBefore)
            if (std::find(numbers.begin(), numbers.end(), n) == numbers.end())
            {
                snprintf(part, sizeof(part), " #%u", n);
                line += part;
            }
        line += "; copies (into<-from):";
        for (int into = 0; into < kRoles; ++into)
            for (int from = 0; from < kRoles; ++from)
                if (g_copies[into][from])
                {
                    snprintf(part, sizeof(part), " %s<-%s x%u", kRoleNames[into], kRoleNames[from],
                             g_copies[into][from]);
                    line += part;
                }
        line += "; clears:";
        for (int role = 0; role < kRoles; ++role)
            if (g_clears[role])
            {
                snprintf(part, sizeof(part), " %s x%u", kRoleNames[role], g_clears[role]);
                line += part;
            }
        if (g_firstBinds)
        {
            snprintf(part, sizeof(part), "; %u shaders bound for the first time:", g_firstBinds);
            line += part;
            for (uint32_t i = 0; i < g_firstBinds && i < 6; ++i)
            {
                snprintf(part, sizeof(part), " %016llX", static_cast<unsigned long long>(g_firstBindHashes[i]));
                line += part;
            }
        }
    }
    g_drewBefore = numbers;
    memcpy(g_copiesBefore, g_copies, sizeof(g_copies));
    memset(g_copies, 0, sizeof(g_copies));
    memcpy(g_clearsBefore, g_clears, sizeof(g_clears));
    memset(g_clears, 0, sizeof(g_clears));
    g_firstBinds = 0;
}

// HALO 4'S DAMAGE FLASH, LOOKED AT CLOSELY. Two passes: the HUD's damage-flash shader draws the flashes into a
// sixteenth-size target, and a second shader blends that target over the finished picture with (ONE,
// INV_SRC_ALPHA), so alpha without colour paints black. For the first draws of each burst of flashes this logs
// the flash pass's constants and both passes' vertices, and saves the overlay the blend pass reads as
// halomccvr_flash_<burst>_<n>_eye<e>.rgba (width, height as two 32-bit numbers, then RGBA rows).
constexpr uint64_t kFlashPassHash = 0xA16EB90E073677CAull;
constexpr uint64_t kFlashBlendHash = 0x175487ED0C95915Full;
uint64_t g_flashLastMs = 0;
uint32_t g_flashBurst = 0;
uint32_t g_flashPassDraws = 0, g_flashBlendDraws = 0;
std::unordered_map<uintptr_t, std::vector<uint8_t>> g_vertexShaders;   // bytes by shader address
std::vector<uintptr_t> g_vertexShadersWritten;

bool ReadBuffer(ID3D11DeviceContext* context, ID3D11Buffer* buffer, std::vector<uint8_t>& out)
{
    if (!buffer || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return false;
    D3D11_BUFFER_DESC desc{};
    buffer->GetDesc(&desc);
    ID3D11Device* device = nullptr;
    context->GetDevice(&device);
    if (!device)
        return false;
    D3D11_BUFFER_DESC staging = desc;
    staging.Usage = D3D11_USAGE_STAGING;
    staging.BindFlags = 0;
    staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging.MiscFlags = 0;
    staging.StructureByteStride = 0;
    ID3D11Buffer* copy = nullptr;
    bool read = false;
    if (SUCCEEDED(device->CreateBuffer(&staging, nullptr, &copy)) && copy)
    {
        context->CopyResource(copy, buffer);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context->Map(copy, 0, D3D11_MAP_READ, 0, &mapped)) && mapped.pData)
        {
            out.assign(static_cast<const uint8_t*>(mapped.pData),
                       static_cast<const uint8_t*>(mapped.pData) + desc.ByteWidth);
            context->Unmap(copy, 0);
            read = true;
        }
        copy->Release();
    }
    device->Release();
    return read;
}

void LogFloats(const char* what, const uint8_t* bytes, size_t byteCount, size_t most)
{
    std::string line = what;
    char part[32];
    const size_t count = (std::min)(byteCount / 4, most);
    for (size_t i = 0; i < count; ++i)
    {
        float value = 0.0f;
        memcpy(&value, bytes + i * 4, 4);
        snprintf(part, sizeof(part), "%s%.4g", i % 4 == 0 ? " | " : " ", value);
        line += part;
    }
    LOG("%s", line.c_str());
}

void LogVertices(ID3D11DeviceContext* context, const char* what, UINT count, bool indexed)
{
    ID3D11Buffer* vertices = nullptr;
    UINT stride = 0, offset = 0;
    context->IAGetVertexBuffers(0, 1, &vertices, &stride, &offset);
    std::vector<uint8_t> bytes;
    if (vertices && stride && ReadBuffer(context, vertices, bytes))
    {
        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        context->IAGetPrimitiveTopology(&topology);
        char head[160];
        snprintf(head, sizeof(head), "FLASH (dev) %s: %s %u, topology %d, vertex stride %u offset %u of %zu bytes:", what,
                 indexed ? "indexed" : "plain", count, static_cast<int>(topology), stride, offset, bytes.size());
        // The first eight vertices from the offset.
        const size_t start = (std::min)(static_cast<size_t>(offset), bytes.size());
        const size_t span = (std::min)(bytes.size() - start, static_cast<size_t>(stride) * 8);
        LogFloats(head, bytes.data() + start, span, 96);
    }
    else
    {
        LOG("FLASH (dev) %s: the vertex buffer could not be read (stride %u)", what, stride);
    }
    if (vertices)
        vertices->Release();
    // The vertex shader's constants (the first two buffers) and its code, once per shader.
    for (UINT slot = 0; slot < 2; ++slot)
    {
        ID3D11Buffer* constants = nullptr;
        context->VSGetConstantBuffers(slot, 1, &constants);
        std::vector<uint8_t> values;
        if (constants && ReadBuffer(context, constants, values))
        {
            char head[96];
            snprintf(head, sizeof(head), "FLASH (dev) %s: vertex shader constants %u (%zu bytes):", what, slot,
                     values.size());
            LogFloats(head, values.data(), values.size(), 64);
        }
        if (constants)
            constants->Release();
    }
    ID3D11VertexShader* vertexShader = nullptr;
    UINT instances = 0;
    context->VSGetShader(&vertexShader, nullptr, &instances);
    if (vertexShader)
    {
        const uintptr_t key = reinterpret_cast<uintptr_t>(vertexShader);
        vertexShader->Release();
        std::vector<uint8_t> code;
        bool write = false;
        AcquireSRWLockExclusive(&g_lock);
        if (std::find(g_vertexShadersWritten.begin(), g_vertexShadersWritten.end(), key) ==
            g_vertexShadersWritten.end())
        {
            g_vertexShadersWritten.push_back(key);
            const auto found = g_vertexShaders.find(key);
            if (found != g_vertexShaders.end())
                code = found->second;
            write = true;
        }
        ReleaseSRWLockExclusive(&g_lock);
        if (write)
        {
            ID3DBlob* text = nullptr;
            if (!code.empty() && SUCCEEDED(D3DDisassemble(code.data(), code.size(), 0, nullptr, &text)) && text)
            {
                wchar_t path[MAX_PATH]{};
                swprintf_s(path, L"%shalomccvr_flash_vs_%p.txt", LogDirectory(), reinterpret_cast<void*>(key));
                FILE* file = nullptr;
                if (_wfopen_s(&file, path, L"wb") == 0 && file)
                {
                    fwrite(text->GetBufferPointer(), 1, text->GetBufferSize(), file);
                    fclose(file);
                }
                text->Release();
                LOG("FLASH (dev) %s: vertex shader %p written as halomccvr_flash_vs_%p.txt (%zu bytes)", what,
                    reinterpret_cast<void*>(key), reinterpret_cast<void*>(key), code.size());
            }
            else
            {
                LOG("FLASH (dev) %s: vertex shader %p: no code kept (created before the census?)", what,
                    reinterpret_cast<void*>(key));
            }
        }
    }
}

void SaveOverlay(ID3D11DeviceContext* context, uint32_t burst, uint32_t n)
{
    if (context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return;
    ID3D11ShaderResourceView* view = nullptr;
    context->PSGetShaderResources(1, 1, &view);
    if (!view)
        return;
    ID3D11Resource* resource = nullptr;
    view->GetResource(&resource);
    view->Release();
    if (!resource)
        return;
    D3D11_RESOURCE_DIMENSION kind = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    resource->GetType(&kind);
    ID3D11Device* device = nullptr;
    context->GetDevice(&device);
    if (kind == D3D11_RESOURCE_DIMENSION_TEXTURE2D && device)
    {
        ID3D11Texture2D* texture = static_cast<ID3D11Texture2D*>(resource);
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        if (desc.Width <= 512 && desc.Height <= 512 && desc.SampleDesc.Count == 1)
        {
            D3D11_TEXTURE2D_DESC staging = desc;
            staging.Usage = D3D11_USAGE_STAGING;
            staging.BindFlags = 0;
            staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            staging.MiscFlags = 0;
            staging.MipLevels = 1;
            staging.ArraySize = 1;
            ID3D11Texture2D* copy = nullptr;
            if (SUCCEEDED(device->CreateTexture2D(&staging, nullptr, &copy)) && copy)
            {
                context->CopySubresourceRegion(copy, 0, 0, 0, 0, texture, 0, nullptr);
                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (SUCCEEDED(context->Map(copy, 0, D3D11_MAP_READ, 0, &mapped)) && mapped.pData)
                {
                    wchar_t path[MAX_PATH]{};
                    swprintf_s(path, L"%shalomccvr_flash_%03u_%03u_eye%d.rgba", LogDirectory(), burst, n,
                               VR_DevRasterEye());
                    FILE* file = nullptr;
                    if (_wfopen_s(&file, path, L"wb") == 0 && file)
                    {
                        const uint32_t size[2] = {desc.Width, desc.Height};
                        fwrite(size, sizeof(size), 1, file);
                        for (UINT row = 0; row < desc.Height; ++row)
                            fwrite(static_cast<const uint8_t*>(mapped.pData) + row * mapped.RowPitch, 4, desc.Width,
                                   file);
                        fclose(file);
                    }
                    context->Unmap(copy, 0);
                }
                copy->Release();
            }
        }
    }
    if (device)
        device->Release();
    resource->Release();
}

// Called for a small draw whose pixel shader has `hash`, outside the lock.
void InspectFlash(ID3D11DeviceContext* context, uint64_t hash, UINT count, bool indexed)
{
    if (hash != kFlashPassHash && hash != kFlashBlendHash)
        return;
    const uint64_t nowMs = GetTickCount64();
    uint32_t burst = 0, n = 0;
    AcquireSRWLockExclusive(&g_lock);
    if (nowMs - g_flashLastMs > 1500)
    {
        ++g_flashBurst;
        g_flashPassDraws = 0;
        g_flashBlendDraws = 0;
    }
    g_flashLastMs = nowMs;
    burst = g_flashBurst;
    n = hash == kFlashPassHash ? g_flashPassDraws++ : g_flashBlendDraws++;
    ReleaseSRWLockExclusive(&g_lock);
    const bool detail = n < 2;
    const bool sample = n < 24 || (n % 12 == 0 && n < 400);
    if (!sample)
        return;
    if (hash == kFlashPassHash)
    {
        ID3D11Buffer* constants = nullptr;
        context->PSGetConstantBuffers(0, 1, &constants);
        std::vector<uint8_t> values;
        if (constants && ReadBuffer(context, constants, values))
        {
            char head[128];
            snprintf(head, sizeof(head), "FLASH (dev) burst %u pass draw %u eye %d: constants (colour, data, scale) x4, "
                     "centre, scale:", burst, n, VR_DevRasterEye());
            LogFloats(head, values.data(), values.size(), 56);
        }
        if (constants)
            constants->Release();
        if (detail)
            LogVertices(context, "flash pass", count, indexed);
    }
    else
    {
        SaveOverlay(context, burst, n);
        if (detail)
            LogVertices(context, "flash blend", count, indexed);
    }
}

void OnCreateVertexShader(ID3D11VertexShader* shader, const void* bytecode, SIZE_T length)
{
    if (!g_config.dev_ps_census || !shader || !bytecode || !length)
        return;
    std::vector<uint8_t> code(static_cast<const uint8_t*>(bytecode), static_cast<const uint8_t*>(bytecode) + length);
    AcquireSRWLockExclusive(&g_lock);
    g_vertexShaders[reinterpret_cast<uintptr_t>(shader)] = std::move(code);
    ReleaseSRWLockExclusive(&g_lock);
}

void OnCreate(ID3D11PixelShader* shader, const void* bytecode, SIZE_T length, uint64_t hash)
{
    if (!g_config.dev_ps_census)
        return;
    Shader entry;
    entry.hash = hash;
    entry.bytecode.assign(static_cast<const uint8_t*>(bytecode), static_cast<const uint8_t*>(bytecode) + length);
    AcquireSRWLockExclusive(&g_lock);
    g_shaders[reinterpret_cast<uintptr_t>(shader)] = std::move(entry);
    ReleaseSRWLockExclusive(&g_lock);
}

// True: bind no pixel shader in this one's place.
bool OnBind(ID3D11PixelShader* shader)
{
    if (!shader || !On())
        return false;
    const uintptr_t key = reinterpret_cast<uintptr_t>(shader);
    AcquireSRWLockExclusive(&g_lock);
    Shader& entry = g_shaders[key];
    if (!entry.bound)
    {
        entry.bound = true;
        if (g_firstBinds < 6)
            g_firstBindHashes[g_firstBinds] = entry.hash;
        ++g_firstBinds;
    }
    ReleaseSRWLockExclusive(&g_lock);
    const uint32_t blanked = g_blankCount.load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < blanked && i < 8; ++i)
        if (g_blankShaders[i].load(std::memory_order_relaxed) == key)
            return true;
    return false;
}

void OnDraw(ID3D11DeviceContext* context, UINT count, bool indexed)
{
    if (count > 6 || !On())
        return;
    ID3D11PixelShader* shader = nullptr;
    UINT instances = 0;
    context->PSGetShader(&shader, nullptr, &instances);
    if (!shader)
        return;
    const uintptr_t key = reinterpret_cast<uintptr_t>(shader);
    shader->Release();
    uint32_t describe = 0;
    uint64_t hash = 0;
    size_t bytes = 0;
    std::string line;
    AcquireSRWLockExclusive(&g_lock);
    Shader& entry = g_shaders[key];
    ++entry.draws;
    hash = entry.hash;
    if (!entry.number)
    {
        entry.number = g_nextNumber++;
        describe = entry.number;
        bytes = entry.bytecode.size();
        WriteDisassembly(entry.number, entry.hash, entry.bytecode);
    }
    RollSecond(GetTickCount64(), line);
    ReleaseSRWLockExclusive(&g_lock);
    if (describe)
        DescribeDraw(context, describe, hash, bytes, count, indexed);
    InspectFlash(context, hash, count, indexed);
    if (!line.empty())
        LOG("%.900s", line.c_str());
}

void NoteCopy(ID3D11Resource* into, ID3D11Resource* from, const char* how)
{
    if (!On())
        return;
    const int intoRole = VR_DevResourceRole(into), fromRole = VR_DevResourceRole(from);
    if (!intoRole && !fromRole)
        return;
    bool first = false;
    AcquireSRWLockExclusive(&g_lock);
    ++g_copies[intoRole][fromRole];
    const uintptr_t a = reinterpret_cast<uintptr_t>(into), b = reinterpret_cast<uintptr_t>(from);
    first = true;
    for (const CopySeen& seen : g_copySeen)
        if (seen.into == a && seen.from == b)
        {
            first = false;
            break;
        }
    if (first && g_copySeen.size() < 256)
        g_copySeen.push_back({a, b});
    ReleaseSRWLockExclusive(&g_lock);
    if (!first)
        return;
    char intoText[160], fromText[160];
    VR_DevDescribeResource(into, intoText, sizeof(intoText));
    VR_DevDescribeResource(from, fromText, sizeof(fromText));
    LOG("PS CENSUS (dev) first %s of this pair, eye %d: into %s <- from %s", how, VR_DevRasterEye(), intoText,
        fromText);
}

struct ClearSeen { UINT width, height; float colour[4]; };
std::vector<ClearSeen> g_clearSeen;

void NoteClear(ID3D11RenderTargetView* view, const FLOAT colour[4])
{
    if (!view || !On())
        return;
    ID3D11Resource* resource = nullptr;
    view->GetResource(&resource);
    const int role = VR_DevResourceRole(resource);
    // A small target's clear colour, once per shape and colour: the flash pass's sixteenth-size target is one.
    D3D11_RESOURCE_DIMENSION kind = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    if (resource)
        resource->GetType(&kind);
    if (kind == D3D11_RESOURCE_DIMENSION_TEXTURE2D && colour)
    {
        D3D11_TEXTURE2D_DESC desc{};
        static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
        if (desc.Width <= 512 && desc.Height <= 512)
        {
            bool first = true;
            AcquireSRWLockExclusive(&g_lock);
            for (const ClearSeen& seen : g_clearSeen)
                if (seen.width == desc.Width && seen.height == desc.Height &&
                    !memcmp(seen.colour, colour, sizeof(seen.colour)))
                    first = false;
            if (first && g_clearSeen.size() < 64)
                g_clearSeen.push_back({desc.Width, desc.Height, {colour[0], colour[1], colour[2], colour[3]}});
            else
                first = false;
            ReleaseSRWLockExclusive(&g_lock);
            if (first)
                LOG("PS CENSUS (dev) first clear of a %ux%u target (fmt %d) to %.3f,%.3f,%.3f,%.3f, eye %d", desc.Width,
                    desc.Height, static_cast<int>(desc.Format), colour[0], colour[1], colour[2], colour[3],
                    VR_DevRasterEye());
        }
    }
    if (resource)
        resource->Release();
    if (!role)
        return;
    AcquireSRWLockExclusive(&g_lock);
    ++g_clears[role];
    ReleaseSRWLockExclusive(&g_lock);
}

void SetBlank(int number)
{
    uint32_t count = 0;
    uint64_t hash = 0;
    AcquireSRWLockExclusive(&g_lock);
    if (number > 0)
    {
        for (const auto& entry : g_shaders)
            if (entry.second.number == static_cast<uint32_t>(number))
                hash = entry.second.hash;
        // Every shader object made from the same bytes: the game can create one twice.
        for (const auto& entry : g_shaders)
            if (count < 8 && (entry.second.number == static_cast<uint32_t>(number) ||
                              (hash && entry.second.hash == hash)))
                g_blankShaders[count++].store(entry.first, std::memory_order_relaxed);
    }
    g_blankCount.store(count, std::memory_order_relaxed);
    ReleaseSRWLockExclusive(&g_lock);
    LOG("PS CENSUS (dev) blank: shader #%d hash %016llX, %u shader object(s) now bind as none", number,
        static_cast<unsigned long long>(hash), count);
}

using CopySubresourceRegionFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, UINT, UINT,
                                                         UINT, ID3D11Resource*, UINT, const D3D11_BOX*);
using ResolveSubresourceFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, ID3D11Resource*,
                                                      UINT, DXGI_FORMAT);
using ClearRenderTargetViewFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11RenderTargetView*,
                                                         const FLOAT[4]);
using CreateVertexShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*,
                                                         ID3D11VertexShader**);
CreateVertexShaderFn g_origCreateVertexShader = nullptr;
HRESULT STDMETHODCALLTYPE CreateVertexShaderHook(ID3D11Device* device, const void* bytecode, SIZE_T length,
                                                 ID3D11ClassLinkage* linkage, ID3D11VertexShader** shader)
{
    const HRESULT result = g_origCreateVertexShader(device, bytecode, length, linkage, shader);
    if (SUCCEEDED(result) && shader && *shader)
        OnCreateVertexShader(*shader, bytecode, length);
    return result;
}
CopySubresourceRegionFn g_origCopySubresourceRegion = nullptr;
ResolveSubresourceFn g_origResolveSubresource = nullptr;
ClearRenderTargetViewFn g_origClearRenderTargetView = nullptr;

void STDMETHODCALLTYPE CopySubresourceRegionHook(ID3D11DeviceContext* context, ID3D11Resource* into, UINT intoSub,
                                                 UINT x, UINT y, UINT z, ID3D11Resource* from, UINT fromSub,
                                                 const D3D11_BOX* box)
{
    NoteCopy(into, from, box ? "region copy (part)" : "region copy (whole)");
    g_origCopySubresourceRegion(context, into, intoSub, x, y, z, from, fromSub, box);
}

void STDMETHODCALLTYPE ResolveSubresourceHook(ID3D11DeviceContext* context, ID3D11Resource* into, UINT intoSub,
                                              ID3D11Resource* from, UINT fromSub, DXGI_FORMAT format)
{
    NoteCopy(into, from, "resolve");
    g_origResolveSubresource(context, into, intoSub, from, fromSub, format);
}

void STDMETHODCALLTYPE ClearRenderTargetViewHook(ID3D11DeviceContext* context, ID3D11RenderTargetView* view,
                                                 const FLOAT colour[4])
{
    NoteClear(view, colour);
    g_origClearRenderTargetView(context, view, colour);
}

// With the key on at startup: the three extra hooks. They are created here and enabled with the rest.
void CreateHooks(void** deviceVtbl, void** contextVtbl)
{
    if (!g_config.dev_ps_census)
        return;
    const MH_STATUS vertexShader = MH_CreateHook(deviceVtbl[12], (void*)&CreateVertexShaderHook,
                                                 (void**)&g_origCreateVertexShader);
    const MH_STATUS region = MH_CreateHook(contextVtbl[46], (void*)&CopySubresourceRegionHook,
                                           (void**)&g_origCopySubresourceRegion);
    const MH_STATUS resolve = MH_CreateHook(contextVtbl[57], (void*)&ResolveSubresourceHook,
                                            (void**)&g_origResolveSubresource);
    const MH_STATUS clear = MH_CreateHook(contextVtbl[50], (void*)&ClearRenderTargetViewHook,
                                          (void**)&g_origClearRenderTargetView);
    LOG("PS CENSUS (dev) ON: region copy hook %d, resolve hook %d, clear hook %d, vertex shader hook %d (0 = created)",
        static_cast<int>(region), static_cast<int>(resolve), static_cast<int>(clear), static_cast<int>(vertexShader));
}
}

void D3D_DevSetBlankPixelShader(int number) { dev_ps_census::SetBlank(number); }
#endif
