int SmrTakeCheckoutRequest(void) { return InterlockedExchange(&checkout_requested, 0) != 0; }

void SmrFinishCheckout(int ready)
{
    if (ready) InterlockedCompareExchange(&auto_ready, 2, 1);
    else
    {
        AcquireSRWLockExclusive(&shop_lock);
        upgrade_selected = SMR_UPGRADE_NONE;
        InterlockedExchange(&auto_ready, 0);
        ReleaseSRWLockExclusive(&shop_lock);
    }
}

int SmrCheckoutPaid(void) { return InterlockedCompareExchange(&auto_ready, 0, 0) == 1; }

static int CheckoutFits(unsigned total, unsigned credits)
{
    return checkout_valid && !shop_failed && (total || credits) &&
        charge.state == CHARGE_IDLE && !exchange.pending &&
        (!total || (inventory_ready && wallet >= total)) &&
        (!credits || (forge_credits_ready && currency_amount[FORGE_CREDITS] >= credits));
}

int SmrCheckout(unsigned* total, unsigned* credits, int* pending)
{
    AcquireSRWLockShared(&shop_lock);
    UnpaidItems(checkout_mask, total);
    *credits = UpgradeCost(upgrade_selected);
    *pending = charge.state != CHARGE_IDLE || exchange.pending;
    int ready = CheckoutFits(*total, *credits);
    ReleaseSRWLockShared(&shop_lock);
    return ready;
}

int SmrBuyCheckout(HfMod* mod)
{
    const HfApi* api = HF_API(mod);
    if (api->game_state(mod) != HF_STATE_SHIP) return 0;
    AcquireSRWLockExclusive(&shop_lock);
    unsigned total;
    uint64_t unpaid = UnpaidItems(checkout_mask, &total);
    unsigned credits = UpgradeCost(upgrade_selected);
    if (!CheckoutFits(total, credits))
    {
        ReleaseSRWLockExclusive(&shop_lock);
        ShopMessage(mod, "Checkout unavailable: check the loadout, balance and pending purchase.");
        return 0;
    }
    charge.mask = unpaid;
    charge.total = total;
    charge.credits = credits;
    charge.upgrade = upgrade_selected;
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

static void ProcessPurchase(HfMod* mod, uintptr_t manager, int balance_ready, unsigned balance, int state)
{
    if (state != CHARGE_QUEUED) return;
    uint64_t selected = 0;
    unsigned total = 0;
    int readable = SelectedItems(mod, &selected);
    AcquireSRWLockShared(&shop_lock);
    uint64_t unpaid = UnpaidItems(selected, &total), mask = charge.mask;
    unsigned price = charge.total, credits = charge.credits;
    SmrUpgrade upgrade = charge.upgrade;
    int upgrade_valid = upgrade && upgrade_selected == upgrade && !upgrade_purchased &&
        credits == UpgradeCost(upgrade);
    ReleaseSRWLockShared(&shop_lock);
    const HfApi* api = HF_API(mod);
    const uint32_t credit_id = kCurrencies[FORGE_CREDITS].id;
    unsigned credit_balance = 0;
    if (api->game_state(mod) != HF_STATE_SHIP || !manager || !readable || unpaid != mask || total != price ||
        (price && (!balance_ready || balance < price)) ||
        (credits && (!upgrade_valid || !ItemRecord(api, manager, credit_id, &credit_balance) ||
            credit_balance < credits || !CanApplyUpgrade(mod, upgrade))))
    {
        AcquireSRWLockExclusive(&shop_lock);
        charge.state = CHARGE_IDLE;
        ReleaseSRWLockExclusive(&shop_lock);
        ShopMessage(mod, "Purchase cancelled: loadout, upgrade or funds unavailable. Nothing deducted.");
        return;
    }
    unsigned after = balance, credits_after = credit_balance;
    char detail[192], message[320];
    int paid = 0;
    if ((price && !DeductRuntime(api, manager, REQUISITION, price, balance, &after)) ||
        (credits && !DeductRuntime(api, manager, credit_id, credits, credit_balance, &credits_after)))
        snprintf(detail, sizeof detail, "Runtime deduction failed verification; profile NOT edited.");
    else if (price && credits)
        paid = SmrDeductProfilePair(logs_folder, REQUISITION, price, credit_id, credits, detail, sizeof detail);
    else
        paid = SmrDeductProfile(logs_folder, price ? REQUISITION : credit_id, price ? price : credits, detail, sizeof detail);
    if (paid)
    {
        AcquireSRWLockExclusive(&shop_lock);
        wallet = after;
        for (size_t i = 0; i < PRICE_COUNT; ++i)
            if (mask & (UINT64_C(1) << i)) access[i] = 1;
        if (credits)
        {
            upgrade_purchased = upgrade;
            upgrade_selected = SMR_UPGRADE_NONE;
            currency_amount[FORGE_CREDITS] = credits_after;
        }
        charge.state = CHARGE_IDLE;
        auto_ready_selection = selected;
        InterlockedExchange(&auto_ready, 1);
        ReleaseSRWLockExclusive(&shop_lock);
        api->log(mod, "Purchased loadout: %u Req and %u Forge Credits, access mask 0x%llX. %s",
            price, credits, (unsigned long long)mask, detail);
        ShopMessage(mod, "Loadout purchased. Readying automatically.");
        return;
    }
    AcquireSRWLockExclusive(&shop_lock);
    inventory_ready = exchange_ready = forge_credits_ready = 0;
    shop_failed = 1;
    charge.state = CHARGE_IDLE;
    ReleaseSRWLockExclusive(&shop_lock);
    snprintf(message, sizeof message, "Purchase stopped after native deduction. %s No access or upgrade granted; shop disabled.", detail);
    ShopMessage(mod, message);
}
