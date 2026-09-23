#pragma once
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <type_traits>
#include "../common/menu_slider_logic.h"

namespace vr_menu
{
// Keep the whole slider+arrows within the original item width. Labels retain
// ImGui's ##/### identity rules, and grouping preserves callers' SameLine use.
template<class Value, class Draw>
bool SliderWithArrows(const char* label, Value* value, Value minimum,
    Value maximum, double step, Draw draw)
{
    ImGui::PushID(label);
    ImGui::BeginGroup();
    const float width=ImGui::CalcItemWidth();
    const float button=ImGui::GetFrameHeight();
    const float gap=ImGui::GetStyle().ItemInnerSpacing.x;
    const Value before=*value;
    const auto nudge=[&](int direction) {
        if constexpr(std::is_integral_v<Value>)
            *value=Value(std::clamp(double(*value)+direction,double(minimum),double(maximum)));
        else
            *value=MenuSliderNudge(*value,minimum,maximum,step,direction);
    };
    ImGui::BeginDisabled(*value<=minimum);
    if(ImGui::ArrowButton("decrease",ImGuiDir_Left)) nudge(-1);
    ImGui::EndDisabled();
    ImGui::SameLine(0,gap);
    ImGui::SetNextItemWidth(std::max(1.0f,width-2*(button+gap)));
    const bool dragged=draw();
    ImGui::SameLine(0,gap);
    ImGui::BeginDisabled(*value>=maximum);
    if(ImGui::ArrowButton("increase",ImGuiDir_Right)) nudge(1);
    ImGui::EndDisabled();
    const char* hidden=std::strstr(label,"##");
    if(!hidden || hidden!=label)
    {
        // Long labels must not make the settings child horizontally scroll or
        // disappear outside the headset panel. Short labels retain one row.
        const float labelWidth=ImGui::CalcTextSize(label,hidden).x;
        const float used=ImGui::GetItemRectMax().x-ImGui::GetWindowPos().x;
        if(used+gap+labelWidth<=ImGui::GetWindowContentRegionMax().x)
            ImGui::SameLine(0,gap);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(label,hidden);
        ImGui::PopTextWrapPos();
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return dragged || *value!=before;
}

inline bool SliderFloat(const char* label,float* value,float minimum,float maximum,
    const char* format="%.3f",ImGuiSliderFlags flags=0)
{
    return SliderWithArrows(label,value,minimum,maximum,MenuSliderStep(format),[&] {
        return ImGui::SliderFloat("##value",value,minimum,maximum,format,flags);
    });
}

inline bool SliderInt(const char* label,int* value,int minimum,int maximum,
    const char* format="%d",ImGuiSliderFlags flags=0)
{
    return SliderWithArrows(label,value,minimum,maximum,1,[&] {
        return ImGui::SliderInt("##value",value,minimum,maximum,format,flags);
    });
}
}
