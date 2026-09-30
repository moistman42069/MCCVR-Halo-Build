// Offline ImGui/D3D preview of the production menu palette and embedded font.
// Does not load MCC, OpenXR, or the injected DLL.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <cstdio>
#include <vector>
#include "menu_style.h"
#include "menu_font.generated.h"
using Microsoft::WRL::ComPtr;
int wmain(int argc, wchar_t** argv) {
    if(argc!=2)return 2;
    constexpr UINT width=1400,height=920;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&device,nullptr,&context)))return 3;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;
    desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11RenderTargetView> rtv;
    if(FAILED(device->CreateTexture2D(&desc,nullptr,&texture)) ||
        FAILED(device->CreateRenderTargetView(texture.Get(),nullptr,&rtv)))return 4;
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    io.DisplaySize=ImVec2(width,height);io.DeltaTime=1.f/60.f;
    io.Fonts->AddFontDefault();ImFontConfig cfg{};cfg.FontDataOwnedByAtlas=false;
    auto* heading=io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(menu_assets::oxanium),
        int(sizeof(menu_assets::oxanium)),13.f,&cfg);
    if(!heading)return 5;
    ImGui::StyleColorsDark();vr_menu_style::ApplyTheme();
    ImGui::GetStyle().ScaleAllSizes(1.5f);io.FontGlobalScale=1.6f;
    if(!ImGui_ImplDX11_Init(device.Get(),context.Get()))return 6;
    ImGui_ImplDX11_NewFrame();ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Preview",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushFont(heading);ImGui::TextColored(vr_menu_style::Rgb(vr_menu_style::kAccent),"HALO MCC VR");
    ImGui::PopFont();ImGui::TextDisabled("VR SETTINGS / OFFLINE STYLE PREVIEW");ImGui::Separator();
    ImGui::BeginChild("Navigation",ImVec2(300,-35),ImGuiChildFlags_Borders);
    ImGui::PushFont(heading);
    for(const char* label:{"Welcome","Status","Comfort","3D Theatre","Controls","VR Mappings",
        "Reload & Holsters","Vehicles","Weapon & Aim","Crosshair","Body & Hands","Picture",
        "HUD","Subtitles","Desktop","Scope","Telemetry Recorder","Advanced"})
        ImGui::Selectable(label,strcmp(label,"Body & Hands")==0);
    ImGui::PopFont();ImGui::EndChild();ImGui::SameLine();
    ImGui::BeginChild("Settings",ImVec2(0,-35),ImGuiChildFlags_Borders);
    ImGui::PushFont(heading);ImGui::TextColored(vr_menu_style::Rgb(vr_menu_style::kAccent),"Body & Hands");
    ImGui::PopFont();ImGui::TextDisabled("Arms, shoulders, and how much of Chief you can see.");
    ImGui::Separator();ImGui::Spacing();bool enabled=true,disabled=false;float drop=.12f;
    ImGui::Checkbox("Arm IK (bend arm to controller)",&enabled);
    ImGui::TextWrapped("Bend supported arm models toward the tracked hands. When a model cannot be solved, keep the working hands and weapons; some titles hide the unsupported arms.");
    ImGui::SliderFloat("Right shoulder drop",&drop,0,.3f,"%.3f");
    ImGui::Checkbox("Level shoulders (don't pitch with head)",&enabled);ImGui::Spacing();
    ImGui::Checkbox("Show arms with hands",&enabled);
    ImGui::TextWrapped("Keep the tracked hands and weapons visible, with optional articulated arms. Turn off to hide the arms. Custom rigs need a recognized arm chain.");
    ImGui::Spacing();ImGui::Checkbox("World collision (experimental)",&disabled);
    ImGui::Checkbox("True physical melee (experimental)",&disabled);
    ImGui::Checkbox("Gesture melee (experimental)",&disabled);
    ImGui::Spacing();ImGui::Checkbox("Show full body",&disabled);
    ImGui::TextWrapped("Experimental body visibility. The current Halo 3 switch can affect its first-person hands. Full-body support in the other games is still in development.");
    ImGui::Spacing();ImGui::TextColored(vr_menu_style::Rgb(vr_menu_style::kWarning),"Full-body avatar IK and finger posing are still in development.");
    ImGui::EndChild();ImGui::TextDisabled("L3+R3 recenters and toggles this menu.");
    ImGui::End();ImGui::Render();
    auto* target=rtv.Get();context->OMSetRenderTargets(1,&target,nullptr);
    const float clear[4]={0,0,0,1};context->ClearRenderTargetView(target,clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;if(FAILED(device->CreateTexture2D(&desc,nullptr,&staging)))return 7;
    context->CopyResource(staging.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE map{};
    if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)))return 8;
    std::vector<unsigned char> pixels(width*height*4);
    for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x) {
        auto* from=static_cast<unsigned char*>(map.pData)+y*map.RowPitch+x*4;
        auto* to=pixels.data()+(y*width+x)*4;to[0]=from[2];to[1]=from[1];to[2]=from[0];to[3]=255;
    }
    context->Unmap(staging.Get(),0);FILE* file=nullptr;if(_wfopen_s(&file,argv[1],L"wb"))return 9;
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);
    header.bfSize=header.bfOffBits+DWORD(pixels.size());BITMAPINFOHEADER info{};
    info.biSize=sizeof(info);info.biWidth=width;info.biHeight=-int(height);
    info.biPlanes=1;info.biBitCount=32;info.biSizeImage=DWORD(pixels.size());
    const bool saved=fwrite(&header,sizeof(header),1,file)==1 && fwrite(&info,sizeof(info),1,file)==1 &&
        fwrite(pixels.data(),pixels.size(),1,file)==1;fclose(file);
    ImGui_ImplDX11_Shutdown();ImGui::DestroyContext();
    puts(saved?"PASS: production font atlas and theme rendered offline":"FAIL: preview write");
    return saved?0:10;
}
