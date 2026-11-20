#include <limits.h>

int SmrCurrencyItem(size_t i, SmrCurrency* out)
{
    if (i >= CURRENCY_COUNT) return 0;
    AcquireSRWLockShared(&shop_lock);
    *out = (SmrCurrency){kCurrencies[i].name, kCurrencies[i].points, currency_amount[i], currency_cap[i],
        exchange_ready && !shop_failed, exchange.pending || charge.state != CHARGE_IDLE};
    ReleaseSRWLockShared(&shop_lock);
    return 1;
}

int SmrExchangeQuote(size_t from, size_t to, unsigned batches, unsigned* debit, unsigned* credit)
{
    *debit = *credit = 0;
    if (from >= CURRENCY_COUNT || to >= CURRENCY_COUNT || from == to || !batches) return 0;
    unsigned a = kCurrencies[from].points, b = kCurrencies[to].points;
    while (b) { unsigned remainder = a % b; a = b; b = remainder; }
    unsigned give = kCurrencies[to].points / a, receive = kCurrencies[from].points / a;
    if (batches > UINT_MAX / give || batches > UINT_MAX / receive) return 0;
    *debit = give * batches;
    *credit = receive * batches;
    return 1;
}

static int ExchangeFits(size_t from, size_t to, unsigned debit, unsigned credit)
{
    return currency_amount[from] >= debit && currency_amount[to] <= UINT_MAX - credit &&
        (!currency_cap[to] || (currency_amount[to] <= currency_cap[to] &&
            credit <= currency_cap[to] - currency_amount[to]));
}

int SmrExchange(HfMod* mod, size_t from, size_t to, unsigned batches)
{
    unsigned debit, credit;
    if (!SmrExchangeQuote(from, to, batches, &debit, &credit) || HF_API(mod)->game_state(mod) != HF_STATE_SHIP)
    {
        ShopMessage(mod, "Exchange unavailable: choose different currencies and a valid batch count on the ship.");
        return 0;
    }
    AcquireSRWLockExclusive(&shop_lock);
    int allowed = exchange_ready && !shop_failed && !exchange.pending && charge.state == CHARGE_IDLE &&
        ExchangeFits(from, to, debit, credit);
    if (allowed)
    {
        exchange.from = from;
        exchange.to = to;
        exchange.batches = batches;
        exchange.pending = 1;
    }
    ReleaseSRWLockExclusive(&shop_lock);
    ShopMessage(mod, allowed ? "Exchanging currencies..." :
        "Exchange unavailable: insufficient funds, destination cap, inventory unavailable or another transaction pending.");
    return allowed;
}

static void ReadCurrencies(HfMod* mod, uintptr_t manager)
{
    unsigned amounts[CURRENCY_COUNT] = {0}, caps[CURRENCY_COUNT] = {0};
    int ready = manager && HF_API(mod)->game_state(mod) == HF_STATE_SHIP;
    for (size_t i = 0; i < CURRENCY_COUNT && ready; ++i)
    {
        uintptr_t record = ItemRecord(HF_API(mod), manager, kCurrencies[i].id, &amounts[i]);
        ready = record && !HF_API(mod)->read(record + 0x18, &caps[i], sizeof caps[i]);
    }
    AcquireSRWLockExclusive(&shop_lock);
    exchange_ready = ready;
    memcpy(currency_amount, amounts, sizeof amounts);
    memcpy(currency_cap, caps, sizeof caps);
    ReleaseSRWLockExclusive(&shop_lock);
}

static void ProcessExchange(HfMod* mod, uintptr_t manager)
{
    AcquireSRWLockExclusive(&shop_lock);
    if (!exchange.pending) { ReleaseSRWLockExclusive(&shop_lock); return; }
    size_t from = exchange.from, to = exchange.to;
    unsigned debit, credit;
    int valid = SmrExchangeQuote(from, to, exchange.batches, &debit, &credit) && exchange_ready &&
        !shop_failed && charge.state == CHARGE_IDLE && ExchangeFits(from, to, debit, credit);
    unsigned before_from = currency_amount[from], before_to = currency_amount[to];
    if (!valid) exchange.pending = 0;
    ReleaseSRWLockExclusive(&shop_lock);
    if (!valid)
    {
        ShopMessage(mod, "Exchange cancelled: left the ship, inventory changed, insufficient funds or destination cap. Nothing changed.");
        return;
    }
    inventory_remove(manager, kCurrencies[from].id, debit);
    unsigned after_from = 0, after_to = 0;
    char detail[192], message[320];
    if (!ItemRecord(HF_API(mod), manager, kCurrencies[from].id, &after_from) || after_from != before_from - debit)
        snprintf(detail, sizeof detail, "Runtime deduction failed verification; destination and profile NOT changed.");
    else
    {
        inventory_add(manager, kCurrencies[to].id, credit, 0, NULL, 0);
        if (!ItemRecord(HF_API(mod), manager, kCurrencies[to].id, &after_to) || after_to != before_to + credit ||
            !ItemRecord(HF_API(mod), manager, kCurrencies[from].id, &after_from) || after_from != before_from - debit)
            snprintf(detail, sizeof detail, "Runtime addition failed verification; profile NOT changed.");
        else if (SmrExchangeProfile(logs_folder, kCurrencies[from].id, debit, kCurrencies[to].id, credit, detail, sizeof detail))
        {
            AcquireSRWLockExclusive(&shop_lock);
            currency_amount[from] = after_from;
            currency_amount[to] = after_to;
            wallet = currency_amount[0];
            exchange.pending = 0;
            ReleaseSRWLockExclusive(&shop_lock);
            snprintf(message, sizeof message, "Exchanged %u %s for %u %s.", debit, kCurrencies[from].name, credit, kCurrencies[to].name);
            ShopMessage(mod, message);
            return;
        }
    }
    AcquireSRWLockExclusive(&shop_lock);
    exchange.pending = 0;
    exchange_ready = inventory_ready = 0;
    shop_failed = 1;
    ReleaseSRWLockExclusive(&shop_lock);
    snprintf(message, sizeof message, "Exchange stopped after native update. %s Shop disabled; do not retry.", detail);
    ShopMessage(mod, message);
}
