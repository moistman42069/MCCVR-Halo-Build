// Included in the Weapon & Aim panel; shares its existing `changed` flag.
// Donor final product surface: Plus first, Standard second, the selected
// product mode's strength, rear reference, and the reset confirmation.
// Lab-only rows stay out of normal navigation.
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Virtual Stock");
        changed |= ImGui::Checkbox("Virtual Stock", &g_config.virtual_stock);
        if (g_config.virtual_stock)
        {
            const bool plusMode = VirtualStockUsesPlusMode(g_config);
            ImGui::Text("Mode");
            if (ImGui::RadioButton("Plus", plusMode))
            {
                SetVirtualStockPlusMode(g_config, true);
                changed = true;
            }
            ImGui::TextDisabled(
                "Stable while shouldered, with more natural freedom\n"
                "when lowering the gun or aiming around corners.");
            if (ImGui::RadioButton("Standard", !plusMode))
            {
                SetVirtualStockPlusMode(g_config, false);
                changed = true;
            }
            ImGui::TextDisabled(
                "Keeps the gun more firmly anchored to the selected virtual stock point.");

            const bool shoulderReference =
                VirtualStockUsesShoulderReference(g_config);
            ImGui::Text("Rear Reference");
            if (ImGui::RadioButton("Centre", !shoulderReference))
            {
                SetVirtualStockShoulderReference(g_config, false);
                changed = true;
            }
            ImGui::TextDisabled(
                "Places the virtual stock point centrally beneath your view.");
            if (ImGui::RadioButton("Shoulder", shoulderReference))
            {
                SetVirtualStockShoulderReference(g_config, true);
                changed = true;
            }
            ImGui::TextDisabled(
                "Offsets the virtual stock point toward your firing shoulder.");
            float stockStrengthPercent =
                VirtualStockActiveStrength(g_config) * 100.0f;
            if (vr_menu::SliderFloat("Virtual Stock Strength",
                    &stockStrengthPercent,
                    kVirtualStockStrengthMinimum * 100.0f,
                    kVirtualStockStrengthMaximum * 100.0f, "%.0f%%"))
            {
                VirtualStockActiveStrength(g_config) =
                    stockStrengthPercent / 100.0f;
                changed = true;
            }
            changed |= vr_menu::SliderFloat("Stock Height (m)",
                &g_config.virtual_stock_rear_height_m,
                kVirtualStockRearHeightMinimumM,
                kVirtualStockRearHeightMaximumM, "%.3f");

            if (VirtualStockUsesShoulderReference(g_config))
            {
                changed |= vr_menu::SliderFloat("Shoulder back (m)",
                    &g_config.virtual_stock_shoulder_back_m,
                    kVirtualStockShoulderBackMinimumM,
                    kVirtualStockShoulderBackMaximumM, "%.3f");
                changed |= vr_menu::SliderFloat("Shoulder side (m)",
                    &g_config.virtual_stock_shoulder_side_m,
                    kVirtualStockShoulderSideMinimumM,
                    kVirtualStockShoulderSideMaximumM, "%.3f");
            }

            // Planted Standard ignores legacy proximity attenuation by design.
            // The persisted virtual_stock_proximity_* keys remain compatible
            // and dormant; normal product UI no longer exposes them.
        }

        ImGui::Spacing();
        if (!g_virtualStockResetArmed)
        {
            if (ImGui::Button("Reset Virtual Stock Settings"))
                g_virtualStockResetArmed = true;
        }
        else
        {
            ImGui::Text("Reset Virtual Stock settings?");
            ImGui::TextDisabled(
                "This will restore all Virtual Stock settings to their defaults.");
            if (ImGui::Button("Cancel##virtualstockreset"))
                g_virtualStockResetArmed = false;
            ImGui::SameLine();
            if (ImGui::Button("Reset##virtualstockreset"))
            {
                ResetVirtualStockSettings(g_config);
                g_virtualStockResetArmed = false;
                changed = true;
            }
        }
