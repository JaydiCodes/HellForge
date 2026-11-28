static void ShopMessage(HfMod* mod, const char* text)
{
    AcquireSRWLockExclusive(&shop_lock);
    int changed = strcmp(shop_message, text) != 0;
    snprintf(shop_message, sizeof shop_message, "%s", text);
    ReleaseSRWLockExclusive(&shop_lock);
    if (changed) HF_API(mod)->log(mod, "%s", text);
}

static void StopShop(HfMod* mod, const char* text)
{
    AcquireSRWLockExclusive(&shop_lock);
    inventory_ready = exchange_ready = upgrade_funds_ready = 0;
    exchange.pending = 0;
    charge.state = CHARGE_IDLE;
    shop_failed = 1;
    ReleaseSRWLockExclusive(&shop_lock);
    ShopMessage(mod, text);
}

int SmrBalance(unsigned* out)
{
    AcquireSRWLockShared(&shop_lock);
    *out = wallet;
    int ready = inventory_ready;
    ReleaseSRWLockShared(&shop_lock);
    return ready;
}

void SmrMessage(char* out, size_t size)
{
    AcquireSRWLockShared(&shop_lock);
    snprintf(out, size, "%s", shop_message);
    ReleaseSRWLockShared(&shop_lock);
}

int SmrShopItem(size_t i, SmrItem* out)
{
    if (i >= loadout_price_count) return 0;
    AcquireSRWLockShared(&shop_lock);
    SmrItemSet selected_set = WithExtraAccess(checkout_equipped);
    int selected = SmrItemSetHas(&selected_set, i);
    *out = (SmrItem){kLoadoutPrices[i].name, kLoadoutPrices[i].requisition, access[i] != 0,
        charge.state != CHARGE_IDLE, selected};
    ReleaseSRWLockShared(&shop_lock);
    return 1;
}

int SmrQuoteLoadout(HfMod* mod, SmrLoadoutQuote* out)
{
    out->count = 0;
    out->total = out->balance = 0;
    out->balance_ready = 0;
    AcquireSRWLockShared(&shop_lock);
    uintptr_t game = shop_failed ? 0 : game_base;
    ReleaseSRWLockShared(&shop_lock);
    SmrItemSet equipped;
    if (!game || !ReadRestrictedItems(HF_API(mod), ReadPickerLoadout(mod, game), &equipped, NULL))
        return 0;
    AcquireSRWLockShared(&shop_lock);
    int valid = !shop_failed;
    if (valid)
    {
        SmrItemSet selected = WithExtraAccess(equipped);
        SmrItemSet unpaid = UnpaidItems(&selected, &out->total);
        out->balance_ready = inventory_ready;
        out->balance = wallet;
        for (size_t i = 0; i < loadout_price_count; ++i)
        {
            if (SmrItemSetHas(&selected, i))
                out->items[out->count++] = (SmrQuoteLine){kLoadoutPrices[i].name,
                    SmrItemSetHas(&unpaid, i) ? kLoadoutPrices[i].requisition : 0};
        }
    }
    ReleaseSRWLockShared(&shop_lock);
    return valid;
}

static void ShopTick(HfMod* mod, float dt, void* user)
{
    const HfApi* api = HF_API(mod);
    ExtraSlotTick(mod, dt);
    HelldiverTick(mod, dt);
    KillsTick(mod);
    if (shop_failed) return;
    if (!game_base)
    {
        uintptr_t game = api->module_base("game.dll", NULL);
        if (!game || !LayoutMatches(api, game))
        {
            if (api->game_state(mod) == HF_STATE_SHIP)
                ShopMessage(mod, "Shop unavailable: inventory/loadout code does not match the reviewed build.");
            return;
        }
        AcquireSRWLockExclusive(&shop_lock);
        game_base = game;
        ReleaseSRWLockExclusive(&shop_lock);
        inventory_remove = (void (*)(uintptr_t, uint32_t, uint32_t))(game + 0x136F5E0);
        inventory_add = (void (*)(uintptr_t, uint32_t, uint32_t, uint32_t, const char*, int))(game + 0x136EF80);
        InitExtraSlot(mod);
    }
    if (!InstallReadyGate(mod))
    {
        AcquireSRWLockExclusive(&shop_lock);
        inventory_ready = 0;
        shop_failed = 1;
        ReleaseSRWLockExclusive(&shop_lock);
        return;
    }
    uintptr_t manager = Inventory(api);
    if (!TrackWallet(mod, manager)) manager = 0;
    if (shop_failed) return;
    ReadCurrencies(mod, manager);
    ProcessExchange(mod, manager);
    if (shop_failed) return;
    ProcessKillReward(mod, manager);
    if (shop_failed) return;
    SmrItemSet equipped = {0};
    uint32_t primary = 0;
    int selection_read = ReadEquippedItems(mod, &equipped, &primary);
    unsigned balance = 0;
    int ready = manager && ItemRecord(api, manager, REQUISITION, &balance);
    AcquireSRWLockExclusive(&shop_lock);
    int became_ready = ready && !inventory_ready;
    inventory_ready = ready;
    wallet = balance;
    int state = charge.state;
    UpdateCheckoutLoadout(equipped, primary, selection_read);
    ReleaseSRWLockExclusive(&shop_lock);
    if (became_ready && state == CHARGE_IDLE)
        ShopMessage(mod, "Purchases charge your saved Spend My Resources wallet.");

    ProcessPurchase(mod, manager, ready, balance, state);
}
