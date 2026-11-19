int SmrTakeCheckoutRequest(void) { return InterlockedExchange(&checkout_requested, 0) != 0; }

void SmrFinishCheckout(int ready)
{
    if (ready) InterlockedCompareExchange(&auto_ready, 2, 1);
    else InterlockedExchange(&auto_ready, 0);
}

int SmrCheckout(unsigned* total, int* pending)
{
    AcquireSRWLockShared(&shop_lock);
    unsigned unpaid = UnpaidItems(checkout_mask, total);
    *pending = charge.state != CHARGE_IDLE;
    int owned = 1;
    for (size_t i = 0; i < PRICE_COUNT; ++i)
        if ((unpaid & (1u << i)) && !owned_amount[i]) owned = 0;
    int ready = owned && checkout_valid && inventory_ready && unpaid && !*pending && wallet >= *total;
    ReleaseSRWLockShared(&shop_lock);
    return ready;
}

int SmrBuyCheckout(HfMod* mod)
{
    const HfApi* api = HF_API(mod);
    if (api->game_state(mod) != HF_STATE_SHIP) return 0;
    AcquireSRWLockExclusive(&shop_lock);
    unsigned total;
    unsigned unpaid = UnpaidItems(checkout_mask, &total);
    int owned = 1;
    for (size_t i = 0; i < PRICE_COUNT; ++i)
        if ((unpaid & (1u << i)) && !owned_amount[i]) owned = 0;
    if (!owned || !checkout_valid || !unpaid || !total || !inventory_ready || charge.state != CHARGE_IDLE || wallet < total)
    {
        ReleaseSRWLockExclusive(&shop_lock);
        ShopMessage(mod, "Checkout unavailable: check the loadout, balance and pending purchase.");
        return 0;
    }
    charge.mask = unpaid;
    charge.total = total;
    charge.state = CHARGE_QUEUED;
    InterlockedExchange(&auto_ready, 0);
    ReleaseSRWLockExclusive(&shop_lock);
    ShopMessage(mod, "Deducting requisition...");
    return 1;
}

static void ProcessPurchase(HfMod* mod, uintptr_t manager, unsigned balance, int state)
{
    if (state != CHARGE_QUEUED) return;
    unsigned selected = 0, total = 0;
    int readable = SelectedItems(mod, &selected);
    AcquireSRWLockShared(&shop_lock);
    unsigned unpaid = UnpaidItems(selected, &total), mask = charge.mask, price = charge.total;
    ReleaseSRWLockShared(&shop_lock);
    if (HF_API(mod)->game_state(mod) != HF_STATE_SHIP || !readable || unpaid != mask || total != price || balance < price)
    {
        AcquireSRWLockExclusive(&shop_lock);
        charge.state = CHARGE_IDLE;
        ReleaseSRWLockExclusive(&shop_lock);
        ShopMessage(mod, "Purchase cancelled: loadout changed, unavailable, or insufficient requisition. Nothing deducted.");
        return;
    }
    unsigned after = 0;
    char detail[192], message[320];
    inventory_remove(manager, REQUISITION, price);
    if (!ItemRecord(HF_API(mod), manager, REQUISITION, &after) || after != balance - price)
        snprintf(detail, sizeof detail, "Runtime deduction failed verification; profile NOT edited.");
    else if (SmrDeductProfile(logs_folder, REQUISITION, price, detail, sizeof detail))
    {
        AcquireSRWLockExclusive(&shop_lock);
        wallet = after;
        for (size_t i = 0; i < PRICE_COUNT; ++i)
            if (mask & (1u << i)) access[i] = 1;
        charge.state = CHARGE_IDLE;
        auto_ready_selection = selected;
        ReleaseSRWLockExclusive(&shop_lock);
        InterlockedExchange(&auto_ready, 1);
        HF_API(mod)->log(mod, "Purchased loadout access (mask 0x%X): runtime requisition %u -> %u. %s",
            mask, balance, after, detail);
        ShopMessage(mod, "Access purchased. Readying automatically.");
        return;
    }
    AcquireSRWLockExclusive(&shop_lock);
    inventory_ready = 0;
    shop_failed = 1;
    ReleaseSRWLockExclusive(&shop_lock);
    snprintf(message, sizeof message, "Purchase stopped after native deduction. %s No access granted; shop disabled.", detail);
    ShopMessage(mod, message);
}
