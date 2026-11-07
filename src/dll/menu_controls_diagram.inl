// THE SIDE PANEL (menu.cpp, inside its namespace): drawn to the right of the menu in the same texture, and shown in the
// headset as a panel of its own beside the menu while Menu_SidePanelShown. On the Welcome page it is the credits, as
// the launcher lists them; on the Controls page it is the controls diagram: both Touch controllers, drawn here, with what each control does in the game chosen under VR Mappings
// (controls_diagram_logic.h), for one context at a time. Pointing at a control or its label picks both out; the same
// highlight is what an interactive mode would drive from a pressed button.

bool g_sidePanelShown = false;

struct ControlsDiagramState
{
    controls_diagram::Context context = controls_diagram::Context::OnFoot;
    int hot = -1;   // the control picked out, or -1
} g_controlsDiagram;

// One controller's drawing, in a 120 x 280 box for the RIGHT controller (the left one is its mirror image), seen from
// above with the front at the top, with no tracking ring (as Quest 3's Touch Plus): the stick resting near the outer edge, the system / menu button below it and a little in, the face buttons on the inner side beside the stick
// (B / Y just below the stick's height, A / X lower and a little nearer the stick), the trigger under the front and the
// grip on the handle's inner side. A generic Touch-style controller, laid out as Quest 2's are.
namespace touch_drawing
{
constexpr float kWidth = 120.0f, kHeight = 280.0f;
constexpr ImVec2 kStick{86.0f, 92.0f};
constexpr float kStickRadius = 16.0f;
constexpr ImVec2 kUpperButton{40.0f, 98.0f}, kLowerButton{56.0f, 123.0f};
constexpr float kButtonRadius = 10.5f;
constexpr ImVec2 kSystemButton{92.0f, 127.0f};
constexpr ImVec2 kTriggerMin{42.0f, 48.0f}, kTriggerMax{76.0f, 68.0f};
constexpr ImVec2 kGripMin{24.0f, 156.0f}, kGripMax{40.0f, 228.0f};
constexpr ImVec2 kHandleMin{36.0f, 128.0f}, kHandleMax{80.0f, 272.0f};
constexpr ImVec2 kFace{60.0f, 104.0f}, kFaceRadius{55.0f, 45.0f};
}

// The anchor of a control on the drawing: where its leader line starts.
ImVec2 TouchAnchor(controls_diagram::Control control, ImVec2 origin, float scale)
{
    using namespace controls_diagram;
    using namespace touch_drawing;
    const bool left = OnLeftController(control);
    ImVec2 at{};
    switch (control)
    {
    case LeftTrigger: case RightTrigger: at = {(kTriggerMin.x + kTriggerMax.x) * 0.5f, kTriggerMin.y + 6.0f}; break;
    case LeftGrip: case RightGrip: at = {(kGripMin.x + kGripMax.x) * 0.5f, (kGripMin.y + kGripMax.y) * 0.5f}; break;
    case Y: case B: at = kUpperButton; break;
    case X: case A: at = kLowerButton; break;
    case LeftStick: case RightStick: at = {kStick.x, kStick.y - kStickRadius + 4.0f}; break;
    case LeftStickClick: case RightStickClick: at = kStick; break;
    case LeftMenu: at = kSystemButton; break;
    default: break;
    }
    if (left) at.x = kWidth - at.x;
    return {origin.x + at.x * scale, origin.y + at.y * scale};
}

// Draws one controller at `origin`. `hot` is the control picked out (its outline lit), or -1.
void DrawTouchController(ImDrawList* draw, bool left, ImVec2 origin, float scale, int hot)
{
    using namespace controls_diagram;
    using namespace touch_drawing;
    const auto P = [&](ImVec2 p) { return ImVec2(origin.x + (left ? kWidth - p.x : p.x) * scale, origin.y + p.y * scale); };
    const auto Box = [&](ImVec2 a, ImVec2 b, ImVec2& outMin, ImVec2& outMax) {
        const ImVec2 pa = P(a), pb = P(b);
        outMin = {std::min(pa.x, pb.x), std::min(pa.y, pb.y)};
        outMax = {std::max(pa.x, pb.x), std::max(pa.y, pb.y)};
    };
    const ImU32 body = ImGui::GetColorU32(Rgb(kSurfaceBg));
    const ImU32 face = ImGui::GetColorU32(Rgb(kRaised));
    const ImU32 edge = ImGui::GetColorU32(Rgb(kSeparator));
    const ImU32 part = ImGui::GetColorU32(Rgb(kRaisedHover));
    const ImU32 lit = ImGui::GetColorU32(Rgb(kAccent));
    const ImU32 litFill = ImGui::GetColorU32(Rgb(kAccentDown));
    const ImU32 label = ImGui::GetColorU32(Rgb(kTextMain));
    const auto isHot = [&](Control a, Control b) { return hot == static_cast<int>(left ? a : b); };

    // The trigger, in front of the face.
    {
        ImVec2 a, b;
        Box(kTriggerMin, kTriggerMax, a, b);
        const bool on = isHot(LeftTrigger, RightTrigger);
        draw->AddRectFilled(a, b, on ? litFill : part, 8.0f * scale);
        draw->AddRect(a, b, on ? lit : edge, 8.0f * scale, 0, on ? 3.0f : 1.5f);
    }
    // The handle, then the grip on its inner side.
    {
        ImVec2 a, b;
        Box(kHandleMin, kHandleMax, a, b);
        draw->AddRectFilled(a, b, body, 22.0f * scale);
        draw->AddRect(a, b, edge, 22.0f * scale, 0, 1.5f);
        Box(kGripMin, kGripMax, a, b);
        const bool on = isHot(LeftGrip, RightGrip);
        draw->AddRectFilled(a, b, on ? litFill : part, 7.0f * scale);
        draw->AddRect(a, b, on ? lit : edge, 7.0f * scale, 0, on ? 3.0f : 1.5f);
    }
    // The face.
    draw->AddEllipseFilled(P(kFace), ImVec2(kFaceRadius.x * scale, kFaceRadius.y * scale), face);
    draw->AddEllipse(P(kFace), ImVec2(kFaceRadius.x * scale, kFaceRadius.y * scale), edge, 0.0f, 0, 1.5f);
    // The stick: its base, and its cap lit for the stick or its click.
    {
        const bool on = isHot(LeftStick, RightStick) || isHot(LeftStickClick, RightStickClick);
        draw->AddCircleFilled(P(kStick), kStickRadius * scale, body);
        draw->AddCircleFilled(P(kStick), (kStickRadius - 5.0f) * scale, on ? litFill : part);
        draw->AddCircle(P(kStick), (kStickRadius - 5.0f) * scale, on ? lit : edge, 0, on ? 3.0f : 1.5f);
    }
    // The two face buttons, lettered.
    const auto button = [&](ImVec2 at, const char* letter, bool on) {
        draw->AddCircleFilled(P(at), kButtonRadius * scale, on ? litFill : part);
        draw->AddCircle(P(at), kButtonRadius * scale, on ? lit : edge, 0, on ? 3.0f : 1.5f);
        const ImVec2 size = ImGui::CalcTextSize(letter);
        draw->AddText(ImVec2(P(at).x - size.x * 0.5f, P(at).y - size.y * 0.5f), label, letter);
    };
    button(kUpperButton, left ? "Y" : "B", isHot(Y, B));
    button(kLowerButton, left ? "X" : "A", isHot(X, A));
    // The small button under the stick: the menu button on the left, the system button on the right (no callout).
    {
        const bool on = left && hot == static_cast<int>(LeftMenu);
        draw->AddCircleFilled(P(kSystemButton), 5.0f * scale, on ? lit : part);
        draw->AddCircle(P(kSystemButton), 5.0f * scale, edge, 0, 1.0f);
    }
}

void DrawControlsDiagram()
{
    using namespace controls_diagram;
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // What it shows: the game chosen under VR Mappings, as its rows stand, and its layout while it is running.
    const int title = MappingTitle();
    const auto game = static_cast<GameTitle>(title + 1);
    const bool running = weapon_interaction::TitleIndex(TitleAdapter_GetActiveTitle()) == title;
    vr_mapping::Transports layout{};
    vr_mapping::ExtraTransports extraLayout{};
    const bool layoutKnown = running && Game_ReadVrActionBindings(layout, GetTickCount64());
    const bool extraKnown = running && Game_ReadVrExtraBindings(extraLayout, GetTickCount64());
    Setup setup{};
    setup.game = game;
    setup.context = g_controlsDiagram.context;
    setup.leftHanded = g_config.left_handed;
    setup.remap = g_config.vr_action_mapping;
    setup.thumbrestDpad = g_config.quest_thumbrest_dpad;
    setup.bindings = &g_config.vr_bindings[title];
    setup.inputs = &g_config.vr_inputs[title];
    setup.extra = &g_config.vr_extra_bindings[title];
    setup.mod = &g_config.vr_mod_bindings;
    setup.layout = layoutKnown ? &layout : nullptr;
    setup.extraLayout = extraKnown ? &extraLayout : nullptr;
    // Buttons the settings turn off: the flashlight's anywhere, and on foot with "Gestures only" reload and weapon
    // switching while their gestures are on (manual_weapon_input::Blocked).
    if (g_config.disable_flashlight_input) setup.blockedAlways |= 1u << vr_mapping::Flashlight;
    if (g_config.gesture_only_weapon_input && g_config.manual_reload) setup.blocked |= 1u << vr_mapping::Reload;
    if (g_config.gesture_only_weapon_input && g_config.weapon_holsters) setup.blocked |= 1u << vr_mapping::SwitchWeapon;
    const Labels labels = Build(setup);

    ImGui::PushFont(g_headingFont);
    ImGui::TextColored(Rgb(kAccent), "Control Configuration Summary");
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Visual summary of your current configuration for: %s.", weapon_interaction::kTitleNames[title]);
    ImGui::TextDisabled("Updates live as you change control options in the main panel.");
    ImGui::PopTextWrapPos();
    if (!g_config.vr_action_mapping)
        ImGui::TextDisabled("Remap buttons is off: buttons are the game's own.");
    else if (!layoutKnown)
        ImGui::TextDisabled("Automatic actions show the usual layout until the game is running.");
    // The contexts, side by side under the heading.
    for (unsigned c = 0; c < static_cast<unsigned>(Context::Count); ++c)
    {
        if (c) ImGui::SameLine();
        const bool selected = g_controlsDiagram.context == static_cast<Context>(c);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, Rgb(kSelected));
        if (ImGui::Button(kContextNames[c], ImVec2(190.0f, 0.0f)))
            g_controlsDiagram.context = static_cast<Context>(c);
        if (selected) ImGui::PopStyleColor();
    }
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.82f);
    const ImVec2 areaMin = ImGui::GetCursorScreenPos();
    const ImVec2 areaMax(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - ImGui::GetStyle().WindowPadding.x,
                         ImGui::GetWindowPos().y + ImGui::GetWindowSize().y - ImGui::GetStyle().WindowPadding.y);
    const float x0 = areaMin.x, x1 = areaMax.x;

    // What no row maps, as the settings have it: each control's built-in jobs in grey boxes, the gestures under the
    // diagram.
    Gestures gestures{};
    gestures.smoothTurn = g_config.turn_smooth;
    gestures.snapDegrees = g_config.turn_snap_deg;
    gestures.vehicleSmoothTurn = g_config.vehicle_smooth_turn;
    gestures.twoHanded = g_config.two_handed_aim;
    gestures.holsters = g_config.weapon_holsters;
    gestures.holsterLocation = g_config.weapon_holster_location;
    gestures.manualReload = g_config.manual_reload;
    gestures.gestureMelee = g_config.gesture_melee;
    gestures.meleeNeedsGrip = g_config.gesture_melee_require_grip;
    gestures.meleeOffHand = g_config.gesture_melee_off_hand;
    gestures.physicalMelee = g_config.physical_melee;
    gestures.shakeReload = g_config.dual_wield_shake_reload;
    gestures.physicalCrouch = g_config.physical_crouch;
    gestures.physicalRunning = g_config.physical_running;
    gestures.dpadHand = g_config.dpad_hand;
    const Labels builtIns = BuiltIns(setup, gestures);
    const auto gestureLines = GestureLines(setup, gestures);
    const float bullet = ImGui::GetFontSize();
    // Each gesture: its name, then what to do, wrapped beside the name.
    const auto gestureName = [](const GestureLine& line) { return line.name + " - "; };
    const auto gestureWrap = [&](const GestureLine& line) {
        return (x1 - x0) - bullet - ImGui::CalcTextSize(gestureName(line).c_str()).x;
    };
    float notesHeight = ImGui::GetFontSize() + 6.0f;
    for (const auto& line : gestureLines)
        notesHeight += ImGui::CalcTextSize(line.how.c_str(), nullptr, false, gestureWrap(line)).y + 3.0f;

    // The diagram: the two controllers side by side, each control's callout on its own controller's outer side, and
    // each action in a callout a box of its own.
    const float top = areaMin.y, bottom = areaMax.y - notesHeight - 14.0f;
    const float calloutWidth = 360.0f, pad = 6.0f, chipPad = 3.0f, chipGap = 3.0f;
    const float scale = std::min(1.8f, ((x1 - x0) - 2.0f * calloutWidth - 56.0f) / (2.0f * touch_drawing::kWidth));
    const float drawWidth = touch_drawing::kWidth * scale, drawHeight = touch_drawing::kHeight * scale;
    const float midX = (x0 + x1) * 0.5f;
    const float drawTop = top + std::max(0.0f, ((bottom - top) - drawHeight) * 0.15f);
    const ImVec2 leftOrigin(midX - 8.0f - drawWidth, drawTop), rightOrigin(midX + 8.0f, drawTop);

    // Lay each side's callouts out by its anchors' heights, one under another, inside the area.
    struct Callout { Control control; ImVec2 anchor; ImVec2 min, max; float titleHeight; };
    Callout callouts[ControlCount]{};
    const float chipWrap = calloutWidth - 2.0f * pad - 2.0f * chipPad;
    float lowest = drawTop + drawHeight;
    for (int side = 0; side < 2; ++side)
    {
        const bool left = side == 0;
        Callout* list[ControlCount]{};
        int count = 0;
        for (unsigned c = 0; c < ControlCount; ++c)
        {
            const auto control = static_cast<Control>(c);
            if (OnLeftController(control) != left) continue;
            Callout& callout = callouts[c];
            callout.control = control;
            callout.anchor = TouchAnchor(control, left ? leftOrigin : rightOrigin, scale);
            callout.titleHeight = ImGui::GetFontSize();
            float height = pad + callout.titleHeight;
            if (labels[c].empty() && builtIns[c].empty())
                height += chipGap + ImGui::GetFontSize();
            for (const auto* lines : {&builtIns[c], &labels[c]})
                for (const auto& line : *lines)
                    height += chipGap + ImGui::CalcTextSize(line.c_str(), nullptr, false, chipWrap).y + 2.0f * chipPad;
            height += pad;
            const float x = left ? x0 : x1 - calloutWidth;
            callout.min = {x, callout.anchor.y - height * 0.5f};
            callout.max = {x + calloutWidth, callout.anchor.y + height * 0.5f};
            list[count++] = &callout;
        }
        std::sort(list, list + count, [](const Callout* a, const Callout* b) { return a->anchor.y < b->anchor.y; });
        const float gap = 6.0f;
        float floor = top;
        for (int i = 0; i < count; ++i)
        {
            const float height = list[i]->max.y - list[i]->min.y;
            list[i]->min.y = std::max(list[i]->min.y, floor);
            list[i]->max.y = list[i]->min.y + height;
            floor = list[i]->max.y + gap;
        }
        const float over = count ? list[count - 1]->max.y - bottom : 0.0f;
        if (over > 0.0f)
        {
            // Pushed past the bottom: pull the stack up from the bottom, never above the top.
            float ceiling = bottom;
            for (int i = count - 1; i >= 0; --i)
            {
                const float height = list[i]->max.y - list[i]->min.y;
                list[i]->max.y = std::min(list[i]->max.y, ceiling);
                list[i]->min.y = std::max(top, list[i]->max.y - height);
                list[i]->max.y = list[i]->min.y + height;
                ceiling = list[i]->min.y - gap;
            }
            // Taller than the area: one under another from the top, never overlapping; the notes go below them.
            floor = top;
            for (int i = 0; i < count; ++i)
            {
                const float height = list[i]->max.y - list[i]->min.y;
                list[i]->min.y = std::max(list[i]->min.y, floor);
                list[i]->max.y = list[i]->min.y + height;
                floor = list[i]->max.y + gap;
            }
        }
        lowest = std::max(lowest, count ? list[count - 1]->max.y : top);
    }

    // What the pointer is on: a callout, or a control on the drawing.
    int hot = -1;
    if (ImGui::IsWindowHovered())
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        for (unsigned c = 0; c < ControlCount && hot < 0; ++c)
        {
            const Callout& callout = callouts[c];
            if (mouse.x >= callout.min.x && mouse.x <= callout.max.x && mouse.y >= callout.min.y && mouse.y <= callout.max.y)
                hot = static_cast<int>(c);
        }
        float nearest = (16.0f * scale) * (16.0f * scale);
        for (unsigned c = 0; c < ControlCount && hot < 0; ++c)
        {
            const ImVec2 d(mouse.x - callouts[c].anchor.x, mouse.y - callouts[c].anchor.y);
            const float distance = d.x * d.x + d.y * d.y;
            if (distance < nearest) { nearest = distance; hot = static_cast<int>(c); }
        }
    }
    g_controlsDiagram.hot = hot;

    // The controllers, then the leader lines over them (a grip's line would otherwise vanish under the handle), then
    // the callouts.
    DrawTouchController(draw, true, leftOrigin, scale, hot);
    DrawTouchController(draw, false, rightOrigin, scale, hot);
    for (unsigned c = 0; c < ControlCount; ++c)
    {
        const Callout& callout = callouts[c];
        const bool left = OnLeftController(callout.control);
        const bool on = hot == static_cast<int>(c);
        const ImU32 color = ImGui::GetColorU32(on ? Rgb(kAccent) : Rgb(kTextDim, 0.55f));
        const float y = callout.min.y + pad + callout.titleHeight * 0.5f;
        const float edgeX = left ? callout.max.x : callout.min.x;
        const ImVec2 elbow(edgeX + (left ? 14.0f : -14.0f), y);
        draw->AddLine(ImVec2(edgeX, y), elbow, color, on ? 2.5f : 1.5f);
        draw->AddLine(elbow, callout.anchor, color, on ? 2.5f : 1.5f);
    }
    for (unsigned c = 0; c < ControlCount; ++c)
    {
        const Callout& callout = callouts[c];
        const bool on = hot == static_cast<int>(c);
        draw->AddCircleFilled(callout.anchor, on ? 4.5f : 3.0f, ImGui::GetColorU32(on ? Rgb(kAccent) : Rgb(kTextDim)));
        draw->AddRectFilled(callout.min, callout.max, ImGui::GetColorU32(on ? Rgb(kRaisedHover) : Rgb(kSurfaceBg)), 6.0f);
        draw->AddRect(callout.min, callout.max, ImGui::GetColorU32(on ? Rgb(kAccent) : Rgb(kBorder)), 6.0f, 0,
                      on ? 2.0f : 1.0f);
        ImVec2 at(callout.min.x + pad, callout.min.y + pad);
        draw->AddText(at, ImGui::GetColorU32(Rgb(kAccent)), kControlNames[c]);
        at.y += callout.titleHeight;
        if (labels[c].empty() && builtIns[c].empty())
        {
            at.y += chipGap;
            draw->AddText(at, ImGui::GetColorU32(Rgb(kTextDim)), "No mapping");
        }
        // Each action in its own box, so one never runs into the next: the built-in jobs first, in grey, then the
        // mapped actions.
        const auto chip = [&](const std::string& line, bool builtIn) {
            at.y += chipGap;
            const float textHeight = ImGui::CalcTextSize(line.c_str(), nullptr, false, chipWrap).y;
            const ImVec2 chipMax(callout.max.x - pad, at.y + textHeight + 2.0f * chipPad);
            const ImVec4 fill = builtIn ? Rgb(0x2C3439) : Rgb(on ? kSelected : kRaised);
            draw->AddRectFilled(at, chipMax, ImGui::GetColorU32(fill), 4.0f);
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x + chipPad, at.y + chipPad),
                          ImGui::GetColorU32(builtIn ? Rgb(0xB9C4C9) : Rgb(kTextMain)), line.c_str(), nullptr, chipWrap);
            at.y = chipMax.y;
        };
        for (const auto& line : builtIns[c]) chip(line, true);
        for (const auto& line : labels[c]) chip(line, false);
    }

    // The gestures, under the diagram and the callouts.
    ImVec2 at(x0, std::max(areaMax.y - notesHeight, lowest + 14.0f));
    draw->AddLine(ImVec2(x0, at.y - 6.0f), ImVec2(x1, at.y - 6.0f), ImGui::GetColorU32(Rgb(kSeparator)));
    draw->AddText(at, ImGui::GetColorU32(Rgb(kAccent)), "Gestures");
    at.y += ImGui::GetFontSize() + 6.0f;
    for (const auto& line : gestureLines)
    {
        const std::string name = gestureName(line);
        const float nameWidth = ImGui::CalcTextSize(name.c_str()).x;
        draw->AddCircleFilled(ImVec2(at.x + bullet * 0.35f, at.y + bullet * 0.5f), 2.5f, ImGui::GetColorU32(Rgb(kTextDim)));
        draw->AddText(ImVec2(at.x + bullet, at.y), ImGui::GetColorU32(Rgb(kAccentHot)), name.c_str());
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x + bullet + nameWidth, at.y),
                      ImGui::GetColorU32(Rgb(kTextMain)), line.how.c_str(), nullptr, gestureWrap(line));
        at.y += ImGui::CalcTextSize(line.how.c_str(), nullptr, false, gestureWrap(line)).y + 3.0f;
    }
    ImGui::PopFont();
}

// The credits, from the copy of CREDITS.txt built into the mod (the launcher's own fallback): read once, as UTF-8 for
// Dear ImGui. The links are left out: opening a browser mid-game is the launcher's job.
struct MenuCredit { std::string section, name, what; };
const std::vector<MenuCredit>& MenuCredits()
{
    static const std::vector<MenuCredit> credits = [] {
        const auto utf8 = [](const std::wstring& text) {
            std::string out(text.size() * 3, '\0');
            out.resize(static_cast<size_t>(WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                out.data(), static_cast<int>(out.size()), nullptr, nullptr)));
            return out;
        };
        const auto* bytes = reinterpret_cast<const char*>(menu_assets::credits);
        const int size = static_cast<int>(sizeof(menu_assets::credits));
        std::wstring text(static_cast<size_t>(size), L'\0');
        text.resize(static_cast<size_t>(MultiByteToWideChar(CP_UTF8, 0, bytes, size, text.data(), size)));
        std::vector<MenuCredit> list;
        for (const auto& entry : ParseCredits(text))
        {
            list.push_back({utf8(entry.section), utf8(entry.name), utf8(entry.what)});
        }
        return list;
    }();
    return credits;
}

void DrawCreditsPanel()
{
    ImGui::PushFont(g_headingFont);
    ImGui::TextColored(Rgb(kAccent), "Credits");
    ImGui::PopFont();
    ImGui::TextDisabled("The people behind Halo MCC VR. Their links are in the launcher's Credits.");
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::BeginChild("##credits", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    ImGui::PushTextWrapPos(0.0f);
    for (const auto& credit : MenuCredits())
    {
        if (!credit.section.empty())
        {
            ImGui::Spacing();
            ImGui::PushFont(g_headingFont);
            ImGui::TextColored(Rgb(kAccent), "%s", credit.section.c_str());
            ImGui::PopFont();
            ImGui::Separator();
        }
        if (credit.name.empty())
        {
            ImGui::TextUnformatted(credit.what.c_str());
            ImGui::Spacing();
            continue;
        }
        ImGui::Spacing();
        ImGui::TextColored(Rgb(kAccentHot), "%s", credit.name.c_str());
        if (!credit.what.empty()) ImGui::TextUnformatted(credit.what.c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

void DrawSidePanel()
{
    if (!g_sidePanelShown)
    {
        g_controlsDiagram.hot = -1;
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(MENU_W), 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(MENU_SIDE_W), static_cast<float>(MENU_H)), ImGuiCond_Always);
    ImGui::Begin("##sidepanel", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings);
    if (g_activeCategory == Cat_Welcome)
    {
        g_controlsDiagram.hot = -1;
        DrawCreditsPanel();
    }
    else
        DrawControlsDiagram();
    ImGui::End();
}
