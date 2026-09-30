// Included in the Two-Handed Lab panel; shares nothing with the product
// surface. Runtime-only experimental rig: every read comes from
// two_hand_lab_runtime::GetSettings()/GetLastDiagnostics() (the UI never
// recomputes solver geometry), every write goes through
// two_hand_lab_runtime::SetSettings()/ResetSettings(), and NOTHING here
// touches `changed`, ConfigSave, config keys, or persistence. Opening this
// page never enables the Lab; only the explicit checkbox below does.
// Statements only: this file is included inside a category block.
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Two-Handed Lab (experimental)");
        ImGui::TextDisabled("VS-off anchor/authority/damping rig. Runtime-only: nothing here\n"
                            "is saved, and Reset Lab never touches product settings. Product latch continuity\n"
                            "uses the fixed 200 ms transition; Lab adds no second continuity layer.");
        ImGui::Spacing();

        const auto labAnchorName = [](two_hand_lab::AnchorMode mode) -> const char* {
            switch (mode)
            {
            case two_hand_lab::AnchorMode::Production:
                return "Pre-GG production (Production)";
            case two_hand_lab::AnchorMode::AA:
                return "Aim pos → Aim pos (AA)";
            case two_hand_lab::AnchorMode::AG:
                return "Aim pos → Grip pos (AG)";
            case two_hand_lab::AnchorMode::GA:
                return "Grip pos → Aim pos (GA)";
            case two_hand_lab::AnchorMode::GG:
                return "Grip pos → Grip pos (GG)";
            }
            return "Pre-GG production (Production)";
        };
        const auto labResolvedName = [](two_hand_lab::ResolvedAnchor resolved) -> const char* {
            switch (resolved)
            {
            case two_hand_lab::ResolvedAnchor::Production:
                return "Production";
            case two_hand_lab::ResolvedAnchor::AA:
                return "AA";
            case two_hand_lab::ResolvedAnchor::AG:
                return "AG";
            case two_hand_lab::ResolvedAnchor::GA:
                return "GA";
            case two_hand_lab::ResolvedAnchor::GG:
                return "GG";
            }
            return "Production";
        };
        const auto labFallbackName = [](two_hand_lab::FallbackReason reason) -> const char* {
            switch (reason)
            {
            case two_hand_lab::FallbackReason::None:
                return "none";
            case two_hand_lab::FallbackReason::MissingPrimaryGrip:
                return "missing primary grip";
            case two_hand_lab::FallbackReason::MissingSupportGrip:
                return "missing support grip";
            case two_hand_lab::FallbackReason::InvalidInput:
                return "invalid input";
            }
            return "none";
        };
        const auto labAgreementName = [](two_hand_lab::AgreementMode mode) -> const char* {
            return mode == two_hand_lab::AgreementMode::SoftAuthority
                ? "Soft authority" : "Legacy hard 0.35";
        };
        const auto labTemporalName = [](two_hand_lab::TemporalMode mode) -> const char* {
            switch (mode)
            {
            case two_hand_lab::TemporalMode::None:
                return "None";
            case two_hand_lab::TemporalMode::Continuity200ms:
                return "Product continuity (not a Lab mode)";
            case two_hand_lab::TemporalMode::ConstantDamping:
                return "Constant damping";
            case two_hand_lab::TemporalMode::AdaptiveDamping:
                return "Adaptive damping";
            }
            return "None";
        };

        two_hand_lab::Settings labSettings = two_hand_lab_runtime::GetSettings();
        two_hand_lab::Diagnostics labDiag{};
        const bool labHasDiag = two_hand_lab_runtime::GetLastDiagnostics(labDiag);

        ImGui::Text("Status");
        if (g_config.virtual_stock)
        {
            ImGui::Text("Lab overrides: Inactive — Virtual Stock is enabled");
        }
        else if (!labSettings.enabled)
        {
            ImGui::Text("Lab overrides: Inactive — Lab overrides disabled");
        }
        else if (labHasDiag && labDiag.labActiveThisFrame)
        {
            ImGui::Text("Lab overrides: Active");
        }
        else
        {
            ImGui::Text("Lab overrides: Armed (waiting for a Lab-applicable frame)");
        }
        ImGui::Text("Numerical two-hand steering: %s", VR_IsTwoHandAiming() ? "Active" : "Inactive");
        if (labHasDiag)
        {
            ImGui::Text("Primary grip: %s   |   Support grip: %s",
                labDiag.primaryGripValid ? "valid" : "invalid",
                labDiag.supportGripValid ? "valid" : "invalid");
            ImGui::Text("Requested anchor: %s   |   Resolved anchor: %s",
                labAnchorName(labDiag.requestedAnchor),
                labResolvedName(labDiag.resolvedAnchor));
            ImGui::Text("Fallback: %s   |   B: %s",
                labFallbackName(labDiag.fallback),
                labDiag.bValid ? "accepted" : "not accepted");
            ImGui::Text("Agreement: %.3f (%s)   |   Effective influence: %.0f%%",
                labDiag.agreement, labAgreementName(labDiag.agreementMode),
                labDiag.effectiveInfluence * 100.0f);
            ImGui::Text("Temporal: %s (%s)",
                labTemporalName(labDiag.temporalMode),
                labDiag.temporalActive ? "active" : "idle");
        }
        else
        {
            ImGui::TextDisabled("No Lab frame published yet.");
        }
        ImGui::Spacing();

        bool labEnabled = labSettings.enabled;
        if (ImGui::Checkbox("Enable Lab Overrides", &labEnabled))
        {
            labSettings.enabled = labEnabled;
            two_hand_lab_runtime::SetSettings(labSettings);
        }
        ImGui::TextDisabled("Opening this page does not enable the Lab.");
        ImGui::Spacing();
        ImGui::Text("Product settings (shared with Weapon & Aim)");
        changed |= vr_menu::SliderFloat("Two-Hand Smoothing",
            &g_config.two_hand_smoothing_strength,
            kTwoHandSmoothingStrengthMinimum,
            kTwoHandSmoothingStrengthMaximum, "%.0f");
        ImGui::TextDisabled(
            "Reduces small controller-tracking jitter while aiming two-handed.\n"
            "0 = off; higher values apply more smoothing.");
        changed |= ImGui::Checkbox("Persistent support grip",
            &g_config.persistent_support_grip);
        ImGui::TextDisabled(
            "Keeps the support grip attached to its weapon until release or weapon change.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Positional anchor");
        if (ImGui::RadioButton("Pre-GG production (Production)",
                labSettings.anchor == two_hand_lab::AnchorMode::Production))
        {
            labSettings.anchor = two_hand_lab::AnchorMode::Production;
            two_hand_lab_runtime::SetSettings(labSettings);
        }
        if (ImGui::RadioButton("Aim pos → Aim pos (AA)", labSettings.anchor == two_hand_lab::AnchorMode::AA))
        {
            labSettings.anchor = two_hand_lab::AnchorMode::AA;
            two_hand_lab_runtime::SetSettings(labSettings);
        }
        if (ImGui::RadioButton("Aim pos → Grip pos (AG)", labSettings.anchor == two_hand_lab::AnchorMode::AG))
        {
            labSettings.anchor = two_hand_lab::AnchorMode::AG;
            two_hand_lab_runtime::SetSettings(labSettings);
        }
        if (ImGui::RadioButton("Grip pos → Aim pos (GA)", labSettings.anchor == two_hand_lab::AnchorMode::GA))
        {
            labSettings.anchor = two_hand_lab::AnchorMode::GA;
            two_hand_lab_runtime::SetSettings(labSettings);
        }
        if (ImGui::RadioButton("Grip pos → Grip pos (GG)", labSettings.anchor == two_hand_lab::AnchorMode::GG))
        {
            labSettings.anchor = two_hand_lab::AnchorMode::GG;
            two_hand_lab_runtime::SetSettings(labSettings);
        }
        ImGui::TextDisabled("Each option names two POSITIONS: first = primary (trigger-hand)\n"
                            "position, second = support (front-hand) position. These options\n"
                            "change positional pivots only; the primary Aim orientation still\n"
                            "supplies the aim orientation/roll baseline. Pre-GG production\n"
                            "(Production) passes the existing support selection through\n"
                            "unchanged; the shipped product path uses the fixed GG grip pair.");
        float labInfluencePercent = labSettings.offhandInfluence * 100.0f;
        if (vr_menu::SliderFloat("Offhand Influence",
                &labInfluencePercent, 0.0f, 100.0f, "%.0f%%"))
        {
            labSettings.offhandInfluence = labInfluencePercent / 100.0f;
            two_hand_lab_runtime::SetSettings(labSettings);
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Off-hand agreement");
        if (ImGui::RadioButton("Legacy hard 0.35",
                labSettings.agreement == two_hand_lab::AgreementMode::LegacyHard))
        {
            labSettings.agreement = two_hand_lab::AgreementMode::LegacyHard;
            two_hand_lab_runtime::SetSettings(labSettings);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Soft authority",
                labSettings.agreement == two_hand_lab::AgreementMode::SoftAuthority))
        {
            labSettings.agreement = two_hand_lab::AgreementMode::SoftAuthority;
            two_hand_lab_runtime::SetSettings(labSettings);
        }
        if (labSettings.agreement == two_hand_lab::AgreementMode::SoftAuthority)
        {
            float labSoftFull = labSettings.softFullAgreement;
            if (vr_menu::SliderFloat("Full positional authority by",
                    &labSoftFull, two_hand_lab::kSoftFullAgreementMinimum,
                    two_hand_lab::kSoftFullAgreementMaximum, "%.2f"))
            {
                labSettings.softFullAgreement = labSoftFull;
                two_hand_lab_runtime::SetSettings(labSettings);
            }
            const float labFullAngleDeg = acosf(std::clamp(
                labSettings.softFullAgreement, -1.0f, 1.0f)) * 57.2957795f;
            ImGui::TextDisabled("Full authority at dot %.2f (A-B angle %.1f deg).",
                labSettings.softFullAgreement, labFullAngleDeg);
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Temporal experiment (one mode at a time)");
        if (ImGui::RadioButton("None",
                labSettings.temporal == two_hand_lab::TemporalMode::None))
        {
            labSettings.temporal = two_hand_lab::TemporalMode::None;
            two_hand_lab_runtime::SetSettings(labSettings);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Constant damping",
                labSettings.temporal == two_hand_lab::TemporalMode::ConstantDamping))
        {
            labSettings.temporal = two_hand_lab::TemporalMode::ConstantDamping;
            two_hand_lab_runtime::SetSettings(labSettings);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Adaptive damping",
                labSettings.temporal == two_hand_lab::TemporalMode::AdaptiveDamping))
        {
            labSettings.temporal = two_hand_lab::TemporalMode::AdaptiveDamping;
            two_hand_lab_runtime::SetSettings(labSettings);
        }
        if (labSettings.temporal == two_hand_lab::TemporalMode::ConstantDamping)
        {
            float labResponseMs = labSettings.dampingResponseMs;
            if (vr_menu::SliderFloat("Response time",
                    &labResponseMs, two_hand_lab::kDampingResponseMinimumMs,
                    two_hand_lab::kDampingResponseMaximumMs, "%.0f ms"))
            {
                labSettings.dampingResponseMs = labResponseMs;
                two_hand_lab_runtime::SetSettings(labSettings);
            }
        }
        if (labSettings.temporal == two_hand_lab::TemporalMode::AdaptiveDamping)
        {
            float labSlowMs = labSettings.adaptiveSlowResponseMs;
            if (vr_menu::SliderFloat("Small-motion response",
                    &labSlowMs, two_hand_lab::kDampingResponseMinimumMs,
                    two_hand_lab::kDampingResponseMaximumMs, "%.0f ms"))
            {
                labSettings.adaptiveSlowResponseMs = labSlowMs;
                two_hand_lab_runtime::SetSettings(labSettings);
            }
            float labFastMs = labSettings.adaptiveFastResponseMs;
            if (vr_menu::SliderFloat("Large-error response",
                    &labFastMs, two_hand_lab::kDampingResponseMinimumMs,
                    two_hand_lab::kDampingResponseMaximumMs, "%.0f ms"))
            {
                labSettings.adaptiveFastResponseMs = labFastMs;
                two_hand_lab_runtime::SetSettings(labSettings);
            }
            float labFullErrorDeg = labSettings.adaptiveFullErrorDeg;
            if (vr_menu::SliderFloat("Full-speed error",
                    &labFullErrorDeg, two_hand_lab::kAdaptiveFullErrorMinimumDeg,
                    two_hand_lab::kAdaptiveFullErrorMaximumDeg, "%.1f deg"))
            {
                labSettings.adaptiveFullErrorDeg = labFullErrorDeg;
                two_hand_lab_runtime::SetSettings(labSettings);
            }
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Quick presets (runtime state only)");
        if (ImGui::Button("Baseline"))
            two_hand_lab_runtime::SetSettings(
                two_hand_lab::QuickPresetSettings(two_hand_lab::QuickPreset::Baseline));
        ImGui::SameLine();
        if (ImGui::Button("AA 100"))
            two_hand_lab_runtime::SetSettings(
                two_hand_lab::QuickPresetSettings(two_hand_lab::QuickPreset::AA100));
        ImGui::SameLine();
        if (ImGui::Button("AG 100"))
            two_hand_lab_runtime::SetSettings(
                two_hand_lab::QuickPresetSettings(two_hand_lab::QuickPreset::AG100));
        ImGui::SameLine();
        if (ImGui::Button("GG 100"))
            two_hand_lab_runtime::SetSettings(
                two_hand_lab::QuickPresetSettings(two_hand_lab::QuickPreset::GG100));
        ImGui::SameLine();
        if (ImGui::Button("GG 75"))
            two_hand_lab_runtime::SetSettings(
                two_hand_lab::QuickPresetSettings(two_hand_lab::QuickPreset::GG75));
        ImGui::SameLine();
        if (ImGui::Button("GG 50"))
            two_hand_lab_runtime::SetSettings(
                two_hand_lab::QuickPresetSettings(two_hand_lab::QuickPreset::GG50));
        ImGui::TextDisabled("Quick presets reset Agreement and Temporal settings for clean A/B testing.\n"
                            "Baseline keeps the Lab enabled with the Production anchor for parity testing.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Telemetry");
        {
            const TelemetryStatusSnapshot labTelemetry = Telemetry_GetStatus();
            const bool labRecording = labTelemetry.state == TelemetryRecorderState::Starting ||
                labTelemetry.state == TelemetryRecorderState::Recording;
            const bool labBusy = labTelemetry.state == TelemetryRecorderState::Finalizing;
            ImGui::Text("Recorder: %s", Telemetry_StateName(labTelemetry.state));
            if (labRecording)
            {
                if (ImGui::Button("Stop & save##lab"))
                    Telemetry_RequestStop();
            }
            else
            {
                ImGui::BeginDisabled(labBusy ||
                    labTelemetry.state == TelemetryRecorderState::Unavailable);
                if (ImGui::Button("Start recording##lab"))
                    Telemetry_RequestStart();
                ImGui::EndDisabled();
            }
        }
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Reset Lab"))
            two_hand_lab_runtime::ResetSettings();
        ImGui::TextDisabled("Restores Lab defaults and disables the Lab. Virtual Stock\n"
                            "settings and state are never affected.");
