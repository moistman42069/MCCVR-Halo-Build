#pragma once
#include <windows.h>

void NativeMenuPointer_Init(HWND window);
// Render thread publishes only a bounded atomic sample and a coalesced message.
void NativeMenuPointer_Publish(bool active, float u = 0, float v = 0, bool pressed = false);
void NativeMenuPointer_PublishScroll(float stick);
bool NativeMenuPointer_ConsumesTrigger();
void NativeMenuPointer_ReportFailures(); // existing diagnostics worker only
void NativeMenuPointer_FrameStatus(const char* status); // static literal, atomic only
bool NativeMenuPointer_ReadVisual(float& u, float& v, bool& pressed);
void NativeMenuPointer_CursorFailed(); // atomic report; retires optional layer only
// Called before ImGui/native processing, on the window thread.
bool NativeMenuPointer_Message(UINT message, LPARAM lParam = 0);
