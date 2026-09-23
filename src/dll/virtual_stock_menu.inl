// Included in the Weapon Aim panel; shares its existing `changed` flag.
ImGui::Separator();
changed |= ImGui::Checkbox("Virtual stock", &g_config.virtual_stock);
ImGui::TextDisabled("Steady a supported weapon against a virtual head or shoulder reference.\n"
                    "Grip controls and the gun's position stay the same.");
if (g_config.virtual_stock)
{
    ImGui::Indent();
    if (!g_config.two_handed_aim)
        ImGui::TextDisabled("Enable Two-handed aiming above to use the stock.");
    changed |= vr_menu::SliderFloat("Stock strength", &g_config.virtual_stock_strength, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::Combo("Stock reference", &g_config.virtual_stock_rear_reference,
                            "Head\0Shoulder\0Chest\0Adaptive\0");
    ImGui::TextDisabled("Zero strength restores normal two-controller aiming.");
    if (ImGui::TreeNode("Advanced stock options"))
    {
        const int mode = g_config.virtual_stock_rear_reference;
        if (mode <= 1)
            changed |= vr_menu::SliderFloat("Rear height from head (m)", &g_config.virtual_stock_rear_height_m, -.30f, .10f, "%.3f");
        if (mode == 1)
        {
            changed |= vr_menu::SliderFloat("Shoulder back (m)", &g_config.virtual_stock_shoulder_back_m, 0.0f, .25f, "%.3f");
            changed |= vr_menu::SliderFloat("Shoulder side (m)", &g_config.virtual_stock_shoulder_side_m, 0.0f, .20f, "%.3f");
        }
        if (mode == 2)
        {
            changed |= vr_menu::SliderFloat("Chest height from head (m)", &g_config.virtual_stock_chest_height_m, -.50f, -.220f, "%.3f");
            changed |= vr_menu::SliderFloat("Chest back (m)", &g_config.virtual_stock_chest_back_m, 0.0f, .25f, "%.3f");
            changed |= vr_menu::SliderFloat("Chest side (m)", &g_config.virtual_stock_chest_side_m, 0.0f, .20f, "%.3f");
        }
        if (mode == 3)
        {
            changed |= vr_menu::SliderFloat("Adaptive top height (m)", &g_config.virtual_stock_adaptive_top_height_m, -.35f, 0.0f, "%.3f");
            changed |= vr_menu::SliderFloat("Adaptive bottom height (m)", &g_config.virtual_stock_adaptive_bottom_height_m, -.65f, -.20f, "%.3f");
            changed |= vr_menu::SliderFloat("Adaptive top half-width (m)", &g_config.virtual_stock_adaptive_top_half_width_m, .02f, .25f, "%.3f");
            changed |= vr_menu::SliderFloat("Adaptive bottom half-width (m)", &g_config.virtual_stock_adaptive_bottom_half_width_m, .02f, .30f, "%.3f");
        }
        changed |= ImGui::Checkbox("Release stock as the gun moves away", &g_config.virtual_stock_proximity_release);
        if (g_config.virtual_stock_proximity_release)
        {
            changed |= vr_menu::SliderFloat("Full stock distance (m)", &g_config.virtual_stock_proximity_full_m, .10f, .60f, "%.3f");
            changed |= vr_menu::SliderFloat("Released distance (m)", &g_config.virtual_stock_proximity_release_m, .15f, .80f, "%.3f");
        }
        ImGui::TextDisabled("Shoulder and chest side follow the selected main hand.\n"
                            "Tracking loss uses normal controller aiming.");
        ImGui::TreePop();
    }
    ImGui::Unindent();
}
