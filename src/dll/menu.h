#pragma once
#include <windows.h>
#include <d3d11.h>

// Dear ImGui settings menu, drawn into an offscreen texture that vr.cpp
// shows on a floating panel in the headset. Toggled with F1; while open,
// mouse and keyboard go to the menu instead of the game.

inline constexpr int MENU_W = 1280;
inline constexpr int MENU_H = 900;

bool Menu_Init(HWND gameWindow, ID3D11Device* device, ID3D11DeviceContext* context, DXGI_FORMAT rtFormat);
bool Menu_IsOpen();
bool Menu_Toggle();
void Menu_SetVrPointer(bool hit, float u, float v, bool pressed, float scrollY);
void Menu_ClearVrPointer();
void Menu_PostLiveResize(); // schedules admitted resize on MCC's window thread
ID3D11Texture2D* Menu_Render(); // draws the current frame of UI; nullptr on failure

// Panel dragging. The panel itself is locked -- nothing inside it moves -- and
// the ONLY way to reposition it is the grab handle along the top edge. The menu
// reports when the pointer is over that bar; vr.cpp owns the actual drag because
// it is the side that has the controller ray. Placement lives in halomccvr.cfg
// as menu_distance_m / menu_width_m / menu_height_m / menu_side_m, read by both
// the composition quad and the pointer raycast.
bool Menu_PointerOverGrabHandle();
void Menu_SetPanelDragging(bool dragging);

// Force the panel open on the welcome page. Called once per process from the
// frame loop on the first focused frame, when show_welcome is set. The page is
// an ordinary browsable category, not a gate -- show_welcome only controls this
// automatic appearance.
bool Menu_OpenWelcome();
