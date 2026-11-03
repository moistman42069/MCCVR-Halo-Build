#pragma once
#include <imgui.h>

// Dear ImGui reports each item it draws to a test engine (IMGUI_ENABLE_TEST_ENGINE) while TestEngineHookItems is set.
// The settings menu borrows that report to learn its own settings' names for search; nothing listens otherwise.
namespace imgui_item_hooks
{
using ItemInfo = void (*)(ImGuiID id, const char* label, int statusFlags);
inline ItemInfo g_itemInfo = nullptr;
}
