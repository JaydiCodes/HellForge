int SmrTakeCheckoutRequest(void) { return InterlockedExchange(&checkout_requested, 0) != 0; }

void SmrFinishCheckout(int ready)
{
    if (ready) InterlockedCompareExchange(&auto_ready, 2, 1);
    else
    {
        AcquireSRWLockExclusive(&shop_lock);
        upgrade_selected = SMR_UPGRADE_NONE;
        if (upgrade_purchased != SMR_UPGRADE_EXTRA_SLOT) extra_pick = 0;
        InterlockedExchange(&auto_ready, 0);
        ReleaseSRWLockExclusive(&shop_lock);
    }
}

int SmrCheckoutPaid(void) { return InterlockedCompareExchange(&auto_ready, 0, 0) == 1; }

static int UpgradeAffordable(const unsigned price[CURRENCY_COUNT], const unsigned balance[CURRENCY_COUNT])
{
    for (size_t i = 1; i < CURRENCY_COUNT; ++i)
        if (balance[i] < price[i]) return 0;
    return 1;
}

static int CheckoutFits(unsigned total, int upgrade, const unsigned price[CURRENCY_COUNT])
{
    return checkout_valid && !shop_failed && (total || upgrade) &&
        charge.state == CHARGE_IDLE && !exchange.pending &&
        (!total || (inventory_ready && wallet >= total)) &&
        (!upgrade || (upgrade_funds_ready && UpgradeAffordable(price, currency_amount)));
}

int SmrCheckout(unsigned* total, unsigned upgrade_price[CURRENCY_COUNT], int* pending)
{
    AcquireSRWLockShared(&shop_lock);
    UnpaidItems(checkout_mask, total);
    int upgrade = UpgradePrice(upgrade_selected, upgrade_price);
    *pending = charge.state != CHARGE_IDLE || exchange.pending;
    int ready = CheckoutFits(*total, upgrade, upgrade_price);
    ReleaseSRWLockShared(&shop_lock);
    return ready;
}

int SmrBuyCheckout(HfMod* mod)
{
    const HfApi* api = HF_API(mod);
    if (api->game_state(mod) != HF_STATE_SHIP) return 0;
    AcquireSRWLockExclusive(&shop_lock);
    unsigned total, upgrade_price[CURRENCY_COUNT];
    uint64_t unpaid = UnpaidItems(checkout_mask, &total);
    int upgrade = UpgradePrice(upgrade_selected, upgrade_price);
    if (!CheckoutFits(total, upgrade, upgrade_price))
    {
        ReleaseSRWLockExclusive(&shop_lock);
        ShopMessage(mod, "Checkout unavailable: check the loadout, balance and pending purchase.");
        return 0;
    }
    charge.mask = unpaid;
    charge.total = total;
    memcpy(charge.upgrade_price, upgrade_price, sizeof upgrade_price);
    charge.upgrade = upgrade ? upgrade_selected : SMR_UPGRADE_NONE;
    charge.extra = charge.upgrade == SMR_UPGRADE_EXTRA_SLOT ? extra_pick : 0;
    charge.state = CHARGE_QUEUED;
    InterlockedExchange(&auto_ready, 0);
    ReleaseSRWLockExclusive(&shop_lock);
    ShopMessage(mod, "Processing loadout purchase...");
    return 1;
}

static int DeductRuntime(const HfApi* api, uintptr_t manager, uint32_t id, unsigned cost, unsigned before, unsigned* after)
{
    inventory_remove(manager, id, cost);
    return ItemRecord(api, manager, id, after) && *after == before - cost;
}

static int UpgradeFunded(const HfApi* api, uintptr_t manager, const unsigned price[CURRENCY_COUNT],
    unsigned before[CURRENCY_COUNT])
{
    for (size_t i = 1; i < CURRENCY_COUNT; ++i)
        if (price[i] && (!ItemRecord(api, manager, kCurrencies[i].id, &before[i]) || before[i] < price[i]))
            return 0;
    return 1;
}

static void ProcessPurchase(HfMod* mod, uintptr_t manager, int balance_ready, unsigned balance, int state)
{
    if (state != CHARGE_QUEUED) return;
    uint64_t selected = 0;
    unsigned total = 0, upgrade_price[CURRENCY_COUNT], expected[CURRENCY_COUNT];
    int readable = SelectedItems(mod, &selected);
    AcquireSRWLockShared(&shop_lock);
    uint64_t unpaid = UnpaidItems(selected, &total), mask = charge.mask;
    unsigned price = charge.total;
    memcpy(upgrade_price, charge.upgrade_price, sizeof upgrade_price);
    SmrUpgrade upgrade = charge.upgrade;
    uint32_t extra = charge.extra;
    int upgrade_valid = upgrade && upgrade_selected == upgrade && !upgrade_purchased &&
        UpgradePrice(upgrade, expected) && !memcmp(expected, upgrade_price, sizeof expected) &&
        (upgrade != SMR_UPGRADE_EXTRA_SLOT || extra == extra_pick);
    ReleaseSRWLockShared(&shop_lock);
    const HfApi* api = HF_API(mod);
    unsigned before[CURRENCY_COUNT] = {0};
    if (api->game_state(mod) != HF_STATE_SHIP || !manager || !readable || unpaid != mask || total != price ||
        (price && (!balance_ready || balance < price)) ||
        (upgrade && (!upgrade_valid || !UpgradeFunded(api, manager, upgrade_price, before) ||
            !CanApplyUpgrade(mod, upgrade, extra))))
    {
        AcquireSRWLockExclusive(&shop_lock);
        charge.state = CHARGE_IDLE;
        ReleaseSRWLockExclusive(&shop_lock);
        ShopMessage(mod, "Purchase cancelled: loadout, upgrade or funds unavailable. Nothing deducted.");
        return;
    }
    unsigned after = balance, upgrade_after[CURRENCY_COUNT];
    memcpy(upgrade_after, before, sizeof before);
    char detail[128] = "Currency ledger saved.", message[192];
    int paid = 0, deducted = !price || DeductRuntime(api, manager, REQUISITION, price, balance, &after);
    for (size_t i = 1; i < CURRENCY_COUNT && deducted; ++i)
        if (upgrade_price[i])
            deducted = DeductRuntime(api, manager, kCurrencies[i].id, upgrade_price[i], before[i], &upgrade_after[i]);
    if (!deducted)
        snprintf(detail, sizeof detail, "Runtime deduction failed verification; ledger unchanged.");
    else
        paid = SaveTransaction(api, manager, detail, sizeof detail);
    if (paid)
    {
        AcquireSRWLockExclusive(&shop_lock);
        wallet = after;
        for (size_t i = 0; i < PRICE_COUNT; ++i)
            if (mask & (UINT64_C(1) << i)) access[i] = 1;
        if (upgrade)
        {
            upgrade_purchased = upgrade;
            upgrade_selected = SMR_UPGRADE_NONE;
            for (size_t i = 1; i < CURRENCY_COUNT; ++i)
                if (upgrade_price[i]) currency_amount[i] = upgrade_after[i];
        }
        charge.state = CHARGE_IDLE;
        auto_ready_selection = selected;
        InterlockedExchange(&auto_ready, 1);
        ReleaseSRWLockExclusive(&shop_lock);
        api->log(mod, "Purchased loadout: %u Req, %u Forge Credits, %u Common, %u Rare and %u Super samples, "
            "access mask 0x%llX. %s", price, upgrade_price[FORGE_CREDITS], upgrade_price[COMMON_SAMPLES],
            upgrade_price[RARE_SAMPLES], upgrade_price[SUPER_SAMPLES], (unsigned long long)mask, detail);
        ShopMessage(mod, "Loadout purchased. Readying automatically.");
        return;
    }
    snprintf(message, sizeof message, "Purchase stopped. %s No access or upgrade granted; shop disabled.", detail);
    StopShop(mod, message);
}
