#pragma once
#include "shop.h"
#include "access_items.h"
#include "currency_ledger.h"
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
static uint64_t checkout_mask;
static int checkout_valid;
static volatile LONG checkout_requested;
static volatile LONG auto_ready;
static uint64_t auto_ready_selection;
enum { DESTROYER_IDLE, DESTROYER_REQUESTED, DESTROYER_OPEN, DESTROYER_CONTINUE };
static volatile LONG destroyer_gate;
static SmrUpgrade upgrade_selected, upgrade_purchased;
static uint32_t extra_pick;
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
    unsigned total, upgrade_price[CURRENCY_COUNT];
    SmrUpgrade upgrade;
    uint32_t extra;
} charge;

static void ShopMessage(HfMod* mod, const char* text);
static void StopShop(HfMod* mod, const char* text);
static int DestroyerSolo(HfMod* mod);
