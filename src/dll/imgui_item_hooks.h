#pragma once
#include <imgui.h>

// Dear ImGui reports each item it draws to a test engine (IMGUI_ENABLE_TEST_ENGINE) while TestEngineHookItems is set.
// The settings menu borrows that report to learn its own settings' names for search; nothing listens otherwise.
namespace imgui_item_hooks
{
using ItemInfo = void (*)(ImGuiID id, const char* label, int statusFlags);
inline ItemInfo g_itemInfo = nullptr;
// Every item with an ID, as it is added and before it draws anything: where plain text before it ends.
using ItemAdded = void (*)(ImGuiID id);
inline ItemAdded g_itemAdded = nullptr;
// Dear ImGui names no dropdown in its report. One named here is reported as it is added, before its list can open.
inline ImGuiID g_dropdownId = 0;
inline const char* g_dropdownLabel = nullptr;
}
