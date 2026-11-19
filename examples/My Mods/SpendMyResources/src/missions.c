static int waiting_for_spawn;

static unsigned TrackSelectedAccess(HfMod* mod, const uint32_t weapons[3], const uint32_t* stratagems, uint32_t count)
{
    unsigned total = 0;
    for (size_t i = 0; i < PRICE_COUNT; ++i)
    {
        int weapon = kLoadoutPrices[i].kind == PRICE_WEAPON;
        const uint32_t* picks = weapon ? weapons : stratagems;
        uint32_t pick_count = weapon ? 3 : count;
        for (uint32_t j = 0; j < pick_count; ++j)
        {
            if (picks[j] != kLoadoutPrices[i].id) continue;
            total += kLoadoutPrices[i].requisition;
            AcquireSRWLockExclusive(&shop_lock);
            int had_access = access[i];
            deployed_access[i] = access[i];
            ReleaseSRWLockExclusive(&shop_lock);
            HF_API(mod)->log(mod, "  %s selected: %u Requisition slips; %s",
                kLoadoutPrices[i].name, kLoadoutPrices[i].requisition,
                had_access ? "access active; consumed at mission end" : "NO PURCHASED ACCESS");
            break;
        }
    }
    return total;
}

static int LogLoadout(HfMod* mod, uintptr_t loadout, const char* when)
{
    const HfApi* api = HF_API(mod);
    uint32_t weapons[3], count, stratagems[MAX_STRATAGEMS];
    if (!loadout || api->read(loadout + STRATAGEM_COUNT, &count, sizeof count) || count > MAX_STRATAGEMS)
        return 0;
    for (uint32_t i = 0; i < 3; ++i)
        if (api->read(loadout + i * WEAPON_STRIDE, &weapons[i], sizeof weapons[i]) || !weapons[i])
            return 0;
    for (uint32_t i = 0; i < count; ++i)
        if (api->read(loadout + STRATAGEM_SLOTS + i * STRATAGEM_STRIDE, &stratagems[i], sizeof stratagems[i]))
            return 0;

    api->log(mod, "%s loadout: primary=0x%08X secondary=0x%08X throwable=0x%08X stratagem_slots=%u",
        when, weapons[0], weapons[1], weapons[2], count);
    for (uint32_t i = 0; i < count; ++i)
        api->log(mod, "  stratagem[%u] type=%u", i + 1, stratagems[i]);
    unsigned total = TrackSelectedAccess(mod, weapons, stratagems, count);
    api->log(mod, "  Total listed loadout price: %u Requisition slips (charged at purchase, not again at deployment)", total);
    return 1;
}

static void MissionStart(HfMod* mod, const HfStateEvent* event, void* user)
{
    const HfApi* api = HF_API(mod);
    uintptr_t game = api->module_base("game.dll", NULL);
    waiting_for_spawn = 0;
    if (!game || !LayoutMatches(api, game))
    {
        api->log(mod, "Mission start: loadout not read; game layout does not match the reviewed build");
        return;
    }

    const HfLocalUser* local = hf_global_LocalUser(mod);
    uint64_t peer = 0;
    if (local && api->read((uintptr_t)local + offsetof(HfLocalUser, peer), &peer, sizeof peer) == 0 &&
        LogLoadout(mod, LocalLoadout(api, game, peer), "Mission start"))
        return;

    waiting_for_spawn = 1;
    api->log(mod, "Mission start: local loadout not ready; will retry on local deployment");
}

static void PlayerSpawn(HfMod* mod, const HfPlayerEvent* event, void* user)
{
    if (!waiting_for_spawn || !event->local)
        return;
    waiting_for_spawn = 0;
    const HfApi* api = HF_API(mod);
    uintptr_t game = api->module_base("game.dll", NULL);
    if (!game || !LogLoadout(mod, LocalLoadout(api, game, event->peer), "Local deployment"))
        api->log(mod, "Local deployment: local loadout could not be read");
}

static void MissionEnd(HfMod* mod, const HfStateEvent* event, void* user)
{
    if (waiting_for_spawn)
        HF_API(mod)->log(mod, "Mission end: no local deployment to retry the loadout read");
    waiting_for_spawn = 0;
    AcquireSRWLockExclusive(&shop_lock);
    for (size_t i = 0; i < PRICE_COUNT; ++i)
        if (deployed_access[i])
        {
            access[i] = 0;
        }
    memset(deployed_access, 0, sizeof deployed_access);
    ReleaseSRWLockExclusive(&shop_lock);
}
