// Optional geometry adapted from Gab_dC's contribution. Included inside vr.cpp's
// anonymous namespace after CurrentAimPoseInputs. The default-off path keeps
// the existing controller solver; this path never changes the pose position.
    AimPoseInputs CurrentStockAimPoseInputs(
        bool rightValid, const XrPosef& right,
        bool leftValid, const XrPosef& left,
        bool coherentHeadValid, const XrVector3f& coherentHeadPosition,
        const XrQuaternionf& coherentHeadOrientation) noexcept
    {
        AimPoseInputs inputs = CurrentAimPoseInputs(rightValid, right, leftValid, left);
        inputs.virtualStockEnabled = g_config.virtual_stock;
        inputs.virtualStockStrength = g_config.virtual_stock_strength;
        inputs.virtualStockRearHeightM = g_config.virtual_stock_rear_height_m;
        inputs.virtualStockRearReference = g_config.virtual_stock_rear_reference;
        inputs.virtualStockShoulderBackM = g_config.virtual_stock_shoulder_back_m;
        inputs.virtualStockShoulderSideM = g_config.virtual_stock_shoulder_side_m;
        inputs.virtualStockChestHeightM = g_config.virtual_stock_chest_height_m;
        inputs.virtualStockChestBackM = g_config.virtual_stock_chest_back_m;
        inputs.virtualStockChestSideM = g_config.virtual_stock_chest_side_m;
        inputs.virtualStockAdaptiveTopHeightM =
            g_config.virtual_stock_adaptive_top_height_m;
        inputs.virtualStockAdaptiveBottomHeightM =
            g_config.virtual_stock_adaptive_bottom_height_m;
        inputs.virtualStockAdaptiveTopHalfWidthM =
            g_config.virtual_stock_adaptive_top_half_width_m;
        inputs.virtualStockAdaptiveBottomHalfWidthM =
            g_config.virtual_stock_adaptive_bottom_half_width_m;
        inputs.virtualStockLeftHanded =
            g_capturedLeftHanded.load(std::memory_order_acquire);
        inputs.virtualStockProximityRelease = g_config.virtual_stock_proximity_release;
        inputs.virtualStockProximityFullM = g_config.virtual_stock_proximity_full_m;
        inputs.virtualStockProximityReleaseM = g_config.virtual_stock_proximity_release_m;
        inputs.headValid = coherentHeadValid;
        inputs.headPosition = coherentHeadPosition;
        inputs.headOrientation = coherentHeadOrientation;
        // Keep the released raw aim-pose endpoint. No new grip-pose action,
        // palm offset or acquisition behavior enters this optional solver.
        inputs.supportPosition = left.position;
        return inputs;
    }


AimPoseResult ComputeStockAimPose(const AimPoseInputs& inputs) noexcept
    {
        AimPoseResult result{};
        if (!inputs.rightValid)
            return result;

        result.updateTwoHandActivity = true;
        result.pose = inputs.right;

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
        };

        if (!inputs.twoHandEnabled || !inputs.leftValid ||
            !inputs.twoHandLatched)
        {
            finishAimPose();
            return result;
        }

        // Two-hand direction selection and orientation construction live in
        // the virtual_stock helper so unit tests exercise the production
        // maths. Legacy controller->controller behavior and thresholds are
        // unchanged. A valid positive-strength stock ray bypasses the
        // primary-forward agreement test; a degenerate stock ray falls back
        // to the legacy line. Base position stays the primary controller
        // throughout: only result.pose.orientation is reassigned below,
        // never result.pose.position.
        const XrVector3f supportEndpoint = inputs.supportPosition;
        const XrQuaternionf rq = inputs.right.orientation;
        const XrVector3f rp = inputs.right.position;
        const XrVector3f rup = Rotate(rq, {0,1,0});
        const XrVector3f rawForward = Rotate(rq, {0,0,-1});
        const auto toPoint = [](const XrVector3f& v) {
            return virtual_stock::Point3{v.x, v.y, v.z};
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
        if (shoulderRequested)
        {
            virtual_stock::Point3 shoulderRear{};
            const bool shoulderTargetValid =
                virtual_stock::TryBuildHmdRelativeShoulderRearTarget(
                    toPoint(inputs.headPosition),
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
                    shoulderRear, toPoint(supportEndpoint), toPoint(rp),
                    toPoint(supportEndpoint), toPoint(rawForward));
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
                    chestRear, toPoint(supportEndpoint), toPoint(rp),
                    toPoint(supportEndpoint), toPoint(rawForward));
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
                    adaptiveTarget, toPoint(supportEndpoint), toPoint(rp),
                    toPoint(supportEndpoint), toPoint(rawForward));
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
            stockStrength = virtual_stock::ApplyVirtualStockProximityRelease(
                inputs.virtualStockProximityRelease,
                inputs.virtualStockStrength, toPoint(rp), inputs.headValid,
                toPoint(inputs.headPosition), inputs.virtualStockRearHeightM,
                inputs.virtualStockProximityFullM,
                inputs.virtualStockProximityReleaseM);
            selection = virtual_stock::SelectTwoHandAimDirection(
                inputs.virtualStockEnabled, stockStrength,
                inputs.virtualStockRearHeightM, inputs.headValid,
                toPoint(inputs.headPosition), toPoint(supportEndpoint),
                toPoint(rp), toPoint(supportEndpoint), toPoint(rawForward));
        }
        if (!selection.valid)
        {
            result.rejectedExtreme = selection.rejectedExtreme;
            result.rejectedAgreement = selection.rejectedAgreement;
            finishAimPose();
            return result;
        }
        virtual_stock::Quat4 aimOrientation{};
        if (!virtual_stock::BuildTwoHandAimOrientation(
                selection.direction, toPoint(rup), aimOrientation))
        {
            finishAimPose();
            return result;
        }
        result.pose.orientation = {
            aimOrientation.x, aimOrientation.y,
            aimOrientation.z, aimOrientation.w};
        result.twoHandActive = true;
        finishAimPose();
        return result;
    }
