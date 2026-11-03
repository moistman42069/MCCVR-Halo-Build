// The test-engine hooks Dear ImGui calls when built with IMGUI_ENABLE_TEST_ENGINE. Only item labels are passed on.
#include "imgui_item_hooks.h"
#include <imgui_internal.h>

void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID, const ImRect&, const ImGuiLastItemData*) {}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags flags)
{
    if (imgui_item_hooks::g_itemInfo && label) imgui_item_hooks::g_itemInfo(id, label, flags);
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }
