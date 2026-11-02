// The existing unit-action update supplies the local dual owner even at idle. Read only: the
// shake itself is a reload button press made on the input thread (vr.cpp).
void Halo3DualShakeUnitTick(uint32_t unit) noexcept
{
    if(!g_config.dual_wield_shake_reload||!g_halo3PlayerUnitGetter||!g_halo3UnitInVehicle)return;
    __try {
        const uint32_t local=uint32_t(g_halo3PlayerUnitGetter(0));
        uint32_t weapons[2]{};
        if(local!=UINT32_MAX && unit==local && !g_halo3UnitInVehicle(int32_t(unit)) &&
            Halo3ReadOwnedDualWeapons(unit,weapons))
            VR_DualWieldObserveOwner(GameTitle::Halo3,g_halo3RuntimeGeneration.load(),unit,weapons);
    } __except(EXCEPTION_EXECUTE_HANDLER) { /* Unknown ownership leaves native controls untouched. */ }
}
