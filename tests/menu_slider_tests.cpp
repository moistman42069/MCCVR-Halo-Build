#include "../src/dll/menu_slider.h"
#include <cmath>
#include <cstdio>

int main()
{
    ImGui::CreateContext();
    auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;
    io.DisplaySize=ImVec2(800,500);
    io.DeltaTime=1.0f/60;
    unsigned char* pixels=nullptr; int width=0,height=0;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    float fractional=0.25f;
    int integer=5;
    ImVec2 origins[2]{};
    bool changed[2]{};
    float button=0;
    auto frame=[&](float mouseX,float mouseY,bool down) {
        io.AddMousePosEvent(mouseX,mouseY);
        io.AddMouseButtonEvent(0,down);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10,10));
        ImGui::SetNextWindowSize(ImVec2(750,400));
        ImGui::Begin("Slider input test",nullptr,ImGuiWindowFlags_NoTitleBar|
            ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings);
        button=ImGui::GetFrameHeight();
        origins[0]=ImGui::GetCursorScreenPos();
        ImGui::SetNextItemWidth(300);
        changed[0]=vr_menu::SliderFloat("Setting##fractional",&fractional,0.0f,1.0f,"%.2f");
        origins[1]=ImGui::GetCursorScreenPos();
        ImGui::SetNextItemWidth(300);
        changed[1]=vr_menu::SliderInt("Setting##integer",&integer,1,10);
        ImGui::End();
        ImGui::Render();
    };
    int failures=0;
    auto check=[&](bool value,const char* message) {
        if(!value) { std::fprintf(stderr,"FAIL: %s (float=%f int=%d)\n",message,fractional,integer); ++failures; }
    };
    frame(-100,-100,false);
    auto click=[&](int row,bool increase) {
        const float x=origins[row].x+(increase ? 300-button*0.5f : button*0.5f);
        const float y=origins[row].y+button*0.5f;
        frame(x,y,false); frame(x,y,true); frame(x,y,false);
    };
    click(0,true);
    check(std::fabs(fractional-0.26f)<1e-6f && changed[0] && !changed[1] && integer==5,
        "right arrow changes just the selected fractional slider and returns changed");
    click(0,false);
    check(std::fabs(fractional-0.25f)<1e-6f && changed[0],"left arrow subtracts the last digit");
    click(1,true);
    check(integer==6 && changed[1] && !changed[0],"integer arrows use one and independent widget identity");
    fractional=1;
    click(0,true);
    check(fractional==1 && !changed[0],"upper endpoint arrow is disabled");
    integer=1;
    click(1,false);
    check(integer==1 && !changed[1],"lower endpoint arrow is disabled");
    const float x=origins[0].x+150, y=origins[0].y+button*0.5f;
    frame(x,y,false); frame(x,y,true); frame(x,y,false);
    check(fractional>0.4f && fractional<0.6f,"the original slider remains draggable between its arrows");
    check(ImGui::GetDrawData()->TotalVtxCount>0,"the complete slider controls produce renderable draw data");
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(10,10));
    ImGui::SetNextWindowSize(ImVec2(360,400));
    ImGui::Begin("Narrow settings pane",nullptr,ImGuiWindowFlags_NoSavedSettings);
    const auto start=ImGui::GetCursorScreenPos();
    ImGui::SetNextItemWidth(300);
    vr_menu::SliderFloat("A long accessibility setting label that must wrap inside the settings pane",
        &fractional,0.f,1.f,"%.2f");
    check(ImGui::GetItemRectMax().x<=start.x+ImGui::GetContentRegionAvail().x+1,
        "long slider label stays within the available pane width");
    check(ImGui::GetItemRectSize().y>button*1.5f,"long slider label wraps below the controls");
    ImGui::End();ImGui::Render();
    ImGui::DestroyContext();
    if(!failures) std::puts("VR menu slider input tests passed");
    return failures ? 1 : 0;
}
