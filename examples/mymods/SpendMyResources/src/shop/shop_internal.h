#pragma once
#include "shop.h"
#include "../access/access_items.h"
#include "../currency/currency_ledger.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

enum { DELIVERY_EAGLE = 0, DELIVERY_ORBITAL = 1, DELIVERY_MISSION = 4, DELIVERY_BARRAGE = 5, DELIVERY_REARM = 7 };
enum { HUD_MISSION = 1, HUD_SUPPORT = 2, HUD_DEFENSIVE = 3, HUD_SPECIAL = 4 };

static unsigned char access[PRICE_COUNT];
static unsigned char deployed_access[PRICE_COUNT];
static unsigned wallet;
static int inventory_ready;
static int shop_failed;
static char shop_message[192] = "Waiting for the player's inventory.";
static SRWLOCK shop_lock = SRWLOCK_INIT;
static const char* logs_folder;
static uint64_t checkout_equipped, ready_selection;
static int checkout_valid, checkout_solo;
enum { CHECKOUT_IDLE, CHECKOUT_REQUESTED, CHECKOUT_OPEN, CHECKOUT_CONFIRMED, CHECKOUT_RESUME };
static volatile LONG checkout_gate;
static LONG CheckoutGate(void) { return InterlockedCompareExchange(&checkout_gate, 0, 0); }
static SmrUpgrade upgrade_selected, upgrade_purchased;
static uint32_t extra_pick;
static SmrHelldiverPerk helldiver_selected[SMR_TIER_COUNT], helldiver_purchased[SMR_TIER_COUNT];
static unsigned currency_amount[CURRENCY_COUNT], currency_cap[CURRENCY_COUNT];
static int exchange_ready, upgrade_funds_ready;
static struct {
    int pending;
    size_t from, to;
    unsigned batches;
} exchange;

enum { CHARGE_IDLE, CHARGE_QUEUED };
static struct {
    int state;
    uint64_t mask;
    unsigned gear_cost, upgrade_price[CURRENCY_COUNT];
    SmrUpgrade upgrade;
    uint32_t extra;
    SmrHelldiverPerk helldiver[SMR_TIER_COUNT];
} charge;

static void ShopMessage(HfMod* mod, const char* text);
static void StopShop(HfMod* mod, const char* text);
static int DestroyerSolo(HfMod* mod);
