#pragma once
#include "hellforge_mod.h"
#include "../currency/currencies.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SmrItem {
    const char* name;
    unsigned price;
    int access;
    int pending;
    int selected;
} SmrItem;

enum {
    SMR_BUILTIN_PRICE_COUNT = 249,
    SMR_MAX_RESTRICTED_ITEMS = 1024,
    SMR_MAX_MODDED_ENEMIES = 1024,
    SMR_ITEM_SET_WORDS = (SMR_MAX_RESTRICTED_ITEMS + 63) / 64
};
typedef enum { PRICE_WEAPON, PRICE_STRATAGEM } PriceKind;
typedef struct AccessItem {
    PriceKind kind;
    uint32_t id;
    const char* name;
    unsigned requisition;
} AccessItem;
typedef struct SmrItemSet { uint64_t words[SMR_ITEM_SET_WORDS]; } SmrItemSet;
typedef struct SmrModdedEnemy { uint64_t resource; const char* key; const char* name; unsigned value; } SmrModdedEnemy;
static inline int SmrItemSetHas(const SmrItemSet* set, size_t item)
{
    return (set->words[item / 64] & (UINT64_C(1) << (item % 64))) != 0;
}
static inline void SmrItemSetAdd(SmrItemSet* set, size_t item)
{
    set->words[item / 64] |= UINT64_C(1) << (item % 64);
}
static inline int SmrItemSetEqual(const SmrItemSet* left, const SmrItemSet* right)
{
    for (size_t i = 0; i < SMR_ITEM_SET_WORDS; ++i)
        if (left->words[i] != right->words[i]) return 0;
    return 1;
}
typedef struct SmrQuoteLine {
    const char* name;
    unsigned due;
} SmrQuoteLine;

typedef struct SmrLoadoutQuote {
    unsigned total, balance;
    int balance_ready;
    size_t count;
    SmrQuoteLine items[SMR_MAX_RESTRICTED_ITEMS];
} SmrLoadoutQuote;

typedef struct SmrCurrency {
    const char* name;
    unsigned points, amount, cap;
    int ready, pending;
} SmrCurrency;

typedef enum SmrUpgrade {
    SMR_UPGRADE_NONE, SMR_UPGRADE_ORBITAL, SMR_UPGRADE_EAGLE, SMR_UPGRADE_DEFENSIVE, SMR_UPGRADE_SUPPORT,
    SMR_UPGRADE_EXTRA_SLOT, SMR_UPGRADE_COUNT
} SmrUpgrade;
typedef struct SmrUpgradeTuning {
    unsigned price[CURRENCY_COUNT]; /* kCurrencies order. */
    double cooldown_percent, other_cooldown_percent;
    double cooldown_seconds, call_in_extra_seconds, rearm_seconds;
} SmrUpgradeTuning;
typedef struct SmrDestroyerStatus {
    unsigned balance[CURRENCY_COUNT];
    int available;
    SmrUpgrade selected, purchased;
    uint32_t extra;
} SmrDestroyerStatus;

enum { SMR_MAX_EXTRA_CHOICES = 149 };
typedef struct SmrExtraChoice {
    uint32_t type;
    const char* name;
    uint32_t category; /* HF_CATEGORY_... */
} SmrExtraChoice;

/* Helldiver upgrades stack: one perk from each tier. */
typedef enum SmrHelldiverTier { SMR_TIER_PLUS, SMR_TIER_ELITE, SMR_TIER_SUPER, SMR_TIER_COUNT } SmrHelldiverTier;
typedef enum SmrHelldiverPerk {
    SMR_PERK_NONE,
    SMR_PERK_PRIMARY_TRADEOFF, SMR_PERK_SECONDARY_TRADEOFF, SMR_PERK_GRENADE_KIT,
    SMR_PERK_ARMORED, SMR_PERK_SPRINTER, SMR_PERK_ENDURANCE,
    SMR_PERK_AUTO_STIM, SMR_PERK_FIELD_MEDIC,
    SMR_PERK_COUNT
} SmrHelldiverPerk;
static inline SmrHelldiverTier SmrPerkTier(SmrHelldiverPerk perk)
{
    return perk <= SMR_PERK_GRENADE_KIT ? SMR_TIER_PLUS : perk <= SMR_PERK_ENDURANCE ? SMR_TIER_ELITE : SMR_TIER_SUPER;
}
typedef struct SmrHelldiverTuning {
    unsigned price[CURRENCY_COUNT]; /* The perk's tier price, kCurrencies order. */
    double primary_ammo_percent, secondary_ammo_percent, throwables_percent;
    double armor_percent, sprint_percent, stamina_percent, movement_percent;
    double health_percent, cooldown_seconds;
} SmrHelldiverTuning;
typedef struct SmrHelldiverStatus {
    unsigned balance[CURRENCY_COUNT];
    int available[SMR_TIER_COUNT];
    SmrHelldiverPerk selected[SMR_TIER_COUNT], purchased[SMR_TIER_COUNT];
} SmrHelldiverStatus;

typedef struct EquipmentEdit {
    HfRef target;
    HfField field;
    const char* key;
    const char* label;
    double value;
} EquipmentEdit;
typedef struct SmrEquipmentChoice {
    const char* title;
    EquipmentEdit* edits;
    size_t edit_count;
} SmrEquipmentChoice;
typedef struct SmrEquipmentGroup {
    uint32_t primary;
    const char* weapon;
    SmrHelldiverTier tier;
    const SmrEquipmentChoice* choices;
    size_t count;
    unsigned price[CURRENCY_COUNT];
} SmrEquipmentGroup;
typedef struct SmrEquipmentTierStatus {
    const SmrEquipmentGroup* group; /* NULL when the equipped weapon has no upgrades at this tier. */
    const unsigned* price;
    unsigned selected, purchased; /* One-based choices; zero means none. */
    int available;
} SmrEquipmentTierStatus;
typedef struct SmrEquipmentStatus {
    SmrEquipmentTierStatus tiers[SMR_TIER_COUNT];
    unsigned balance[CURRENCY_COUNT];
    int configured;
} SmrEquipmentStatus;
SmrEquipmentStatus SmrEquipment(void);
int SmrSelectEquipment(HfMod* mod, const SmrEquipmentGroup* group, unsigned choice);
int SmrLoadEquipmentConfig(SmrEquipmentGroup* groups, size_t count, char* result, size_t size);

SmrHelldiverStatus SmrHelldiver(HfMod* mod);
int SmrSelectHelldiver(HfMod* mod, SmrHelldiverTier tier, SmrHelldiverPerk perk);
const SmrHelldiverTuning* SmrGetHelldiverTuning(SmrHelldiverPerk perk);
int SmrLoadHelldiverConfig(SmrHelldiverTuning tuning[SMR_PERK_COUNT], char* result, size_t size);

SmrDestroyerStatus SmrDestroyer(HfMod* mod);
int SmrSelectUpgrade(HfMod* mod, SmrUpgrade upgrade, uint32_t extra);
size_t SmrExtraChoices(SmrExtraChoice* choices, size_t capacity);
const char* SmrExtraName(uint32_t type);
const SmrUpgradeTuning* SmrGetUpgradeTuning(SmrUpgrade upgrade);
int SmrLoadUpgradeConfig(SmrUpgradeTuning tuning[SMR_UPGRADE_COUNT], char* result, size_t size);

int SmrInit(const HfApi* api, HfMod* mod);
int SmrShopItem(size_t i, SmrItem* out);
int SmrLoadoutOpen(HfMod* mod);
int SmrQuoteLoadout(HfMod* mod, SmrLoadoutQuote* out);
int SmrBalance(unsigned* out);
void SmrMessage(char* out, size_t size);
typedef enum SmrCheckoutAction {
    SMR_CHECKOUT_BLOCKED, SMR_CHECKOUT_PURCHASE, SMR_CHECKOUT_CONTINUE
} SmrCheckoutAction;
int SmrConfirmCheckout(HfMod* mod, SmrCheckoutAction action);
SmrCheckoutAction SmrCheckout(unsigned* gear_cost, unsigned upgrade_price[CURRENCY_COUNT], int* pending);
static inline uint64_t SmrRequisitionDue(unsigned gear_cost, const unsigned upgrade_price[CURRENCY_COUNT])
{
    return (uint64_t)gear_cost + upgrade_price[REQUISITION_SLIPS];
}
int SmrCheckoutComplete(void);
int SmrTakeCheckoutRequest(void);
void SmrFinishCheckout(int ready);
int SmrLoadPrices(const char* const* names, unsigned* prices, size_t count, char* result, size_t size);
int SmrEnsureModdedContentFile(char* result, size_t size);
int SmrLoadModdedContent(HfMod* mod, AccessItem* items, const char** item_keys, size_t* item_count,
    SmrModdedEnemy* enemies, size_t* enemy_count, const char* const* builtin_enemy_names,
    size_t builtin_enemy_count, char* result, size_t size);
/* Kill rewards: an enemy missing from kill-rewards.json is worth the default; 0 excludes it. */
enum {
    SMR_KILL_DEFAULT_VALUE = 1, SMR_KILL_MAX_VALUE = 1000000, SMR_KILL_BONUS_PERCENT = 5,
    SMR_KILL_ABANDON_PERCENT = 20
};
int SmrLoadKillRewards(const char* const* names, unsigned* values, size_t count, char* result, size_t size);
/* The last counted kill: Requisition with this mission's projected reward before and after it, capped. base is the
 * amount without this mission's kills. sequence changes with each kill and is 0 before the first. */
typedef struct SmrKillNotice { uint32_t sequence; unsigned base, before, after; } SmrKillNotice;
void SmrLastKill(SmrKillNotice* out);
/* Completed mission rewards, before the wallet cap, retained after the live tally resets. earned is share + bonus,
 * or SMR_KILL_ABANDON_PERCENT of it when the mission was abandoned. */
typedef struct SmrKillSummary {
    uint32_t sequence;
    unsigned team_kills, own_kills, players;
    uint64_t team, mine, share, bonus, earned;
    int abandoned;
} SmrKillSummary;
void SmrLastKillSummary(SmrKillSummary* out);
int SmrCurrencyItem(size_t i, SmrCurrency* out);
int SmrExchangeQuote(size_t from, size_t to, unsigned batches, unsigned* debit, unsigned* credit);
int SmrExchange(HfMod* mod, size_t from, size_t to, unsigned batches);

#ifdef __cplusplus
}
#endif
