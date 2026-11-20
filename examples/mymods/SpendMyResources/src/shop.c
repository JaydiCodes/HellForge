static void ShopMessage(HfMod* mod, const char* text)
{
    AcquireSRWLockExclusive(&shop_lock);
    int changed = strcmp(shop_message, text) != 0;
    snprintf(shop_message, sizeof shop_message, "%s", text);
    ReleaseSRWLockExclusive(&shop_lock);
    if (changed) HF_API(mod)->log(mod, "%s", text);
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
    if (i >= PRICE_COUNT) return 0;
    AcquireSRWLockShared(&shop_lock);
    *out = (SmrItem){kLoadoutPrices[i].name, kLoadoutPrices[i].requisition, access[i] != 0,
        owned_amount[i] != 0, charge.state != CHARGE_IDLE, (checkout_mask & (1u << i)) != 0};
    ReleaseSRWLockShared(&shop_lock);
    return 1;
}

static void ShopTick(HfMod* mod, float dt, void* user)
{
    const HfApi* api = HF_API(mod);
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
        game_base = game;
        inventory_remove = (void (*)(uintptr_t, uint32_t, uint32_t))(game + 0x136F5E0);
        inventory_add = (void (*)(uintptr_t, uint32_t, uint32_t, uint32_t, const char*, int))(game + 0x136EF80);
    }
    if (!InstallReadyGate(mod))
    {
        AcquireSRWLockExclusive(&shop_lock);
        inventory_ready = 0;
        ReleaseSRWLockExclusive(&shop_lock);
        shop_failed = 1;
        return;
    }
    uintptr_t manager = Inventory(api);
    ReadCurrencies(mod, manager);
    ProcessExchange(mod, manager);
    if (shop_failed) return;
    unsigned selected = 0;
    int selection_read = SelectedItems(mod, &selected);
    unsigned balance = 0, amounts[PRICE_COUNT] = {0};
    int ready = manager && ItemRecord(api, manager, REQUISITION, &balance);
    for (size_t i = 0; i < PRICE_COUNT && ready; ++i)
    {
        uint32_t item_state = 0;
        uintptr_t record = ItemRecord(api, manager, kLoadoutPrices[i].mix_id, &amounts[i]);
        ready = record && !api->read(record + 0x14, &item_state, sizeof item_state);
        if (item_state != 2 && item_state != 4) amounts[i] = 0;
    }
    AcquireSRWLockExclusive(&shop_lock);
    int became_ready = ready && !inventory_ready;
    inventory_ready = ready;
    wallet = balance;
    int state = charge.state;
    checkout_mask = selected;
    checkout_valid = selection_read;
    for (size_t i = 0; i < PRICE_COUNT; ++i)
        owned_amount[i] = amounts[i];
    ReleaseSRWLockExclusive(&shop_lock);
    if (!ready) return;
    if (became_ready && state == CHARGE_IDLE)
        ShopMessage(mod, "Purchases deduct real requisition from this offline profile.");

    ProcessPurchase(mod, manager, balance, state);
}
