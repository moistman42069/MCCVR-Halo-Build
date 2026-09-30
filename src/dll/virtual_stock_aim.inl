// Virtual-stock productised semantics re-hosted from the donor final state
// (Standard/Plus product modes, donor defaults, Hybrid mode 3 with neck/seat
// neutral-capture state). Included inside vr.cpp's anonymous namespace after
// CurrentAimPoseInputs. The default-off path keeps the legacy controller solver
// shape; this file never moves result.pose.position.

    struct ResolvedVirtualStockAimSettings
    {
        VirtualStockTestProfile profile = VirtualStockTestProfile::Custom;
        VirtualStockAimSettings settings{};
    };

    ResolvedVirtualStockAimSettings
    CurrentResolvedVirtualStockAimSettings() noexcept
    {
        const VirtualStockAimSettings userSettings =
            VirtualStockAimSettingsFromConfig(
                g_config, g_hybridDiagnosticOverride.Load());
        ResolvedVirtualStockAimSettings resolved{};
        resolved.profile = g_virtualStockTestProfile.Load();
        resolved.settings = ResolveVirtualStockTestProfile(
            resolved.profile, userSettings);
        return resolved;
    }

    VirtualStockAimSettings CurrentEffectiveVirtualStockAimSettings() noexcept
    {
        return CurrentResolvedVirtualStockAimSettings().settings;
    }

    void ApplyVirtualStockAimSettings(
        AimPoseInputs& inputs, const VirtualStockAimSettings& settings,
        VirtualStockTestProfile profile) noexcept
    {
        inputs.testProfileUsed = profile;
        inputs.virtualStockEnabled = settings.virtualStockEnabled;
        inputs.virtualStockStrength = settings.virtualStockStrength;
        inputs.virtualStockRearHeightM = settings.virtualStockRearHeightM;
        inputs.virtualStockRearReference = settings.virtualStockRearReference;
        inputs.virtualStockShoulderBackM = settings.virtualStockShoulderBackM;
        inputs.virtualStockShoulderSideM = settings.virtualStockShoulderSideM;
        inputs.virtualStockChestHeightM = settings.virtualStockChestHeightM;
        inputs.virtualStockChestBackM = settings.virtualStockChestBackM;
        inputs.virtualStockChestSideM = settings.virtualStockChestSideM;
        inputs.virtualStockAdaptiveTopHeightM =
            settings.virtualStockAdaptiveTopHeightM;
        inputs.virtualStockAdaptiveBottomHeightM =
            settings.virtualStockAdaptiveBottomHeightM;
        inputs.virtualStockAdaptiveTopHalfWidthM =
            settings.virtualStockAdaptiveTopHalfWidthM;
        inputs.virtualStockAdaptiveBottomHalfWidthM =
            settings.virtualStockAdaptiveBottomHalfWidthM;
        inputs.virtualStockHybridOffhandInfluence =
            settings.virtualStockHybridOffhandInfluence;
        inputs.virtualStockHybridAdsReference =
            settings.virtualStockHybridAdsReference;
        inputs.virtualStockHybridSeatFullM =
            settings.virtualStockHybridSeatFullM;
        inputs.virtualStockHybridSeatReleaseM =
            settings.virtualStockHybridSeatReleaseM;
        inputs.hybridHorizontalRearReleaseEnabled =
            settings.hybridHorizontalRearReleaseEnabled;
        inputs.hybridHorizontalRearReleaseFullM =
            settings.hybridHorizontalRearReleaseFullM;
        inputs.hybridHorizontalRearReleaseReleaseM =
            settings.hybridHorizontalRearReleaseReleaseM;
        inputs.hybridInverseNeckEnabled = settings.hybridInverseNeckEnabled;
        inputs.hybridInverseNeckStrength = settings.hybridInverseNeckStrength;
        inputs.hybridInverseNeckForwardM = settings.hybridInverseNeckForwardM;
        inputs.hybridInverseNeckUpM = settings.hybridInverseNeckUpM;
        inputs.hybridInverseNeckLateralM = settings.hybridInverseNeckLateralM;
        inputs.hybridDiagnosticOverride = settings.hybridDiagnosticOverride;
        inputs.virtualStockProximityRelease =
            settings.virtualStockProximityRelease;
        inputs.virtualStockProximityFullM = settings.virtualStockProximityFullM;
        inputs.virtualStockProximityReleaseM =
            settings.virtualStockProximityReleaseM;
    }

    VirtualStockAimSettings VirtualStockAimSettingsFromAimPoseInputs(
        const AimPoseInputs& inputs) noexcept
    {
        VirtualStockAimSettings settings{};
        settings.virtualStockEnabled = inputs.virtualStockEnabled;
        settings.virtualStockStrength = inputs.virtualStockStrength;
        settings.virtualStockRearHeightM = inputs.virtualStockRearHeightM;
        settings.virtualStockRearReference = inputs.virtualStockRearReference;
        settings.virtualStockShoulderBackM = inputs.virtualStockShoulderBackM;
        settings.virtualStockShoulderSideM = inputs.virtualStockShoulderSideM;
        settings.virtualStockChestHeightM = inputs.virtualStockChestHeightM;
        settings.virtualStockChestBackM = inputs.virtualStockChestBackM;
        settings.virtualStockChestSideM = inputs.virtualStockChestSideM;
        settings.virtualStockAdaptiveTopHeightM =
            inputs.virtualStockAdaptiveTopHeightM;
        settings.virtualStockAdaptiveBottomHeightM =
            inputs.virtualStockAdaptiveBottomHeightM;
        settings.virtualStockAdaptiveTopHalfWidthM =
            inputs.virtualStockAdaptiveTopHalfWidthM;
        settings.virtualStockAdaptiveBottomHalfWidthM =
            inputs.virtualStockAdaptiveBottomHalfWidthM;
        settings.virtualStockHybridOffhandInfluence =
            inputs.virtualStockHybridOffhandInfluence;
        settings.virtualStockHybridAdsReference =
            inputs.virtualStockHybridAdsReference;
        settings.virtualStockHybridSeatFullM =
            inputs.virtualStockHybridSeatFullM;
        settings.virtualStockHybridSeatReleaseM =
            inputs.virtualStockHybridSeatReleaseM;
        settings.hybridHorizontalRearReleaseEnabled =
            inputs.hybridHorizontalRearReleaseEnabled;
        settings.hybridHorizontalRearReleaseFullM =
            inputs.hybridHorizontalRearReleaseFullM;
        settings.hybridHorizontalRearReleaseReleaseM =
            inputs.hybridHorizontalRearReleaseReleaseM;
        settings.hybridInverseNeckEnabled = inputs.hybridInverseNeckEnabled;
        settings.hybridInverseNeckStrength = inputs.hybridInverseNeckStrength;
        settings.hybridInverseNeckForwardM = inputs.hybridInverseNeckForwardM;
        settings.hybridInverseNeckUpM = inputs.hybridInverseNeckUpM;
        settings.hybridInverseNeckLateralM = inputs.hybridInverseNeckLateralM;
        settings.hybridDiagnosticOverride = inputs.hybridDiagnosticOverride;
        settings.virtualStockProximityRelease =
            inputs.virtualStockProximityRelease;
        settings.virtualStockProximityFullM =
            inputs.virtualStockProximityFullM;
        settings.virtualStockProximityReleaseM =
            inputs.virtualStockProximityReleaseM;
        return settings;
    }

    AimPoseInputs AimPoseInputsForProfile(
        const AimPoseInputs& base,
        VirtualStockTestProfile profile) noexcept
    {
        AimPoseInputs resolved = base;
        // Two-Hand Lab tranche 2A: control/counterfactual solves stay
        // Lab-inert. The canonical solve and VR_GetAimPose carry the Lab
        // through CurrentStockAimPoseInputs; the A/B/C counterfactuals must
        // reproduce the stock-only baselines, so only the enable is cleared
        // here (stored selections persist and may re-activate on a later
        // VS-off solve).
        resolved.twoHandLabEnabled = false;
        const VirtualStockAimSettings settings =
            ResolveVirtualStockTestProfile(
                profile, VirtualStockAimSettingsFromAimPoseInputs(base));
        ApplyVirtualStockAimSettings(resolved, settings, profile);
        return resolved;
    }

    AimPoseInputs AimPoseInputsForSelectedProfile(
        const AimPoseInputs& base,
        const VirtualStockAimSettings& userSettings,
        VirtualStockTestProfile selectedProfile) noexcept
    {
        AimPoseInputs resolved = base;
        ApplyVirtualStockAimSettings(
            resolved,
            ResolveVirtualStockTestProfile(selectedProfile, userSettings),
            selectedProfile);
        return resolved;
    }

    // Stock-aware primary-aim constructor for an explicit neutral-capture
    // sample. The committed cross-thread path supplies these values from its
    // matching prepared-frame publication rather than reading frame-owned
    // state live.
    AimPoseInputs CurrentStockAimPoseInputsWithNeutralCapture(
        bool rightValid, const XrPosef& right,
        bool leftValid, const XrPosef& left,
        bool coherentHeadValid, const XrVector3f& coherentHeadPosition,
        const XrQuaternionf& coherentHeadOrientation,
        bool supportGripPositionValid,
        const XrVector3f& supportGripPosition,
        bool primaryGripPositionValid,
        const XrVector3f& primaryGripPosition,
        bool inverseNeckNeutralValid,
        const XrQuaternionf& inverseNeckNeutralOrientation,
        uint64_t inverseNeckNeutralCaptureSerial,
        uint64_t inverseNeckNeutralCaptureContactSpaceEpoch) noexcept
    {
        AimPoseInputs inputs = CurrentAimPoseInputs(rightValid, right, leftValid, left);
        const VirtualStockAimSettings userSettings =
            VirtualStockAimSettingsFromConfig(
                g_config, g_hybridDiagnosticOverride.Load());
        inputs = AimPoseInputsForSelectedProfile(
            inputs, userSettings, g_virtualStockTestProfile.Load());
        inputs.virtualStockLeftHanded =
            g_capturedLeftHanded.load(std::memory_order_acquire);
        inputs.headValid = coherentHeadValid;
        inputs.headPosition = coherentHeadPosition;
        inputs.headOrientation = coherentHeadOrientation;
        inputs.inverseNeckNeutralValid = inverseNeckNeutralValid;
        inputs.inverseNeckNeutralOrientation = inverseNeckNeutralOrientation;
        inputs.inverseNeckNeutralCaptureSerial =
            inverseNeckNeutralCaptureSerial;
        inputs.inverseNeckNeutralCaptureContactSpaceEpoch =
            inverseNeckNeutralCaptureContactSpaceEpoch;
        const auto toPoint = [](const XrVector3f& v) {
            return virtual_stock::Point3{v.x, v.y, v.z};
        };
        // Virtual Stock / Lab support endpoint ("Reduce Support-Hand
        // Rotation"). This is the only SOLVE-TIME consumer of the RSR
        // checkbox: it selects the Virtual Stock support endpoint (and the Lab
        // Production anchor pass-through) and is reported as provenance. The
        // free two-hand production geometry does not read it - it consumes the
        // grip fields below directly, whose validity is tracking-only.
        const virtual_stock::SupportEndpointSelection support =
            virtual_stock::SelectTwoHandSupportEndpoint(
                g_config.two_hand_support_grip_pose, toPoint(left.position),
                supportGripPositionValid, toPoint(supportGripPosition));
        inputs.supportPosition = XrVector3f{
            support.position.x, support.position.y, support.position.z};
        inputs.supportEndpointUsedGrip = support.usedGrip;
        inputs.supportGripPoseEnabled = g_config.two_hand_support_grip_pose;
        // W3: the free two-hand (VS-OFF) offhand directional authority. Stamped
        // here, with the other product two-hand settings read from Config, so
        // the solve consumes one coherent copy. Virtual Stock solves never read
        // it, and the A/B/C counterfactual copies inherit it unchanged (their
        // "Virtual Stock off" baseline is this frame's real free two-hand
        // product path).
        inputs.twoHandOffhandInfluence = g_config.two_hand_offhand_influence;
        // Two-Hand Lab tranche 2A: attach one coherent runtime snapshot plus
        // the raw grip endpoints for this exact sample (same-sample proof in
        // vr.cpp at the Lab store). Tranche 2B also stamps the snapshot's
        // generation: the temporal packet echoes it so consumers fail open on
        // a same-serial settings toggle. The solver gates on
        // inputs.virtualStockEnabled, never on g_config here.
        const TwoHandLabSettingsSnapshot labSnapshot =
            SnapshotTwoHandLabSettingsWithGeneration();
        inputs.twoHandLabEnabled = labSnapshot.settings.enabled;
        inputs.twoHandLabAnchor =
            static_cast<int>(labSnapshot.settings.anchor);
        inputs.twoHandLabOffhandInfluence =
            labSnapshot.settings.offhandInfluence;
        inputs.twoHandLabAgreement =
            static_cast<int>(labSnapshot.settings.agreement);
        inputs.twoHandLabSoftFullAgreement =
            labSnapshot.settings.softFullAgreement;
        inputs.twoHandLabTemporal =
            static_cast<int>(labSnapshot.settings.temporal);
        inputs.twoHandLabGeneration = labSnapshot.generation;
        inputs.primaryGripValid = primaryGripPositionValid;
        inputs.primaryGripPosition = primaryGripPosition;
        inputs.supportGripValid = supportGripPositionValid;
        inputs.supportGripPosition = supportGripPosition;
        // Persistent support grip (F14): qualify this invocation against the
        // durable relationship BEFORE the support-capable raw aim can be
        // selected, and resolve an unreadable relationship or a title-denied
        // invocation to the ordinary calibrated one-hand path for this
        // assembly only. The durable relationship is never mutated here, and
        // `twoHandLatched` above deliberately stays the DURABLE engagement
        // (g_twoHandLatched, never this per-invocation gate) so neither the
        // Two-Handed Lab's latch-edge source nor the existing aim-authority
        // meaning can be changed by a gated invocation.
        const support_grip::SolveSupportQualification supportQualification =
            ResolveAimSupportQualification();
        inputs.supportMayConsume = supportQualification.supportTrusted;
        // T13 corrective: the retention input is the SAME qualification bit,
        // never a second source of truth. `supportTrusted` is only ever set by
        // QualifySolveSupport() for a readable, engaged relationship after its
        // feature-off short circuit and its untrusted early return, so PG off,
        // an unwired title, an unreadable or disengaged relationship and an
        // invocation denied through the live `g_supportInvocationUntrusted`
        // channel all leave retention false by construction. That denial
        // channel is set only by a title trust seam (today the H3/ODST
        // `ControllerWorldPoseEx` carrier around its weapon-hand solve), so it
        // protects exactly the assemblies such a seam wraps; the frame-thread
        // assemblies (the presented pose/reticle solve) carry the durable
        // relationship qualification into the published pose and rely on each
        // title's consumer seam to refuse support-derived geometry for an
        // untrusted invocation. Per-title audit, including the consumers that
        // do not re-check:
        // docs/PERSISTENT-GRIP-PORT-2026-09-27.md section 3G.1.
        inputs.supportSteeringRetained = supportQualification.supportTrusted;
        inputs.supportEpoch = supportQualification.supportEpoch;
        inputs.supportRelationshipReadable =
            supportQualification.relationshipReadable;
        inputs.supportRelationshipEngaged =
            supportQualification.relationshipEngaged;
        inputs.supportSolveSerial =
            g_preparedSerialPublished.load(std::memory_order_acquire);
        // Data-only carry of the frozen forcing flag for same-frame telemetry:
        // the application below is unchanged and nothing reads this copy in
        // the solve, so control semantics are identical. `twoHandEnabled ==
        // false` alone cannot distinguish config-off, dual presentation and
        // this explicit forcing.
        inputs.supportForceOneHand = supportQualification.forceOneHand;
        if (supportQualification.forceOneHand)
            inputs.twoHandEnabled = false;
        return inputs;
    }

    // Preserve the live-capture constructor and its existing callers. The
    // frame thread owns this state; committed consumers use the explicit
    // overload above so they never read it without g_headCs.
    AimPoseInputs CurrentStockAimPoseInputs(
        bool rightValid, const XrPosef& right,
        bool leftValid, const XrPosef& left,
        bool coherentHeadValid, const XrVector3f& coherentHeadPosition,
        const XrQuaternionf& coherentHeadOrientation,
        bool supportGripPositionValid,
        const XrVector3f& supportGripPosition,
        bool primaryGripPositionValid,
        const XrVector3f& primaryGripPosition) noexcept
    {
        const XrQuaternionf inverseNeckNeutralOrientation{
            g_inverseNeckNeutralCapture.neutralOrientation.x,
            g_inverseNeckNeutralCapture.neutralOrientation.y,
            g_inverseNeckNeutralCapture.neutralOrientation.z,
            g_inverseNeckNeutralCapture.neutralOrientation.w};
        return CurrentStockAimPoseInputsWithNeutralCapture(
            rightValid, right, leftValid, left,
            coherentHeadValid, coherentHeadPosition, coherentHeadOrientation,
            supportGripPositionValid, supportGripPosition,
            primaryGripPositionValid, primaryGripPosition,
            g_inverseNeckNeutralCapture.neutralValid,
            inverseNeckNeutralOrientation,
            g_inverseNeckNeutralCapture.captureSerial,
            g_inverseNeckNeutralCapture.captureContactSpaceEpoch);
    }

    template <bool CaptureTrace>
    AimPoseResult ComputeAimPoseImpl(
        const AimPoseInputs& inputs, AimPoseTrace* trace) noexcept
    {
        if constexpr (CaptureTrace)
            *trace = {};
        if constexpr (CaptureTrace)
        {
            virtual_stock::Quat4 neutralOrientation{};
            trace->inverseNeckNeutralValid =
                inputs.hybridInverseNeckEnabled &&
                inputs.inverseNeckNeutralValid &&
                virtual_stock::TryNormalizeQuaternion(
                    {inputs.inverseNeckNeutralOrientation.x,
                     inputs.inverseNeckNeutralOrientation.y,
                     inputs.inverseNeckNeutralOrientation.z,
                     inputs.inverseNeckNeutralOrientation.w},
                    neutralOrientation);
            trace->inverseNeckStrength = std::isfinite(
                inputs.hybridInverseNeckStrength)
                ? std::clamp(inputs.hybridInverseNeckStrength, 0.0f, 1.0f)
                : 0.0f;
            if (trace->inverseNeckNeutralValid)
            {
                trace->inverseNeckNeutralOrientation = {
                    neutralOrientation.x, neutralOrientation.y,
                    neutralOrientation.z, neutralOrientation.w};
                trace->inverseNeckNeutralCaptureSerial =
                    inputs.inverseNeckNeutralCaptureSerial;
                trace->inverseNeckNeutralCaptureContactSpaceEpoch =
                    inputs.inverseNeckNeutralCaptureContactSpaceEpoch;
            }
        }
        AimPoseResult result{};
        if (!inputs.rightValid)
            return result;

        const auto traceVec = [](const virtual_stock::Point3& value) {
            return AimTraceVec3{value.x, value.y, value.z};
        };
        const auto traceXrVec = [](const XrVector3f& value) {
            return AimTraceVec3{value.x, value.y, value.z};
        };
        if constexpr (CaptureTrace)
        {
            trace->aValid = true;
            trace->primaryQuaternion = {
                inputs.right.orientation.x, inputs.right.orientation.y,
                inputs.right.orientation.z, inputs.right.orientation.w};
            trace->primaryDirection = traceXrVec(
                Rotate(inputs.right.orientation, {0.0f, 0.0f, -1.0f}));
            trace->finalDirectionValid = true;
            trace->finalDirection = trace->primaryDirection;
        }

        result.updateTwoHandActivity = true;
        result.pose = inputs.right;
        // Persistent support grip solve-time receipt: pass the assembly's
        // qualification through untouched; `supportTrusted` is narrowed to
        // what this exact solve consumed at finishAimPose(). Zero/false when
        // the feature is off, so a PG-off solve reports no support provenance.
        result.supportEpoch = inputs.supportEpoch;
        result.supportRelationshipReadable = inputs.supportRelationshipReadable;
        result.supportRelationshipEngaged = inputs.supportRelationshipEngaged;
        result.supportSolveSerial = inputs.supportSolveSerial;
        // Telemetry-only carry: the frozen forcing flag of the assembly this
        // solve was computed from. No consumer treats it as a control input.
        result.supportForceOneHand = inputs.supportForceOneHand;

        auto finishAimPose = [&]() {
            auto multiply = [](const XrQuaternionf& a,
                               const XrQuaternionf& b) {
                return XrQuaternionf{
                    a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
                    a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
                    a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
                    a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z};
            };
            constexpr float kDegToRad = 0.01745329252f;
            const float yaw = inputs.gunYawDeg * kDegToRad;
            const float pitch = inputs.gunPitchDeg * kDegToRad;
            const float roll = inputs.gunRollDeg * kDegToRad;
            const XrQuaternionf qYaw{
                0.0f, sinf(yaw*0.5f), 0.0f, cosf(yaw*0.5f)};
            const XrQuaternionf qPitch{
                sinf(pitch*0.5f), 0.0f, 0.0f, cosf(pitch*0.5f)};
            const XrQuaternionf qRoll{
                0.0f, 0.0f, sinf(-roll*0.5f), cosf(roll*0.5f)};
            const XrQuaternionf corrected = multiply(
                result.pose.orientation,
                multiply(multiply(qYaw, qPitch), qRoll));
            const float length = sqrtf(
                corrected.x*corrected.x + corrected.y*corrected.y +
                corrected.z*corrected.z + corrected.w*corrected.w);
            if (!std::isfinite(length) || length < 1e-5f)
                return;
            result.pose.orientation = {
                corrected.x/length, corrected.y/length,
                corrected.z/length, corrected.w/length};
            result.valid = true;
            // Persistent support grip: this exact pose consumed support
            // geometry only when the accepted two-hand path steered it AND
            // the assembly's qualification allowed it. A one-hand solve while
            // the relationship is engaged keeps the engaged epoch but never
            // claims support provenance (F14: never an ordinary epoch-0 ray).
            result.supportTrusted = result.twoHandActive &&
                inputs.supportMayConsume;
            if constexpr (CaptureTrace)
                trace->finalCalibrationValid = true;
        };

        if (!inputs.twoHandEnabled || !inputs.leftValid ||
            !inputs.twoHandLatched)
        {
            if constexpr (CaptureTrace)
            {
                trace->path = AimSolverPath::OneHand;
                trace->exactAEndpointSelected = true;
                // Two-Hand Lab one-hand observation (tranche 2B). Every
                // Lab-applicable frame publishes diagnostics
                // (stored-enabled AND resolved Virtual Stock OFF), including
                // one-hand solves, so the UI and telemetry show honest
                // status instead of a gap. No B was attempted: bValid is
                // false, resolved echoes requested, effective authority is
                // zero, and no pivots were consumed by the selection.
                if (inputs.twoHandLabEnabled && !inputs.virtualStockEnabled)
                {
                    const two_hand_lab::AnchorMode labOneHandRequested =
                        (inputs.twoHandLabAnchor >= 0 &&
                            inputs.twoHandLabAnchor <= 4)
                        ? static_cast<two_hand_lab::AnchorMode>(
                              inputs.twoHandLabAnchor)
                        : two_hand_lab::AnchorMode::Production;
                    const two_hand_lab::AgreementMode labOneHandAgreement =
                        inputs.twoHandLabAgreement == 1
                        ? two_hand_lab::AgreementMode::SoftAuthority
                        : two_hand_lab::AgreementMode::LegacyHard;
                    const two_hand_lab::TemporalMode labOneHandTemporal =
                        two_hand_lab::NormalizeTemporalMode(
                            inputs.twoHandLabTemporal);
                    const float labOneHandRequestedInfluence =
                        std::isfinite(inputs.twoHandLabOffhandInfluence)
                        ? std::clamp(inputs.twoHandLabOffhandInfluence,
                              0.0f, 1.0f)
                        : 0.0f;
                    trace->labValid = true;
                    trace->lab.labEnabledStored = true;
                    trace->lab.labActiveThisFrame = true;
                    trace->lab.requestedAnchor = labOneHandRequested;
                    trace->lab.resolvedAnchor =
                        two_hand_lab::ToResolvedAnchor(labOneHandRequested);
                    trace->lab.fallback =
                        two_hand_lab::FallbackReason::None;
                    trace->lab.primaryGripValid = inputs.primaryGripValid;
                    trace->lab.supportGripValid = inputs.supportGripValid;
                    trace->lab.bValid = false;
                    trace->lab.agreement = 0.0f;
                    trace->lab.agreementMode = labOneHandAgreement;
                    trace->lab.confidence =
                        labOneHandAgreement ==
                            two_hand_lab::AgreementMode::SoftAuthority
                        ? two_hand_lab::SoftAuthorityConfidence(0.0f,
                              inputs.twoHandLabSoftFullAgreement)
                        : 1.0f;
                    trace->lab.requestedInfluence =
                        labOneHandRequestedInfluence;
                    trace->lab.effectiveInfluence = 0.0f;
                    trace->lab.temporalMode = labOneHandTemporal;
                    trace->lab.temporalActive = false;
                    trace->lab.errorDeg = 0.0f;
                    trace->lab.pivotsValid = false;
                    trace->lab.primaryPivot = virtual_stock::Point3{};
                    trace->lab.supportPivot = virtual_stock::Point3{};
                }
            }
            finishAimPose();
            return result;
        }

        // Two-hand direction selection and orientation construction live in
        // the virtual_stock helper so unit tests exercise the production
        // maths. Legacy controller->controller behavior and thresholds are
        // unchanged. A valid positive-strength stock ray bypasses the
        // primary-forward agreement test; a degenerate stock ray falls back
        // to the legacy line. With Virtual Stock OFF the legacy line is the
        // fixed Grip -> Grip production pair (W2, selected further below);
        // with Virtual Stock ON it stays the primary AIM -> production support
        // endpoint pair. Base position stays the primary controller
        // throughout: only result.pose.orientation is reassigned below,
        // never result.pose.position.
        //
        // 2026-09-29 Two-Hand Smoothing scope: the prepared smoothed copies
        // feed this solve with Virtual Stock ON or OFF (one eligibility rule,
        // both stock modes; see vr.cpp). What stays VS-OFF only is the product
        // Grip -> Grip geometry and the Two-Handed Lab below - their own
        // `!inputs.virtualStockEnabled` gates are unchanged. The smoothed
        // support copy is still selected by the RAW-derived
        // `supportEndpointUsedGrip` correspondence, never re-derived from the
        // smoothed copies.
        const bool filteredGeometry = inputs.twoHandSmoothingGeometryValid &&
            inputs.twoHandEnabled && inputs.twoHandLatched && inputs.leftValid;
        const XrVector3f supportEndpoint = filteredGeometry
            ? (inputs.supportEndpointUsedGrip &&
                    inputs.twoHandSmoothedSupportGripValid
                ? inputs.twoHandSmoothedSupportGripPosition
                : inputs.twoHandSmoothedSupportAimPosition)
            : inputs.supportPosition;
        const XrQuaternionf rq = filteredGeometry
            ? inputs.twoHandSmoothedPrimaryOrientation
            : inputs.right.orientation;
        const XrVector3f rp = inputs.right.position;
        const XrVector3f solverPrimaryPosition = filteredGeometry
            ? inputs.twoHandSmoothedPrimaryAimPosition : rp;
        const XrVector3f rup = Rotate(rq, {0,1,0});
        const XrVector3f rawForward = Rotate(rq, {0,0,-1});
        XrVector3f solverForward = rawForward;
        const bool solverForwardSmoothed = filteredGeometry;
        const auto toPoint = [](const XrVector3f& v) {
            return virtual_stock::Point3{v.x, v.y, v.z};
        };
        if (inputs.virtualStockEnabled &&
            inputs.virtualStockRearReference == 3)
        {
            if constexpr (CaptureTrace)
                trace->path = AimSolverPath::Hybrid;
            const virtual_stock::Point3 primary = toPoint(solverPrimaryPosition);
            const virtual_stock::Point3 support = toPoint(supportEndpoint);
            const virtual_stock::Point3 primaryForward = toPoint(solverForward);
            virtual_stock::Point3 offhandDirection{};
            bool rejectedExtreme = false;
            float rejectedAgreement = 0.0f;
            const bool offhandAccepted =
                virtual_stock::TryBuildAcceptedSupportDirection(
                    primary, support, primaryForward, offhandDirection,
                    rejectedExtreme, rejectedAgreement);
            if constexpr (CaptureTrace)
            {
                trace->bAttempted = true;
                trace->bAccepted = offhandAccepted;
                trace->bDirection = traceVec(offhandDirection);
                const float agreement = virtual_stock::Dot(
                    offhandDirection, primaryForward);
                trace->bAgreement = std::isfinite(agreement)
                    ? agreement : 0.0f;
                trace->bExtremeRejected = rejectedExtreme;
                trace->bRejectedAgreement = rejectedAgreement;
            }

            const float offhandInfluence =
                std::isfinite(inputs.virtualStockHybridOffhandInfluence)
                    ? std::clamp(inputs.virtualStockHybridOffhandInfluence,
                                 0.0f, 1.0f)
                    : 0.0f;
            if constexpr (CaptureTrace)
                trace->offhandInfluenceUsed = offhandInfluence;
            virtual_stock::Point3 hipDirection = primaryForward;
            bool hipUsable = virtual_stock::Finite(hipDirection);
            bool hipNeedsOrientation = false;
            if (offhandAccepted)
            {
                virtual_stock::Point3 blendedHip{};
                const bool blended =
                    virtual_stock::TryBlendDirectionAuthority(
                        primaryForward, offhandDirection, offhandInfluence,
                        blendedHip);
                if constexpr (CaptureTrace)
                {
                    trace->hipBlendAttempted = true;
                    trace->hipBlendSucceeded = blended;
                }
                if (blended)
                {
                    hipDirection = blendedHip;
                    hipNeedsOrientation = offhandInfluence > 0.0f;
                }
                // A failed intermediate blend falls back to the exact primary
                // direction; it never manufactures another authority vector.
                else
                {
                    hipDirection = primaryForward;
                    hipNeedsOrientation = false;
                }
                hipUsable = virtual_stock::Finite(hipDirection);
            }
            if constexpr (CaptureTrace)
            {
                trace->hipAimValid = hipUsable;
                trace->hipAimDirection = traceVec(hipDirection);
            }

            virtual_stock::Point3 rawHeadTarget{};
            const bool rawHeadTargetValid = inputs.headValid &&
                virtual_stock::BuildVirtualStockRearTarget(
                    toPoint(inputs.headPosition),
                    inputs.virtualStockRearHeightM, rawHeadTarget);
            virtual_stock::HybridInverseNeckEvaluation inverseNeck{};
            // Diagnostic inverse-neck correction only: the product sway
            // correction was retired on 2026-09-27 and is reachable solely
            // through explicit diagnostic profiles. It applies to both Plus
            // Centre and Plus Shoulder; only the rear-reference geometry
            // differs. Horizontal release below stays on raw HMD XZ regardless.
            const bool inverseNeckAttempted =
                inputs.hybridInverseNeckEnabled;
            if (inverseNeckAttempted && inputs.headValid &&
                inputs.inverseNeckNeutralValid)
            {
                inverseNeck = virtual_stock::EvaluateHybridInverseNeck(
                    toPoint(inputs.headPosition),
                    {inputs.headOrientation.x, inputs.headOrientation.y,
                     inputs.headOrientation.z, inputs.headOrientation.w},
                    {inputs.inverseNeckNeutralOrientation.x,
                     inputs.inverseNeckNeutralOrientation.y,
                     inputs.inverseNeckNeutralOrientation.z,
                     inputs.inverseNeckNeutralOrientation.w},
                    inputs.hybridInverseNeckStrength,
                    inputs.hybridInverseNeckForwardM,
                    inputs.hybridInverseNeckUpM,
                    inputs.hybridInverseNeckLateralM);
            }
            virtual_stock::Point3 headTarget = rawHeadTarget;
            bool headTargetValid = rawHeadTargetValid;
            if (inverseNeck.valid)
            {
                headTargetValid = virtual_stock::BuildVirtualStockRearTarget(
                    inverseNeck.correctedHead,
                    inputs.virtualStockRearHeightM, headTarget);
            }
            virtual_stock::Point3 adsTarget = headTarget;
            bool adsTargetValid = headTargetValid;
            bool shoulderTargetUsed = false;
            if constexpr (CaptureTrace)
            {
                trace->inverseNeckAttempted = inverseNeckAttempted;
                trace->inverseNeckValid = inverseNeck.valid;
                if (inverseNeck.valid)
                {
                    trace->inverseNeckCurrentOffset =
                        traceVec(inverseNeck.currentOffset);
                    trace->inverseNeckNeutralOffset =
                        traceVec(inverseNeck.neutralOffset);
                    trace->inverseNeckPredictedOrbit =
                        traceVec(inverseNeck.predictedOrbit);
                    trace->inverseNeckCorrectionClamped =
                        inverseNeck.correctionClamped;
                    trace->inverseNeckAppliedCorrection =
                        traceVec(inverseNeck.appliedCorrection);
                    trace->inverseNeckCorrectedHeadPosition =
                        traceVec(inverseNeck.correctedHead);
                }
                trace->rawHeadTargetValid = rawHeadTargetValid;
                if (rawHeadTargetValid)
                    trace->rawHeadTarget = traceVec(rawHeadTarget);
                trace->correctedHeadTargetValid = headTargetValid;
                if (headTargetValid)
                    trace->correctedHeadTarget = traceVec(headTarget);
                trace->requestedTarget =
                    inputs.virtualStockHybridAdsReference == 1
                        ? AimStockTarget::Shoulder : AimStockTarget::Head;
            }
            if (adsTargetValid && inputs.virtualStockHybridAdsReference == 1)
            {
                // Shoulder corrects the position origin only; the horizontal
                // back/side basis stays on the current HMD yaw orientation.
                // Q0 orientation is never used for forward/right here.
                const virtual_stock::Point3 shoulderBase =
                    inverseNeck.valid ? inverseNeck.correctedHead
                                      : toPoint(inputs.headPosition);
                virtual_stock::Point3 shoulderTarget{};
                if (virtual_stock::TryBuildHmdRelativeShoulderRearTarget(
                        shoulderBase,
                        {inputs.headOrientation.x, inputs.headOrientation.y,
                         inputs.headOrientation.z, inputs.headOrientation.w},
                        inputs.virtualStockRearHeightM,
                        inputs.virtualStockShoulderBackM,
                        inputs.virtualStockShoulderSideM,
                        inputs.virtualStockLeftHanded, shoulderTarget))
                {
                    adsTarget = shoulderTarget;
                    shoulderTargetUsed = true;
                }
            }
            if constexpr (CaptureTrace)
            {
                trace->actualTarget = adsTargetValid
                    ? (shoulderTargetUsed
                        ? AimStockTarget::Shoulder : AimStockTarget::Head)
                    : AimStockTarget::None;
                trace->shoulderToHeadFallback = adsTargetValid &&
                    inputs.virtualStockHybridAdsReference == 1 &&
                    !shoulderTargetUsed;
                trace->targetValid = adsTargetValid;
                if (adsTargetValid)
                    trace->target = traceVec(adsTarget);
            }

            virtual_stock::HybridStockEvaluation stockEvaluation{};
            virtual_stock::Point3 stockDirection{};
            const bool stockAvailable = adsTargetValid &&
                virtual_stock::TryBuildHybridStockDirection(
                    primary, adsTarget, support,
                    inputs.virtualStockStrength, stockDirection,
                    CaptureTrace ? &stockEvaluation : nullptr);
            virtual_stock::HybridSeatEvaluation seatEvaluation{};
            const float naturalSeatInfluence = stockAvailable
                ? virtual_stock::ComputeHybridSeatInfluence(
                    primary, adsTarget, support,
                    inputs.virtualStockHybridSeatFullM,
                    inputs.virtualStockHybridSeatReleaseM,
                    CaptureTrace ? &seatEvaluation : nullptr)
                : 0.0f;
            if constexpr (CaptureTrace)
            {
                trace->cAttempted = adsTargetValid;
                trace->cValid = stockAvailable;
                trace->virtualRearValid = stockEvaluation.virtualRearValid;
                if (stockEvaluation.virtualRearValid)
                    trace->virtualRear = traceVec(stockEvaluation.virtualRear);
                if (stockAvailable)
                    trace->cDirection = traceVec(stockDirection);
                trace->stockStrengthUsed = inputs.virtualStockStrength;
                trace->seatAttempted = stockAvailable;
                trace->seatValid = seatEvaluation.valid;
                trace->seatSegmentLength = seatEvaluation.segmentLength;
                trace->seatRawProjection = seatEvaluation.rawProjection;
                trace->seatClampedProjection =
                    seatEvaluation.clampedProjection;
                trace->seatClosestValid = seatEvaluation.valid;
                if (seatEvaluation.valid)
                    trace->seatClosest = traceVec(seatEvaluation.closest);
                trace->seatErrorM = seatEvaluation.seatError;
                trace->wNaturalValid = seatEvaluation.valid;
                trace->wNatural = naturalSeatInfluence;

                float rearDistanceSquared = 0.0f;
                float primarySupportDistanceSquared = 0.0f;
                float targetSupportDistanceSquared = 0.0f;
                if (adsTargetValid &&
                    virtual_stock::TryDistanceSquared(
                        primary, adsTarget, rearDistanceSquared) &&
                    virtual_stock::TryDistanceSquared(
                        primary, support, primarySupportDistanceSquared) &&
                    virtual_stock::TryDistanceSquared(
                        adsTarget, support, targetSupportDistanceSquared))
                {
                    const float rearDistance = std::sqrt(rearDistanceSquared);
                    const float primarySupportDistance =
                        std::sqrt(primarySupportDistanceSquared);
                    const float targetSupportDistance =
                        std::sqrt(targetSupportDistanceSquared);
                    if (std::isfinite(rearDistance) &&
                        std::isfinite(primarySupportDistance) &&
                        std::isfinite(targetSupportDistance))
                    {
                        trace->releaseGeometryValid = true;
                        trace->rearToStockTargetDistanceM = rearDistance;
                        trace->primaryToSupportDistanceM =
                            primarySupportDistance;
                        trace->stockToSupportDistanceM =
                            targetSupportDistance;
                    }
                }
            }
            float afterDiagnosticInfluence = naturalSeatInfluence;
            const HybridDiagnosticOverride diagnosticOverride =
                NormalizeHybridDiagnosticOverride(
                    static_cast<uint8_t>(inputs.hybridDiagnosticOverride));
            bool afterDiagnosticInfluenceValid =
                seatEvaluation.valid || !CaptureTrace;
            if (diagnosticOverride == HybridDiagnosticOverride::ForceHip &&
                stockAvailable)
            {
                afterDiagnosticInfluence = 0.0f;
                afterDiagnosticInfluenceValid = true;
            }
            else if (diagnosticOverride == HybridDiagnosticOverride::ForceStock &&
                stockAvailable)
            {
                afterDiagnosticInfluence = 1.0f;
                afterDiagnosticInfluenceValid = true;
            }

            virtual_stock::HybridHorizontalReleaseEvaluation
                horizontalReleaseEvaluation{};
            float effectiveSeatInfluence = afterDiagnosticInfluence;
            bool effectiveSeatInfluenceValid = afterDiagnosticInfluenceValid;
            if (inputs.hybridHorizontalRearReleaseEnabled)
            {
                const float horizontalInfluence =
                    virtual_stock::ComputeHybridHorizontalRearReleaseInfluence(
                        primary, inputs.headValid,
                        toPoint(inputs.headPosition),
                        inputs.hybridHorizontalRearReleaseFullM,
                        inputs.hybridHorizontalRearReleaseReleaseM,
                        &horizontalReleaseEvaluation);
                effectiveSeatInfluence = horizontalReleaseEvaluation.valid
                    ? afterDiagnosticInfluence * horizontalInfluence : 0.0f;
                effectiveSeatInfluenceValid = afterDiagnosticInfluenceValid &&
                    horizontalReleaseEvaluation.valid &&
                    std::isfinite(effectiveSeatInfluence);
                if (!effectiveSeatInfluenceValid)
                    effectiveSeatInfluence = 0.0f;
            }
            if constexpr (CaptureTrace)
            {
                trace->effectiveDiagnosticOverride =
                    static_cast<uint8_t>(diagnosticOverride);
                trace->wAfterDiagnosticValid = stockAvailable &&
                    afterDiagnosticInfluenceValid;
                trace->wAfterDiagnostic = afterDiagnosticInfluence;
                trace->horizontalReleaseAttempted =
                    inputs.hybridHorizontalRearReleaseEnabled;
                trace->horizontalReleaseValid =
                    horizontalReleaseEvaluation.valid;
                trace->rearHorizontalReachM =
                    horizontalReleaseEvaluation.horizontalReach;
                trace->horizontalReleaseInfluence =
                    horizontalReleaseEvaluation.influence;
                trace->wEffectiveValid = stockAvailable &&
                    effectiveSeatInfluenceValid;
                trace->wEffective = effectiveSeatInfluence;
                trace->overrideApplied = stockAvailable &&
                    diagnosticOverride != HybridDiagnosticOverride::Normal;
                trace->forceStockEligible = stockAvailable;
            }

            const bool horizontalReleaseEnabled =
                inputs.hybridHorizontalRearReleaseEnabled;
            virtual_stock::Point3 finalDirection = horizontalReleaseEnabled
                ? (offhandAccepted ? offhandDirection : primaryForward)
                : hipDirection;
            const bool baseNeedsOrientation = horizontalReleaseEnabled
                ? offhandAccepted
                : (offhandAccepted && hipNeedsOrientation);
            const bool baseUsable = horizontalReleaseEnabled
                ? virtual_stock::Finite(finalDirection) : hipUsable;
            bool stockContributed = false;
            if (stockAvailable && effectiveSeatInfluence > 0.0f)
            {
                if constexpr (CaptureTrace)
                    trace->stockBlendAttempted = true;
                if (horizontalReleaseEnabled)
                {
                    virtual_stock::Point3 composed{};
                    if (baseUsable &&
                        virtual_stock::TryBlendDirectionAuthority(
                            finalDirection, stockDirection,
                            effectiveSeatInfluence, composed))
                    {
                        finalDirection = composed;
                        stockContributed = true;
                        if constexpr (CaptureTrace)
                            trace->stockBlendSucceeded = true;
                    }
                }
                else if (diagnosticOverride ==
                    HybridDiagnosticOverride::ForceStock)
                {
                    finalDirection = stockDirection;
                    stockContributed = true;
                    if constexpr (CaptureTrace)
                        trace->stockBlendSucceeded = true;
                }
                else if (hipUsable)
                {
                    virtual_stock::Point3 composed{};
                    if (virtual_stock::TryBlendDirectionAuthority(
                            hipDirection, stockDirection, effectiveSeatInfluence,
                            composed))
                    {
                        finalDirection = composed;
                        stockContributed = true;
                        if constexpr (CaptureTrace)
                            trace->stockBlendSucceeded = true;
                    }
                    // A failed HipAim->C blend falls back to HipAim, never to C.
                }
            }

            const bool requiresOrientation = stockContributed ||
                baseNeedsOrientation;
            bool orientationBuilt = true;
            if (requiresOrientation)
            {
                if constexpr (CaptureTrace)
                    trace->orientationRebuildAttempted = true;
                virtual_stock::Quat4 aimOrientation{};
                orientationBuilt = baseUsable &&
                    virtual_stock::Finite(finalDirection) &&
                    virtual_stock::BuildTwoHandAimOrientation(
                        finalDirection, toPoint(rup), aimOrientation);
                if (orientationBuilt)
                {
                    result.pose.orientation = {
                        aimOrientation.x, aimOrientation.y,
                        aimOrientation.z, aimOrientation.w};
                    if constexpr (CaptureTrace)
                        trace->orientationRebuildSucceeded = true;
                }
            }

            if (!orientationBuilt)
            {
                result.twoHandActive = false;
            }
            else
            {
                // Accepted B owns presentation even when it contributes zero
                // numerical authority. C alone can activate presentation only
                // after its final orientation has been built successfully.
                result.twoHandActive = offhandAccepted || stockContributed;
            }
            if (!offhandAccepted && rejectedExtreme &&
                !result.twoHandActive)
            {
                result.rejectedExtreme = true;
                result.rejectedAgreement = rejectedAgreement;
            }
            if constexpr (CaptureTrace)
            {
                trace->stockContributed = stockContributed && orientationBuilt;
                trace->exactAEndpointSelected =
                    !orientationBuilt ||
                    (!stockContributed && !baseNeedsOrientation);
                const virtual_stock::Point3 appliedDirection =
                    orientationBuilt ? finalDirection : primaryForward;
                trace->finalDirectionValid =
                    virtual_stock::Finite(appliedDirection);
                if (trace->finalDirectionValid)
                    trace->finalDirection = traceVec(appliedDirection);
            }
            finishAimPose();
            return result;
        }
        // Two-Hand Lab single-source pivots (tranche 2A). The legacy/VS-OFF
        // selection below and its trace-only re-derivation must consume the
        // SAME (D = primary, S = support) pair, and the Lab diagnostics must
        // describe that pair: no duplicated anchor-selection equations.
        // Lab-inactive Virtual Stock solves keep today's exact (primary AIM,
        // production support endpoint) values, so every Virtual Stock path
        // below is behaviorally unchanged; Lab-inactive VS-OFF solves consume
        // the fixed Grip -> Grip product pair selected just below (W2), and
        // Lab-active solves stay the rig's own anchor. Production anchor passes
        // the existing production-selected endpoint through; it never emulates
        // it.
        // Ordinal mapping is inline (not via the vr.cpp store helpers) so
        // this extracted solver body stays self-contained for the offline
        // fixtures: AnchorMode/AgreementMode/TemporalMode ordinals match
        // declaration order (Production/LegacyHard/None = 0), out of range
        // fails safe to the current behavior in each dimension.
        const bool labActive = inputs.twoHandLabEnabled &&
            !inputs.virtualStockEnabled;
        const two_hand_lab::AnchorMode labRequestedAnchor =
            (inputs.twoHandLabAnchor >= 0 && inputs.twoHandLabAnchor <= 4)
            ? static_cast<two_hand_lab::AnchorMode>(inputs.twoHandLabAnchor)
            : two_hand_lab::AnchorMode::Production;
        const two_hand_lab::AgreementMode labAgreementMode =
            inputs.twoHandLabAgreement == 1
            ? two_hand_lab::AgreementMode::SoftAuthority
            : two_hand_lab::AgreementMode::LegacyHard;
        const two_hand_lab::TemporalMode labTemporalMode =
            two_hand_lab::NormalizeTemporalMode(inputs.twoHandLabTemporal);
        two_hand_lab::ResolvedPivots labPivots{};
        if (labActive)
        {
            two_hand_lab::PivotInputs labPivotInputs{};
            labPivotInputs.primaryAim = toPoint(solverPrimaryPosition);
            labPivotInputs.supportAim = toPoint(filteredGeometry
                ? inputs.twoHandSmoothedSupportAimPosition
                : inputs.left.position);
            labPivotInputs.primaryGripValid = inputs.primaryGripValid;
            labPivotInputs.primaryGrip = toPoint(filteredGeometry &&
                inputs.twoHandSmoothedPrimaryGripValid
                ? inputs.twoHandSmoothedPrimaryGripPosition
                : inputs.primaryGripPosition);
            labPivotInputs.supportGripValid = inputs.supportGripValid;
            labPivotInputs.supportGrip = toPoint(filteredGeometry &&
                inputs.twoHandSmoothedSupportGripValid
                ? inputs.twoHandSmoothedSupportGripPosition
                : inputs.supportGripPosition);
            labPivotInputs.productionSupportUsedGrip =
                inputs.supportEndpointUsedGrip;
            labPivotInputs.productionSupportPosition =
                toPoint(supportEndpoint);
            labPivots = two_hand_lab::ResolvePivots(
                labPivotInputs, labRequestedAnchor);
        }
        // Free two-hand production geometry (W2): with Virtual Stock OFF and
        // the two-hand hold latched, the positional B line is the primary Grip
        // position -> support Grip position pair. `SelectTwoHandGripEndpoints`
        // owns the rule (both grips committed and finite, otherwise the aim
        // pair), so this product path never consults "Reduce Support-Hand
        // Rotation" and never needs the Lab: the Lab, when explicitly enabled,
        // keeps owning its own anchor above (that is why this is gated on
        // !labActive). Positional pivots only - the orientation/roll baseline
        // stays the primary Aim observation (rq/rup) and the weapon/base
        // position stays the raw primary position (rp). When this solve carries
        // prepared-serial smoothed input copies, the grip pair comes from those
        // same smoothed copies (never a mix of smoothed and raw endpoints).
        const bool productGripGeometry =
            !inputs.virtualStockEnabled && !labActive;
        const bool smoothedGrips = filteredGeometry;
        const bool productPrimaryGripValid = smoothedGrips
            ? inputs.twoHandSmoothedPrimaryGripValid : inputs.primaryGripValid;
        const bool productSupportGripValid = smoothedGrips
            ? inputs.twoHandSmoothedSupportGripValid : inputs.supportGripValid;
        const virtual_stock::Point3 productPrimaryGrip = toPoint(smoothedGrips
            ? inputs.twoHandSmoothedPrimaryGripPosition
            : inputs.primaryGripPosition);
        const virtual_stock::Point3 productSupportGrip = toPoint(smoothedGrips
            ? inputs.twoHandSmoothedSupportGripPosition
            : inputs.supportGripPosition);
        const virtual_stock::Point3 productSupportAim = toPoint(
            filteredGeometry ? inputs.twoHandSmoothedSupportAimPosition
                             : inputs.left.position);
        virtual_stock::TwoHandGripEndpoints productEndpoints{};
        if (productGripGeometry)
        {
            productEndpoints = virtual_stock::SelectTwoHandGripEndpoints(
                toPoint(solverPrimaryPosition), productSupportAim,
                productPrimaryGripValid, productPrimaryGrip,
                productSupportGripValid, productSupportGrip);
        }
        // An invalid resolve (non-finite pivots) falls back to today's pair,
        // which fails the acceptance test exactly as today: never a silent
        // downgrade, never a new acceptance. The production pair above keeps
        // the same shape by construction: an unusable pair leaves the exact
        // pre-GG aim-endpoint pair selected.
        const virtual_stock::Point3 legacyPrimary =
            (labActive && labPivots.valid) ? labPivots.primary
            : ((productGripGeometry && productEndpoints.valid)
                ? productEndpoints.primary : toPoint(solverPrimaryPosition));
        const virtual_stock::Point3 legacySupport =
            (labActive && labPivots.valid) ? labPivots.support
            : ((productGripGeometry && productEndpoints.valid)
                ? productEndpoints.support : toPoint(supportEndpoint));
        // Lab trace fill shared by the rejection and accepted paths below.
        // No-op unless the Lab is active for this solve; reads the trace's
        // own B re-derivation for agreement rather than recomputing it.
        const auto fillLabTrace = [&](bool bValid, float agreement,
                                      float confidence, float requested,
                                      float effective) {
            if constexpr (CaptureTrace)
            {
                if (!labActive)
                    return;
                trace->labValid = true;
                trace->lab.labEnabledStored = inputs.twoHandLabEnabled;
                trace->lab.labActiveThisFrame = true;
                trace->lab.requestedAnchor = labRequestedAnchor;
                trace->lab.resolvedAnchor = labPivots.resolved;
                trace->lab.fallback = labPivots.fallbackReason;
                trace->lab.primaryGripValid = inputs.primaryGripValid;
                trace->lab.supportGripValid = inputs.supportGripValid;
                trace->lab.bValid = bValid;
                trace->lab.agreement = agreement;
                trace->lab.agreementMode = labAgreementMode;
                trace->lab.confidence = confidence;
                trace->lab.requestedInfluence = requested;
                trace->lab.effectiveInfluence = effective;
                trace->lab.temporalMode = labTemporalMode;
                trace->lab.temporalActive = false;
                trace->lab.errorDeg = 0.0f;
                // Selected pivots: the same (D, S) pair the legacy/VS-OFF
                // selection and its trace re-derivation consumed above. An
                // invalid resolve (non-finite pivots) reports invalid with
                // zeroed positions, never stale geometry.
                trace->lab.pivotsValid = labPivots.valid;
                trace->lab.primaryPivot = labPivots.valid
                    ? legacyPrimary : virtual_stock::Point3{};
                trace->lab.supportPivot = labPivots.valid
                    ? legacySupport : virtual_stock::Point3{};
            }
        };
        float stockStrength = 0.0f;
        virtual_stock::DirectionSelection selection{};
        bool shoulderTargetConstructed = false;
        bool chestTargetConstructed = false;
        bool adaptiveTargetSelected = false;
        virtual_stock::Point3 adaptiveTarget{};
        const bool shoulderRequested = inputs.virtualStockEnabled &&
            inputs.virtualStockRearReference == 1 && inputs.headValid;
        const bool chestRequested = inputs.virtualStockEnabled &&
            inputs.virtualStockRearReference == 2 && inputs.headValid;
        const bool adaptiveRequested = inputs.virtualStockEnabled &&
            inputs.virtualStockRearReference == 3 && inputs.headValid;
        // Diagnostic inverse-neck correction for Standard Centre/Shoulder only:
        // the product sway correction was retired on 2026-09-27 and is
        // reachable solely through explicit diagnostic profiles. Corrects the
        // positional base only; Shoulder keeps the current HMD yaw basis.
        // Dormant Chest/Adaptive never evaluate or consume correction.
        // Failure falls back to raw HMD and never drops stock.
        virtual_stock::HybridInverseNeckEvaluation fixedInverseNeck{};
        const bool productFixedReference =
            inputs.virtualStockRearReference == 0 ||
            inputs.virtualStockRearReference == 1;
        const bool fixedInverseNeckAttempted =
            inputs.virtualStockEnabled && productFixedReference &&
            inputs.hybridInverseNeckEnabled &&
            inputs.headValid && inputs.inverseNeckNeutralValid;
        if (fixedInverseNeckAttempted)
        {
            fixedInverseNeck = virtual_stock::EvaluateHybridInverseNeck(
                toPoint(inputs.headPosition),
                {inputs.headOrientation.x, inputs.headOrientation.y,
                 inputs.headOrientation.z, inputs.headOrientation.w},
                {inputs.inverseNeckNeutralOrientation.x,
                 inputs.inverseNeckNeutralOrientation.y,
                 inputs.inverseNeckNeutralOrientation.z,
                 inputs.inverseNeckNeutralOrientation.w},
                inputs.hybridInverseNeckStrength,
                inputs.hybridInverseNeckForwardM, inputs.hybridInverseNeckUpM,
                inputs.hybridInverseNeckLateralM);
        }
        const virtual_stock::Point3 fixedHeadBase =
            fixedInverseNeck.valid ? fixedInverseNeck.correctedHead
                                   : toPoint(inputs.headPosition);
        if constexpr (CaptureTrace)
        {
            trace->inverseNeckAttempted =
                trace->inverseNeckAttempted || fixedInverseNeckAttempted;
            if (fixedInverseNeckAttempted && fixedInverseNeck.valid)
            {
                trace->inverseNeckValid = true;
                trace->inverseNeckCurrentOffset =
                    traceVec(fixedInverseNeck.currentOffset);
                trace->inverseNeckNeutralOffset =
                    traceVec(fixedInverseNeck.neutralOffset);
                trace->inverseNeckPredictedOrbit =
                    traceVec(fixedInverseNeck.predictedOrbit);
                trace->inverseNeckCorrectionClamped =
                    fixedInverseNeck.correctionClamped;
                trace->inverseNeckAppliedCorrection =
                    traceVec(fixedInverseNeck.appliedCorrection);
                trace->inverseNeckCorrectedHeadPosition =
                    traceVec(fixedInverseNeck.correctedHead);
            }
            trace->effectiveDiagnosticOverride = static_cast<uint8_t>(
                NormalizeHybridDiagnosticOverride(
                    static_cast<uint8_t>(inputs.hybridDiagnosticOverride)));
            trace->fixedConfiguredStrength = inputs.virtualStockStrength;
            trace->fixedProximityEnabled =
                inputs.virtualStockProximityRelease;
            if (inputs.virtualStockEnabled &&
                inputs.virtualStockRearReference >= 0 &&
                inputs.virtualStockRearReference <= 2)
            {
                trace->requestedTarget = inputs.virtualStockRearReference == 1
                    ? AimStockTarget::Shoulder
                    : inputs.virtualStockRearReference == 2
                        ? AimStockTarget::Chest
                        : AimStockTarget::Head;
            }
        }
        const auto recordFixedTarget = [&](virtual_stock::Point3 target,
                                            bool valid,
                                            float effectiveStrength,
                                            AimStockTarget actualTarget) {
            if constexpr (CaptureTrace)
            {
                trace->actualTarget = valid
                    ? actualTarget : AimStockTarget::None;
                trace->targetValid = valid;
                trace->fixedTargetValid = valid;
                trace->fixedEffectiveStrength = effectiveStrength;
                if (!valid)
                    return;
                trace->target = traceVec(target);
                trace->fixedTarget = traceVec(target);
                float distanceSquared = 0.0f;
                if (virtual_stock::TryDistanceSquared(
                        toPoint(rp), target, distanceSquared))
                {
                    const float distance = std::sqrt(distanceSquared);
                    if (std::isfinite(distance))
                    {
                        trace->fixedRearDistanceValid = true;
                        trace->fixedRearToTargetDistanceM = distance;
                        if (inputs.virtualStockProximityRelease)
                        {
                            trace->fixedProximityCalculated = true;
                            trace->fixedProximityInfluence =
                                virtual_stock::ComputeProximityInfluence(
                                    distance,
                                    inputs.virtualStockProximityFullM,
                                    inputs.virtualStockProximityReleaseM);
                        }
                    }
                }
            }
        };
        if (shoulderRequested)
        {
            virtual_stock::Point3 shoulderRear{};
            const bool shoulderTargetValid =
                virtual_stock::TryBuildHmdRelativeShoulderRearTarget(
                    fixedHeadBase,
                    {inputs.headOrientation.x, inputs.headOrientation.y,
                     inputs.headOrientation.z, inputs.headOrientation.w},
                    inputs.virtualStockRearHeightM,
                    inputs.virtualStockShoulderBackM,
                    inputs.virtualStockShoulderSideM,
                    inputs.virtualStockLeftHanded, shoulderRear);
            if (shoulderTargetValid)
            {
                shoulderTargetConstructed = true;
                stockStrength =
                    virtual_stock::ApplyVirtualStockProximityReleaseForTarget(
                        inputs.virtualStockProximityRelease,
                        inputs.virtualStockStrength, toPoint(rp), true,
                        shoulderRear, inputs.virtualStockProximityFullM,
                        inputs.virtualStockProximityReleaseM);
                selection = virtual_stock::SelectTwoHandAimDirectionForTarget(
                    inputs.virtualStockEnabled, stockStrength, true,
                        shoulderRear, toPoint(supportEndpoint),
                        toPoint(solverPrimaryPosition),
                    toPoint(supportEndpoint), toPoint(solverForward));
                if constexpr (CaptureTrace)
                {
                    recordFixedTarget(
                        shoulderRear, true, stockStrength,
                        AimStockTarget::Shoulder);
                }
            }
        }
        else if (chestRequested)
        {
            virtual_stock::Point3 chestRear{};
            const bool chestTargetValid =
                virtual_stock::TryBuildHmdRelativeChestRearTarget(
                    toPoint(inputs.headPosition),
                    {inputs.headOrientation.x, inputs.headOrientation.y,
                     inputs.headOrientation.z, inputs.headOrientation.w},
                    inputs.virtualStockChestHeightM,
                    inputs.virtualStockChestBackM,
                    inputs.virtualStockChestSideM,
                    inputs.virtualStockLeftHanded, chestRear);
            if (chestTargetValid)
            {
                chestTargetConstructed = true;
                stockStrength =
                    virtual_stock::ApplyVirtualStockProximityReleaseForTarget(
                        inputs.virtualStockProximityRelease,
                        inputs.virtualStockStrength, toPoint(rp), true,
                        chestRear, inputs.virtualStockProximityFullM,
                        inputs.virtualStockProximityReleaseM);
                selection = virtual_stock::SelectTwoHandAimDirectionForTarget(
                    inputs.virtualStockEnabled, stockStrength, true,
                        chestRear, toPoint(supportEndpoint),
                        toPoint(solverPrimaryPosition),
                    toPoint(supportEndpoint), toPoint(solverForward));
                if constexpr (CaptureTrace)
                {
                    recordFixedTarget(
                        chestRear, true, stockStrength,
                        AimStockTarget::Chest);
                }
            }
        }
        else if (adaptiveRequested)
        {
            virtual_stock::AdaptiveStockPatch adaptivePatch{};
            const bool patchValid =
                virtual_stock::TryBuildAdaptiveStockPatch(
                    toPoint(inputs.headPosition),
                    {inputs.headOrientation.x, inputs.headOrientation.y,
                     inputs.headOrientation.z, inputs.headOrientation.w},
                    inputs.virtualStockAdaptiveTopHeightM,
                    inputs.virtualStockAdaptiveBottomHeightM,
                    inputs.virtualStockAdaptiveTopHalfWidthM,
                    inputs.virtualStockAdaptiveBottomHalfWidthM,
                    adaptivePatch);
            adaptiveTargetSelected = patchValid &&
                virtual_stock::TrySelectAdaptiveRearTarget(
                    toPoint(rp), adaptivePatch, adaptiveTarget);
            if (adaptiveTargetSelected)
            {
                stockStrength =
                    virtual_stock::ApplyVirtualStockProximityReleaseForTarget(
                        inputs.virtualStockProximityRelease,
                        inputs.virtualStockStrength, toPoint(rp), true,
                        adaptiveTarget, inputs.virtualStockProximityFullM,
                        inputs.virtualStockProximityReleaseM);
                selection = virtual_stock::SelectTwoHandAimDirectionForTarget(
                    inputs.virtualStockEnabled, stockStrength, true,
                        adaptiveTarget, toPoint(supportEndpoint),
                        toPoint(solverPrimaryPosition),
                    toPoint(supportEndpoint), toPoint(solverForward));
            }
        }
        const bool useHeadPath =
            (!shoulderRequested && !chestRequested && !adaptiveRequested) ||
            (shoulderRequested && virtual_stock::ShouldFallbackToHeadRearTarget(
                shoulderRequested, shoulderTargetConstructed)) ||
            (chestRequested && virtual_stock::ShouldFallbackToHeadRearTarget(
                chestRequested, chestTargetConstructed)) ||
            (adaptiveRequested && !adaptiveTargetSelected);
        if (useHeadPath)
        {
            // This is the released Head path, including its existing fallback
            // behavior when the optional Shoulder basis is unavailable.
            // The product sway correction that used to supply this positional
            // base was retired 2026-09-27; raw-H fallback is already selected
            // in fixedHeadBase.
            stockStrength = virtual_stock::ApplyVirtualStockProximityRelease(
                inputs.virtualStockProximityRelease,
                inputs.virtualStockStrength, toPoint(rp), inputs.headValid,
                fixedHeadBase, inputs.virtualStockRearHeightM,
                inputs.virtualStockProximityFullM,
                inputs.virtualStockProximityReleaseM);
            selection = virtual_stock::SelectTwoHandAimDirection(
                inputs.virtualStockEnabled, stockStrength,
                inputs.virtualStockRearHeightM, inputs.headValid,
                fixedHeadBase, toPoint(supportEndpoint),
                legacyPrimary, legacySupport, toPoint(solverForward),
                // T13 corrective: the engaged+trusted qualification may retain
                // support steering past the legacy agreement floor, and only
                // on the VS-off legacy product path. A VS-on invocation keeps
                // every existing fallback (its stock ray already bypasses the
                // floor), so this argument can never alter VS-on behavior.
                inputs.supportSteeringRetained && !inputs.virtualStockEnabled);
            if constexpr (CaptureTrace)
            {
                virtual_stock::Point3 headTarget{};
                const bool headTargetValid = inputs.virtualStockEnabled &&
                    inputs.headValid &&
                    virtual_stock::BuildVirtualStockRearTarget(
                        fixedHeadBase,
                        inputs.virtualStockRearHeightM, headTarget);
                trace->shoulderToHeadFallback = shoulderRequested &&
                    !shoulderTargetConstructed;
                recordFixedTarget(
                    headTarget, headTargetValid, stockStrength,
                    AimStockTarget::Head);
            }
        }
        if constexpr (CaptureTrace)
        {
            trace->path = selection.usedVirtualStock
                ? AimSolverPath::FixedStock
                : AimSolverPath::LegacyTwoHand;
            trace->fixedDirectionValid =
                selection.valid && selection.usedVirtualStock;
            if (!selection.usedVirtualStock)
            {
                virtual_stock::Point3 bDirection{};
                bool rejectedExtreme = false;
                float rejectedAgreement = 0.0f;
                trace->bAttempted = true;
                // T13 corrective: the re-derivation applies the SAME retention
                // policy the selection just used (same qualification bit, same
                // VS-off-only gate), so the trace can never describe a
                // different rule than the solver ran.
                const bool bAccepted =
                    virtual_stock::TryBuildAcceptedSupportDirection(
                        legacyPrimary, legacySupport,
                        toPoint(solverForward), bDirection,
                        rejectedExtreme, rejectedAgreement,
                        inputs.supportSteeringRetained &&
                            !inputs.virtualStockEnabled);
                trace->bAccepted = bAccepted;
                // Honest pairing: `bExtremeRejected` keeps its established
                // meaning ("this attempt was rejected by the 0.35 floor", i.e.
                // it was NOT accepted), and the new `bSteeringRetained`
                // records the retained acceptance together with the floor
                // agreement the attempt crossed. `bAgreement` always carries
                // the true dot product, so a retained frame is visible as
                // b_accepted=true + b_steering_retained=true + a_dot_b<0.35.
                trace->bSteeringRetained = bAccepted && rejectedExtreme;
                trace->bDirection = traceVec(bDirection);
                const float agreement = virtual_stock::Dot(
                    bDirection, toPoint(solverForward));
                trace->bAgreement = std::isfinite(agreement)
                    ? agreement : 0.0f;
                trace->bExtremeRejected =
                    rejectedExtreme && !trace->bSteeringRetained;
                trace->bRejectedAgreement = trace->bExtremeRejected
                    ? rejectedAgreement : 0.0f;
            }
        }
        if (!selection.valid)
        {
            result.rejectedExtreme = selection.rejectedExtreme;
            result.rejectedAgreement = selection.rejectedAgreement;
            if constexpr (CaptureTrace)
            {
                trace->exactAEndpointSelected = true;
                // Rejected B owns zero authority: the Lab records the
                // rejection (the same agreement the trace re-derivation saw)
                // without steering anything. The 0.35 agreement floor is
                // unchanged for base/PG-off behaviour and for every
                // unretained invocation; the only case that never reaches
                // this branch is a retained assertion (persistent grip wired
                // AND the durable relationship readable+engaged AND this
                // invocation proving the same owner AND Virtual Stock off),
                // which is accepted above and therefore flows through the
                // normal accepted-B authority path exactly like an in-cone
                // selection.
                const float labRejectedRequested =
                    std::isfinite(inputs.twoHandLabOffhandInfluence)
                    ? std::clamp(inputs.twoHandLabOffhandInfluence,
                          0.0f, 1.0f)
                    : 0.0f;
                const float labRejectedConfidence =
                    labAgreementMode ==
                        two_hand_lab::AgreementMode::SoftAuthority
                    ? two_hand_lab::SoftAuthorityConfidence(
                          trace->bAgreement,
                          inputs.twoHandLabSoftFullAgreement)
                    : 1.0f;
                fillLabTrace(false, trace->bAgreement,
                    labRejectedConfidence, labRejectedRequested, 0.0f);
            }
            finishAimPose();
            return result;
        }
        // Offhand directional authority (VS-OFF two-hand, accepted B only).
        // Product (W3): the requested authority is the configured free two-hand
        // offhand influence, consumed only by the VS-OFF free two-hand path. A
        // Virtual Stock solve keeps today's full stock-direction rebuild, and a
        // Lab-active solve reads its own stored influence below. influence >= 1
        // keeps today's full-B direction; influence <= 0 keeps the exact
        // primary orientation (no positional rebuild, never a forward/up
        // rebuild); a strictly intermediate influence blends primary direction
        // -> B through the same tested TryBlendDirectionAuthority the Lab
        // consumes, and a failed blend keeps the exact primary (Hybrid
        // precedent). The setting is DIRECTIONAL AUTHORITY only: acceptance and
        // twoHandActive stay the legacy accepted-B semantics at every
        // influence, so zero authority never decides whether the weapon is
        // logically held.
        //
        // Lab-active solves keep the Lab's own stored influence and its Soft
        // authority confidence scaling: the product setting is never read while
        // the Lab owns the solve.
        //
        // T13: a retained assertion (engaged+trusted persistent-grip
        // invocation, Virtual Stock off) arrives here as an ordinary accepted
        // B, so the Lab reads one coherent selection and the retained
        // invocation is reported as accepted (bValid) with its true crossed
        // agreement; nothing in this path down-weights it.
        const bool productOffhandAuthority =
            !inputs.virtualStockEnabled && !labActive;
        float requestedInfluence = 1.0f;
        if (productOffhandAuthority)
        {
            requestedInfluence = std::isfinite(inputs.twoHandOffhandInfluence)
                ? std::clamp(inputs.twoHandOffhandInfluence, 0.0f, 1.0f)
                : 0.0f;
        }
        float effectiveInfluence = requestedInfluence;
        float appliedAgreement = 0.0f;
        float appliedConfidence = 1.0f;
        virtual_stock::Point3 appliedDirection = selection.direction;
        bool exactPrimary = effectiveInfluence <= 0.0f;
        if (labActive)
        {
            const virtual_stock::Point3 rawForwardPoint =
                toPoint(solverForward);
            const float dotAgreement = virtual_stock::Dot(
                rawForwardPoint, selection.direction);
            appliedAgreement =
                std::isfinite(dotAgreement) ? dotAgreement : 0.0f;
            requestedInfluence =
                std::isfinite(inputs.twoHandLabOffhandInfluence)
                ? std::clamp(inputs.twoHandLabOffhandInfluence, 0.0f, 1.0f)
                : 0.0f;
            appliedConfidence =
                labAgreementMode ==
                    two_hand_lab::AgreementMode::SoftAuthority
                ? two_hand_lab::SoftAuthorityConfidence(appliedAgreement,
                      inputs.twoHandLabSoftFullAgreement)
                : 1.0f;
            effectiveInfluence = requestedInfluence * appliedConfidence;
            exactPrimary = effectiveInfluence <= 0.0f;
        }
        if (!exactPrimary && effectiveInfluence < 1.0f)
        {
            virtual_stock::Point3 blended{};
            if (virtual_stock::TryBlendDirectionAuthority(
                    toPoint(solverForward), selection.direction,
                    effectiveInfluence, blended))
                appliedDirection = blended;
            else
                exactPrimary = true;
        }
        if (exactPrimary)
        {
            // Accepted B owns presentation even at zero numerical authority
            // (Hybrid precedent): active, exact primary orientation. If the
            // optional VS-OFF filter is active, rebuild only this two-hand
            // primary direction; the one-hand branch remains byte-identical.
            result.twoHandActive = true;
            if (solverForwardSmoothed)
            {
                result.pose.orientation = rq;
            }
            if constexpr (CaptureTrace)
            {
                trace->exactAEndpointSelected = true;
                trace->finalDirectionValid = true;
                trace->finalDirection = traceVec(toPoint(solverForward));
                fillLabTrace(true, appliedAgreement, appliedConfidence,
                    requestedInfluence, effectiveInfluence);
            }
            finishAimPose();
            return result;
        }
        virtual_stock::Quat4 aimOrientation{};
        if constexpr (CaptureTrace)
            trace->orientationRebuildAttempted = true;
        if (!virtual_stock::BuildTwoHandAimOrientation(
                appliedDirection, toPoint(rup), aimOrientation))
        {
            if constexpr (CaptureTrace)
            {
                trace->exactAEndpointSelected = true;
                fillLabTrace(true, appliedAgreement, appliedConfidence,
                    requestedInfluence, effectiveInfluence);
            }
            finishAimPose();
            return result;
        }
        result.pose.orientation = {
            aimOrientation.x, aimOrientation.y,
            aimOrientation.z, aimOrientation.w};
        result.twoHandActive = true;
        if constexpr (CaptureTrace)
        {
            trace->orientationRebuildSucceeded = true;
            trace->finalDirectionValid = true;
            trace->finalDirection = traceVec(appliedDirection);
            trace->exactAEndpointSelected = false;
            fillLabTrace(true, appliedAgreement, appliedConfidence,
                requestedInfluence, effectiveInfluence);
        }
        finishAimPose();
        return result;
    }

    // Recipient extraction-shape name for the product aim solver. The donor
    // merged the legacy and stock paths into one body, so this is the exact
    // same calculation ComputeAimPose() performs (no Adaptive-era branch).
    AimPoseResult ComputeStockAimPose(const AimPoseInputs& inputs) noexcept
    {
        return ComputeAimPoseImpl<false>(inputs, nullptr);
    }
