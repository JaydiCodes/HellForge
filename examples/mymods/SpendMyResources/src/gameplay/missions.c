static int waiting_for_spawn;

static unsigned TrackSelectedAccess(HfMod* mod, const uint32_t weapons[3], const uint32_t* stratagems, uint32_t count)
{
    unsigned total = 0;
    for (size_t i = 0; i < loadout_price_count; ++i)
    {
        if (!kLoadoutPrices[i].requisition) continue;
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
    if (!ReadStratagems(api, loadout, stratagems, &count))
        return 0;
    for (uint32_t i = 0; i < 3; ++i)
        if (api->read(loadout + i * WEAPON_STRIDE, &weapons[i], sizeof weapons[i]) || !weapons[i])
            return 0;

    api->log(mod, "%s loadout: primary=0x%08X secondary=0x%08X throwable=0x%08X stratagem_slots=%u",
        when, weapons[0], weapons[1], weapons[2], count);
    for (uint32_t i = 0; i < count; ++i)
        api->log(mod, "  stratagem[%u] type=%u", i + 1, stratagems[i]);
    unsigned total = TrackSelectedAccess(mod, weapons, stratagems, count);
    api->log(mod, "  Total listed loadout price: %u Requisition slips (charged at purchase, not again at deployment)", total);
    ApplyUpgrade(mod, loadout, 0);
    ApplyHelldiver(mod, loadout);
    ApplyEquipment(mod, weapons[0]);
    return 1;
}

static void RetainExtra(HfMod* mod, const char* message)
{
    extra_deployment.finished = 1;
    ShopMessage(mod, message);
}

static void ExtraSlotTick(HfMod* mod, float dt)
{
    if (extra_deployment.finished || upgraded_count) return;
    const HfApi* api = HF_API(mod);
    AcquireSRWLockShared(&shop_lock);
    uint32_t type = upgrade_purchased == SMR_UPGRADE_EXTRA_SLOT ? extra_pick : 0;
    ReleaseSRWLockShared(&shop_lock);
    if (!type || api->game_state(mod) != HF_STATE_MISSION) return;
    if (dt > 0) extra_deployment.seconds += dt;
    if (extra_deployment.seconds < extra_deployment.next_check) return;
    if (extra_deployment.seconds >= 90)
    {
        RetainExtra(mod, "Extra stratagem deployment or assets timed out; purchase retained for a later mission.");
        return;
    }
    extra_deployment.next_check = extra_deployment.seconds + 0.25f;
    if (!extra_deployment.peer) extra_deployment.peer = LocalPeer(mod);
    ExtraContext context;
    if (!ReadExtraContext(mod, &context))
    {
        extra_deployment.loadout = 0;
        return;
    }
    /* The loadout can be rebuilt in the first deployment update. Wait for the same record on the next poll. */
    if (context.loadout != extra_deployment.loadout)
    {
        extra_deployment.loadout = context.loadout;
        return;
    }
    HfStratagemSettings row;
    uintptr_t at = EligibleExtra(api, Inventory(api), context.picks, context.count, type, &row);
    if (!at)
    {
        RetainExtra(mod, "Extra stratagem no longer eligible or mission list full; purchase retained for a later mission.");
        return;
    }
    int assets = ExtraPackages(mod, at, &row);
    if (assets == HF_PACKAGE_LOADING) return;
    if (assets != HF_PACKAGE_RESIDENT)
    {
        RetainExtra(mod, "Extra stratagem assets unavailable; purchase retained for a later mission.");
        return;
    }
    UpgradeEdit edits[MAX_UPGRADE_EDITS];
    size_t count;
    uint32_t added;
    if (!PlanUpgrade(mod, context.loadout, SMR_UPGRADE_EXTRA_SLOT, type, edits, &count) ||
        !ApplyUpgradeEdits(mod, edits, count) || !(added = AppendExtra(mod, &context, &row)))
    {
        ResetUpgrade(mod);
        RetainExtra(mod, "Extra stratagem grant could not be verified; purchase retained and not retried in this mission.");
        return;
    }
    extra_deployment.finished = 1;
    const uint32_t nothing[3] = {0};
    TrackSelectedAccess(mod, nothing, &type, 1);
    api->log(mod, "Extra Stratagem Slot active: %s%s (mission entries %u -> %u).",
        SmrExtraName(type), added == 2 ? " with Eagle Rearm" : "", context.count, context.count + added);
}

static void UpgradeStateChanged(HfMod* mod, const HfStateEvent* event, void* user)
{
    /* The SDK marks uses as mission-timed; do not set them after deployment. */
    if (event->to == HF_STATE_PREPARE_MISSION && event->from != HF_STATE_MISSION)
    {
        uintptr_t loadout = ReadLocalLoadout(mod);
        ApplyUpgrade(mod, loadout, 1);
        ApplyHelldiver(mod, loadout);
    }
    else if (event->from == HF_STATE_PREPARE_MISSION && event->to != HF_STATE_MISSION)
    {
        ResetUpgrade(mod);
        FinishHelldiver(mod);
    }
}

static void MissionStart(HfMod* mod, const HfStateEvent* event, void* user)
{
    const HfApi* api = HF_API(mod);
    uintptr_t game = api->module_base("game.dll", NULL);
    waiting_for_spawn = 0;
    memset(&extra_deployment, 0, sizeof extra_deployment);
    FinishKills(mod);
    ResetKills();
    SmrFinishCheckout(0);
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
    if (!event->local) return;
    if (!extra_deployment.peer) extra_deployment.peer = event->peer;
    if (!waiting_for_spawn) return;
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
    memset(&extra_deployment, 0, sizeof extra_deployment);
    SettleKills(mod);
    int consumed_upgrade = upgraded_count != 0;
    ResetUpgrade(mod);
    int consumed_helldiver = FinishHelldiver(mod);
    FinishEquipment(mod);
    AcquireSRWLockExclusive(&shop_lock);
    if (consumed_upgrade)
    {
        upgrade_purchased = SMR_UPGRADE_NONE;
        extra_pick = 0;
    }
    if (consumed_helldiver) memset(helldiver_purchased, 0, sizeof helldiver_purchased);
    for (size_t i = 0; i < loadout_price_count; ++i)
        if (deployed_access[i])
        {
            access[i] = 0;
        }
    memset(deployed_access, 0, sizeof deployed_access);
    ReleaseSRWLockExclusive(&shop_lock);
}
