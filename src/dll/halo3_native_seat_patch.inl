// Included by game.cpp and the production native seat lifetime fixture.
    enum class Halo3NativeSeatState : uint32_t
    {
        Stock = 0,
        Active,
        StockFallback,
    };

    // C17 loaded-tag transaction. Camera-thread owned; worker-visible fields
    // are atomics used only for transition logging. The saved values are
    // restored on seat change/exit or when the feature is disabled.
    struct Halo3NativeSeatPatch
    {
        uint32_t generation = 0;
        uint32_t definitionIndex = 0xFFFFFFFFu;
        int seatIndex = -1;
        uint32_t* flags = nullptr;
        unsigned char* tagBase = nullptr;
        unsigned char* definition = nullptr;
        void* instances = nullptr;
        int32_t* cameraTrackCount = nullptr;
        uint32_t originalFlags = 0;
        int32_t originalCameraTrackCount = 0;
        bool active = false;
        // The seat's tag flag was actually written (the dev A/B path, seat_view_tag_write). False = the tag is as
        // loaded and the camera director is answered instead (halo3_seat_view.inl, the default).
        bool tagWritten = false;
        // The last attempt at this seat: its mode, and whether it failed only because the director's relay was
        // not up. Such a seat is tried again when the relay is, or when the mode changes.
        bool attemptWroteTag = false;
        bool waitedForView = false;
    };
    Halo3NativeSeatPatch g_halo3NativeSeatPatch;
    std::atomic<uint32_t> g_halo3NativeSeatState{
        static_cast<uint32_t>(Halo3NativeSeatState::Stock)};
    std::atomic<uint32_t> g_halo3NativeSeatSerial{0};
    // The occupied seat's ORIGINAL tag flags lack bit 4 (the third-person camera flag), so C20 refused it and the
    // game itself rebases this seat's aim by the vehicle's up frame, unmodded. 0 = not seen (or not seated);
    // otherwise Halo3SeatNativeNoThirdPersonKey(). Written here (camera thread), read by the WaitThread: the
    // world-angle rebase skip (seat_aim_world_frame.inl) stands down while it is set, and logs the seat once.
    std::atomic<uint32_t> g_halo3SeatNativeNoThirdPerson{0};

    constexpr uint32_t Halo3SeatNativeNoThirdPersonKey(uint32_t definitionIndex, int seatIndex)
    {
        return 0x80000000u | (definitionIndex & 0xFFFFu) << 8 | (static_cast<uint32_t>(seatIndex) & 0xFFu);
    }

    // A module generation does not identify a loaded map's tag allocation.
    // Re-resolve from current storage before reading or restoring the lease.
    uint32_t* Halo3ResolveOwnedSeatStorage(const Halo3NativeSeatPatch& patch)
    {
        if (!patch.active || !patch.flags || !patch.generation ||
            patch.generation != TitleAdapter_GetGeneration(GameTitle::Halo3))
            return nullptr;
        auto* tagBase = g_halo3TagDataBase
            ? static_cast<unsigned char*>(*g_halo3TagDataBase) : nullptr;
        void* instances = g_halo3TagInstanceTable ? *g_halo3TagInstanceTable : nullptr;
        if (!tagBase || tagBase != patch.tagBase || !instances || instances != patch.instances)
            return nullptr;
        auto* definition = Halo3LoadedTagDefinition(patch.definitionIndex);
        if (!definition || definition != patch.definition) return nullptr;
        const int32_t count = *reinterpret_cast<const int32_t*>(
            definition + kHalo3VehicleSeatsBlockOffset);
        const uint32_t address = *reinterpret_cast<const uint32_t*>(
            definition + kHalo3VehicleSeatsBlockOffset + 4);
        if (!address || count <= 0 || count > 126 ||
            patch.seatIndex < 0 || patch.seatIndex >= count) return nullptr;
        auto* liveFlags = reinterpret_cast<uint32_t*>(tagBase + size_t(address)*4 +
            size_t(patch.seatIndex)*kHalo3VehicleSeatStride);
        return liveFlags == patch.flags ? liveFlags : nullptr;
    }

    bool Halo3NativeSeatStillOwned(const Halo3NativeSeatPatch& patch)
    {
        __try
        {
            const auto* flags = Halo3ResolveOwnedSeatStorage(patch);
            return flags && *flags == (patch.tagWritten ? Halo3FirstPersonSeatFlags(patch.originalFlags)
                                                         : patch.originalFlags);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // The occupied Halo 3 seat's own flags, or 0 when no seat is owned. The
    // first-person seat patch already re-reads this record from the loaded tag
    // on entry and keeps the ORIGINAL word, so no second tag walk is needed.
    uint32_t Halo3OccupiedSeatFlags()
    {
        const Halo3NativeSeatPatch& patch = g_halo3NativeSeatPatch;
        if (!patch.active ||
            g_halo3NativeSeatState.load(std::memory_order_acquire) !=
                static_cast<uint32_t>(Halo3NativeSeatState::Active))
        {
            return 0;
        }
        return patch.originalFlags;
    }

    void Halo3RestoreNativeSeatPatch()
    {
        Halo3NativeSeatPatch& patch = g_halo3NativeSeatPatch;
        // The director stops being answered for this occupant at once, whatever else follows.
        Halo3SeatViewDisarm();
        const bool hadState = patch.active ||
            g_halo3NativeSeatState.load(std::memory_order_relaxed) !=
                static_cast<uint32_t>(Halo3NativeSeatState::Stock);
        // Only a tag this lease wrote is written back.
        if (patch.active && patch.tagWritten && patch.generation &&
            patch.generation == TitleAdapter_GetGeneration(GameTitle::Halo3))
        {
            __try
            {
                const uint32_t patchedFlags =
                    Halo3FirstPersonSeatFlags(patch.originalFlags);
                // Storage identity and compare/exchange are both required:
                // a reused address or concurrent native writer is not ours.
                auto* liveFlags = Halo3ResolveOwnedSeatStorage(patch);
                if (liveFlags)
                    InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(liveFlags),
                        static_cast<LONG>(patch.originalFlags), static_cast<LONG>(patchedFlags));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                // The loaded map may already be disappearing. Feature cleanup
                // must never disarm the working camera core.
            }
        }
        patch = {};
        g_halo3NativeAnchorCalibration = {};
        g_halo3SeatNativeNoThirdPerson.store(0, std::memory_order_relaxed);
        g_halo3NativeSeatState.store(
            static_cast<uint32_t>(Halo3NativeSeatState::Stock),
            std::memory_order_release);
        if (hadState)
            g_halo3NativeSeatSerial.fetch_add(1, std::memory_order_release);
    }

    // A chase camera of the mod's own shows the seat: whatever makes the director's answer "first person" (bit 4 clear
    // in the seat's own data, or a host's own hook on the test), it would draw the arms and weapon at that camera. Its
    // occupant is told "third person" instead, so the game draws the character (seat_view_logic.h, THE OTHER WAY
    // ROUND). A seat the game already answers third person is not changed. The director alone is answered; the tag is
    // not written.
    void Halo3ArmChaseCamera(int32_t unit)
    {
        Halo3SeatViewArmChase(unit != -1 && !Halo3SeatViewWritesTag() && Halo3SeatViewAvailable() &&
                                      Halo3VehicleCameraThirdPerson()
                                  ? unit : -1);
    }

    // THE FIRST-PERSON SEAT. For as long as the VR vehicle camera owns the view, Halo is to treat the occupant as
    // first person, so it stops drawing the player's character model in the space the headset is looking out of.
    //
    // THE DEFAULT: the loaded tag is NOT written. The seat record is found and checked exactly as
    // before, its flags are kept for the readers that want them, and the occupant is armed for the camera
    // director's relay (halo3_seat_view.inl), which answers the director alone. The simulation reads a stock seat:
    // the same on every machine of a lockstep game, and the same as an unmodded host's.
    //
    // C20, THE TAG WRITE, is kept behind the dev key seat_view_tag_write for an A/B: clear the occupied seat's
    // `third person camera` bit. Halo re-reads the bit from the loaded tag every camera frame (halo3+0x1326E8 ->
    // +0x212198), so the effect is immediate and the restore below is complete -- and so does the simulation
    // (the unit camera evaluator for the firing assist and the acquisition, and the seated camera evaluator
    // +0x3555EC for 28 callers), which is what puts a co-op campaign client out of step with its host.
    //
    // This is NOT C17. C17 additionally zeroed the seat's camera-track count
    // and then made the resulting native camera the motion parent; that was
    // headset-rejected. Only the flag is written here, the camera-track count
    // is read as a bounds check and never modified, and the camera itself
    // remains the accepted C18/C19 authored-node anchor.
    bool Halo3EnsureFirstPersonSeatFlag(uint32_t definitionIndex, int seatIndex,
                                        uint32_t generation, int32_t unit = -1)
    {
        Halo3NativeSeatPatch& patch = g_halo3NativeSeatPatch;
        const bool sameSeat = patch.generation == generation &&
            patch.definitionIndex == definitionIndex &&
            patch.seatIndex == seatIndex;
        if (!g_config.vehicle_first_person || !g_config.vehicle_hide_body)
        {
            // The body setting off still leaves a chase camera to answer. Given back once, not per frame: giving back
            // disarms, and a director call between that and the next line would draw the arms.
            if (!g_config.vehicle_first_person || patch.generation ||
                g_halo3NativeSeatState.load(std::memory_order_relaxed) !=
                    static_cast<uint32_t>(Halo3NativeSeatState::Stock))
                Halo3RestoreNativeSeatPatch();
            if (g_config.vehicle_first_person)
                Halo3ArmChaseCamera(unit);
            return false;
        }
        const bool writeTag = Halo3SeatViewWritesTag();
        if (sameSeat && patch.active && patch.tagWritten == writeTag && Halo3NativeSeatStillOwned(patch) &&
            (writeTag || Halo3SeatViewAvailable()))
        {
            // A chase camera of the mod's own (vehicle_cameras.inl) shows the seat from outside: the
            // director keeps its own third-person answer, so the game draws the player's character.
            if (!writeTag)
            {
                Halo3SeatViewArm(Halo3VehicleCameraThirdPerson() ? -1 : unit);
                Halo3ArmChaseCamera(unit);
            }
            return true;
        }
        if (sameSeat && !patch.active && patch.attemptWroteTag == writeTag &&
            !(patch.waitedForView && Halo3SeatViewAvailable()) &&
            g_halo3NativeSeatState.load(
                std::memory_order_relaxed) ==
                static_cast<uint32_t>(Halo3NativeSeatState::StockFallback))
        {
            Halo3ArmChaseCamera(unit);
            return false;
        }

        Halo3RestoreNativeSeatPatch();
        patch.generation = generation;
        patch.definitionIndex = definitionIndex;
        patch.seatIndex = seatIndex;
        bool installed = false;
        bool nativeNoThirdPerson = false;
        bool waitedForView = false;
        __try
        {
            void** instSlot = g_halo3TagInstanceTable;
            void** baseSlot = g_halo3TagDataBase;
            auto* instTable = instSlot
                ? static_cast<unsigned char*>(*instSlot) : nullptr;
            auto* tagBase = baseSlot
                ? static_cast<unsigned char*>(*baseSlot) : nullptr;
            if (instTable && tagBase && definitionIndex <= 0xFFFFu &&
                seatIndex >= 0 && seatIndex <= kHalo3VehicleSeatMax)
            {
                const uint32_t instanceDword =
                    *reinterpret_cast<const uint32_t*>(
                        instTable + static_cast<size_t>(definitionIndex) * 8 + 4);
                auto* definition = instanceDword
                    ? tagBase + static_cast<size_t>(instanceDword) * 4 : nullptr;
                const int32_t seatCount = definition
                    ? *reinterpret_cast<const int32_t*>(
                          definition + kHalo3VehicleSeatsBlockOffset)
                    : 0;
                const uint32_t seatsAddress = definition
                    ? *reinterpret_cast<const uint32_t*>(
                          definition + kHalo3VehicleSeatsBlockOffset + 4)
                    : 0;
                if (definition && seatCount > 0 && seatCount <= 126 &&
                    seatIndex < seatCount && seatsAddress)
                {
                    auto* seat = tagBase +
                        static_cast<size_t>(seatsAddress) * 4 +
                        static_cast<size_t>(seatIndex) * kHalo3VehicleSeatStride;
                    auto* flags = reinterpret_cast<uint32_t*>(seat);
                    auto* trackCount = reinterpret_cast<int32_t*>(
                        seat + kHalo3SeatCameraTracksBlockOffset);
                    const uint32_t originalFlags = *flags;
                    const int32_t originalTrackCount = *trackCount;
                    nativeNoThirdPerson =
                        !Halo3SeatFlagsLookLikePlayerSeat(originalFlags);
                    // Every official H3 player seat has bit 4. Requiring it is
                    // the live layout proof that keeps a bad tag walk stock.
                    // The track count is only a second bounds check on the same
                    // seat record; the reference mod keeps those tracks and so
                    // do we.
                    if (Halo3SeatFlagsLookLikePlayerSeat(originalFlags) &&
                        originalTrackCount >= 0 && originalTrackCount <= 16)
                    {
                        const uint32_t patchedFlags =
                            Halo3FirstPersonSeatFlags(originalFlags);
                        patch.flags = flags;
                        patch.tagBase = tagBase;
                        patch.definition = definition;
                        patch.instances = instTable;
                        patch.cameraTrackCount = trackCount;
                        patch.originalFlags = originalFlags;
                        patch.originalCameraTrackCount = originalTrackCount;
                        if (writeTag)
                        {
                            installed = static_cast<uint32_t>(InterlockedCompareExchange(
                                reinterpret_cast<volatile LONG*>(flags),
                                static_cast<LONG>(patchedFlags), static_cast<LONG>(originalFlags))) == originalFlags;
                            patch.tagWritten = installed;
                        }
                        else
                        {
                            // The tag stays exactly as loaded. The seat is first person only if the director's
                            // relay is up to say so.
                            installed = Halo3SeatViewAvailable();
                            waitedForView = !installed;
                            patch.tagWritten = false;
                        }
                        patch.active = installed;
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            installed = false;
        }
        if (!installed)
        {
            Halo3RestoreNativeSeatPatch();
            patch.generation = generation;
            patch.definitionIndex = definitionIndex;
            patch.seatIndex = seatIndex;
            patch.attemptWroteTag = writeTag;
            patch.waitedForView = waitedForView;
            g_halo3SeatNativeNoThirdPerson.store(nativeNoThirdPerson
                ? Halo3SeatNativeNoThirdPersonKey(definitionIndex, seatIndex) : 0u,
                std::memory_order_relaxed);
            g_halo3NativeSeatState.store(
                static_cast<uint32_t>(Halo3NativeSeatState::StockFallback),
                std::memory_order_release);
            g_halo3NativeSeatSerial.fetch_add(1, std::memory_order_release);
            Halo3ArmChaseCamera(unit);
            return false;
        }
        g_halo3NativeAnchorCalibration = {};
        patch.attemptWroteTag = writeTag;
        if (!patch.tagWritten)
        {
            Halo3SeatViewArm(Halo3VehicleCameraThirdPerson() ? -1 : unit);
            Halo3ArmChaseCamera(unit);
        }
        g_halo3NativeSeatState.store(
            static_cast<uint32_t>(Halo3NativeSeatState::Active),
            std::memory_order_release);
        g_halo3NativeSeatSerial.fetch_add(1, std::memory_order_release);
        return true;
    }

