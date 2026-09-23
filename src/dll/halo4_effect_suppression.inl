// Worker-only setting transition. The three effect bridges stay installed and
// forward stock when disabled. Their enable byte and the independent mode-one
// particle deny must change together; gating only the bridge leaves AR particles
// suppressed after the user turns the fallback off.
bool Halo4SetEffectsSuppressed(uintptr_t base, bool hide)
{
    if (!base || !g_halo4Restoration.effectsInstalled.load(std::memory_order_acquire))
        return false;
    const bool wasHidden = g_halo4Restoration.effectsModeOnePatched;
    if (wasHidden == hide && (g_halo4EffectsEnabled != 0) == hide)
        return true;

    const auto& expected = wasHidden ? kHalo4EffectModeOneHidden : kHalo4EffectModeOneStock;
    const auto& replacement = hide ? kHalo4EffectModeOneHidden : kHalo4EffectModeOneStock;
    const uintptr_t site = base + kHalo4EffectModeOneRva;
    if (!Halo4PatchMatches(site, expected))
        return false;

    // No allocation, logging or module lookup while threads are frozen. Both
    // sequences have the same three-byte extent, but the hidden sequence has a
    // second instruction at +2. Never resume a suspended interior instruction
    // into the middle of the replacement stock instruction.
    DWORD previous = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(site), expected.size(),
            PAGE_EXECUTE_READWRITE, &previous))
        return false;
    bool changed = false;
    bool resumed = true;
    {
        ReachThreadFreeze frozen;
        if (frozen.Capture())
        {
            bool safe = true;
            for (const ReachFrozenThread& thread : frozen.Threads())
            {
                if (!thread.suspended)
                    continue;
                CONTEXT context{};
                context.ContextFlags = CONTEXT_CONTROL;
                if (!GetThreadContext(thread.handle, &context))
                {
                    if (WaitForSingleObject(thread.handle, 0) != WAIT_OBJECT_0)
                        safe = false;
                    continue;
                }
                if (context.Rip > site && context.Rip < site + expected.size())
                    safe = false;
            }
            if (safe && Halo4PatchMatches(site, expected) &&
                Halo4SafeWrite(reinterpret_cast<void*>(site),
                    replacement.data(), replacement.size()) &&
                Halo4PatchMatches(site, replacement))
            {
                FlushInstructionCache(GetCurrentProcess(),
                    reinterpret_cast<void*>(site), replacement.size());
                g_halo4Restoration.effectsModeOnePatched = hide;
                g_halo4EffectsEnabled = hide ? 1 : 0;
                changed = true;
            }
            resumed = frozen.Release();
        }
    }
    DWORD ignored = 0;
    const bool restoredProtection = VirtualProtect(reinterpret_cast<void*>(site),
        expected.size(), previous, &ignored) != 0;
    return changed && resumed && restoredProtection;
}
