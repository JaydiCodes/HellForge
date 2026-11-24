int SmrTakeCheckoutRequest(void)
{
    return InterlockedCompareExchange(&checkout_gate, CHECKOUT_OPEN, CHECKOUT_REQUESTED) == CHECKOUT_REQUESTED;
}

void SmrFinishCheckout(int ready)
{
    if (ready) InterlockedCompareExchange(&checkout_gate, CHECKOUT_RESUME, CHECKOUT_CONFIRMED);
    else
    {
        AcquireSRWLockExclusive(&shop_lock);
        upgrade_selected = SMR_UPGRADE_NONE;
        if (upgrade_purchased != SMR_UPGRADE_EXTRA_SLOT) extra_pick = 0;
        InterlockedExchange(&checkout_gate, CHECKOUT_IDLE);
        ReleaseSRWLockExclusive(&shop_lock);
    }
}

int SmrCheckoutComplete(void) { return CheckoutGate() == CHECKOUT_CONFIRMED; }

static int UpgradeAffordable(const unsigned price[CURRENCY_COUNT], const unsigned balance[CURRENCY_COUNT])
{
    for (size_t i = COMMON_SAMPLES; i < CURRENCY_COUNT; ++i)
        if (balance[i] < price[i]) return 0;
    return 1;
}

static int CheckoutPending(void)
{
    return charge.state != CHARGE_IDLE || exchange.pending || SmrCheckoutComplete();
}

static SmrCheckoutAction CheckoutAction(unsigned gear_cost, int upgrade, const unsigned price[CURRENCY_COUNT], int pending)
{
    if (!checkout_valid || pending) return SMR_CHECKOUT_BLOCKED;
    uint64_t requisition = SmrRequisitionDue(gear_cost, price);
    if (!requisition && !upgrade) return SMR_CHECKOUT_CONTINUE;
    if (shop_failed || (requisition && (!inventory_ready || wallet < requisition)) ||
        (upgrade && (!checkout_solo || !upgrade_funds_ready || !UpgradeAffordable(price, currency_amount))))
        return SMR_CHECKOUT_BLOCKED;
    return SMR_CHECKOUT_PURCHASE;
}

SmrCheckoutAction SmrCheckout(unsigned* gear_cost, unsigned upgrade_price[CURRENCY_COUNT], int* pending)
{
    AcquireSRWLockShared(&shop_lock);
    UnpaidItems(WithExtraAccess(checkout_equipped, checkout_solo), gear_cost);
    int upgrade = UpgradePrice(upgrade_selected, upgrade_price);
    *pending = CheckoutPending();
    SmrCheckoutAction action = CheckoutAction(*gear_cost, upgrade, upgrade_price, *pending);
    ReleaseSRWLockShared(&shop_lock);
    return action;
}

int SmrConfirmCheckout(HfMod* mod, SmrCheckoutAction action)
{
    if (HF_API(mod)->game_state(mod) != HF_STATE_SHIP)
    {
        ShopMessage(mod, "Checkout unavailable: no longer on the ship.");
        return 0;
    }
    AcquireSRWLockExclusive(&shop_lock);
    unsigned gear_cost, upgrade_price[CURRENCY_COUNT];
    uint64_t selected = WithExtraAccess(checkout_equipped, checkout_solo);
    uint64_t unpaid = UnpaidItems(selected, &gear_cost);
    int upgrade = UpgradePrice(upgrade_selected, upgrade_price);
    if (CheckoutGate() != CHECKOUT_OPEN || action == SMR_CHECKOUT_BLOCKED ||
        action != CheckoutAction(gear_cost, upgrade, upgrade_price, CheckoutPending()))
    {
        ReleaseSRWLockExclusive(&shop_lock);
        ShopMessage(mod, "Checkout unavailable: check the loadout, balance and pending purchase.");
        return 0;
    }
    if (action == SMR_CHECKOUT_CONTINUE)
    {
        ready_selection = selected;
        InterlockedExchange(&checkout_gate, CHECKOUT_CONFIRMED);
        ReleaseSRWLockExclusive(&shop_lock);
        return 1;
    }
    charge.mask = unpaid;
    charge.gear_cost = gear_cost;
    memcpy(charge.upgrade_price, upgrade_price, sizeof upgrade_price);
    charge.upgrade = upgrade ? upgrade_selected : SMR_UPGRADE_NONE;
    charge.extra = charge.upgrade == SMR_UPGRADE_EXTRA_SLOT ? extra_pick : 0;
    charge.state = CHARGE_QUEUED;
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
    for (size_t i = COMMON_SAMPLES; i < CURRENCY_COUNT; ++i)
        if (price[i] && (!ItemRecord(api, manager, kCurrencies[i].id, &before[i]) || before[i] < price[i]))
            return 0;
    return 1;
}

static void ProcessPurchase(HfMod* mod, uintptr_t manager, int balance_ready, unsigned balance, int state, int solo)
{
    if (state != CHARGE_QUEUED) return;
    uint64_t equipped = 0;
    unsigned gear_cost, upgrade_price[CURRENCY_COUNT], expected[CURRENCY_COUNT];
    int readable = ReadEquippedItems(mod, &equipped);
    AcquireSRWLockShared(&shop_lock);
    uint64_t selected = WithExtraAccess(equipped, solo);
    uint64_t unpaid = UnpaidItems(selected, &gear_cost), mask = charge.mask;
    int active = CheckoutGate() == CHECKOUT_OPEN;
    unsigned quoted_gear_cost = charge.gear_cost;
    memcpy(upgrade_price, charge.upgrade_price, sizeof upgrade_price);
    SmrUpgrade upgrade = charge.upgrade;
    uint32_t extra = charge.extra;
    int upgrade_valid = upgrade && upgrade_selected == upgrade && !upgrade_purchased &&
        UpgradePrice(upgrade, expected) && !memcmp(expected, upgrade_price, sizeof expected) &&
        (upgrade != SMR_UPGRADE_EXTRA_SLOT || extra == extra_pick);
    ReleaseSRWLockShared(&shop_lock);
    const HfApi* api = HF_API(mod);
    uint64_t requisition = SmrRequisitionDue(gear_cost, upgrade_price);
    unsigned before[CURRENCY_COUNT] = {0};
    if (!active || api->game_state(mod) != HF_STATE_SHIP || !manager || !readable || unpaid != mask ||
        gear_cost != quoted_gear_cost || (requisition && (!balance_ready || balance < requisition)) ||
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
    unsigned req_cost = (unsigned)requisition;
    int paid = 0, deducted = !req_cost || DeductRuntime(api, manager, REQUISITION, req_cost, balance, &after);
    for (size_t i = COMMON_SAMPLES; i < CURRENCY_COUNT && deducted; ++i)
        if (upgrade_price[i])
            deducted = DeductRuntime(api, manager, kCurrencies[i].id, upgrade_price[i], before[i], &upgrade_after[i]);
    if (!deducted)
        snprintf(detail, sizeof detail, "Runtime deduction failed verification; ledger unchanged.");
    else
        paid = SaveTransaction(api, manager, detail, sizeof detail);
    if (paid)
    {
        AcquireSRWLockExclusive(&shop_lock);
        wallet = currency_amount[REQUISITION_SLIPS] = after;
        for (size_t i = 0; i < PRICE_COUNT; ++i)
            if (mask & (UINT64_C(1) << i)) access[i] = 1;
        if (upgrade)
        {
            upgrade_purchased = upgrade;
            upgrade_selected = SMR_UPGRADE_NONE;
            for (size_t i = COMMON_SAMPLES; i < CURRENCY_COUNT; ++i)
                if (upgrade_price[i]) currency_amount[i] = upgrade_after[i];
        }
        charge.state = CHARGE_IDLE;
        ready_selection = selected;
        InterlockedCompareExchange(&checkout_gate, CHECKOUT_CONFIRMED, CHECKOUT_OPEN);
        ReleaseSRWLockExclusive(&shop_lock);
        api->log(mod, "Purchased loadout: %u Req, %u Forge Credits, %u Common, %u Rare and %u Super samples, "
            "access mask 0x%llX. %s", req_cost, upgrade_price[FORGE_CREDITS], upgrade_price[COMMON_SAMPLES],
            upgrade_price[RARE_SAMPLES], upgrade_price[SUPER_SAMPLES], (unsigned long long)mask, detail);
        ShopMessage(mod, "Loadout purchased. Readying automatically.");
        return;
    }
    snprintf(message, sizeof message, "Purchase stopped. %s No access or upgrade granted; shop disabled.", detail);
    StopShop(mod, message);
}
