#pragma once
#include "shop.h"
#include "access_items.h"
#include "currencies.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

static unsigned char access[PRICE_COUNT];
static unsigned char deployed_access[PRICE_COUNT];
static unsigned owned_amount[PRICE_COUNT];
static unsigned wallet;
static int inventory_ready;
static int shop_failed;
static char shop_message[192] = "Waiting for the player's inventory.";
static SRWLOCK shop_lock = SRWLOCK_INIT;
static const char* logs_folder;
static unsigned checkout_mask;
static int checkout_valid;
static volatile LONG checkout_requested;
static volatile LONG auto_ready;
static unsigned auto_ready_selection;
static unsigned currency_amount[CURRENCY_COUNT], currency_cap[CURRENCY_COUNT];
static int exchange_ready;
static struct {
    int pending;
    size_t from, to;
    unsigned batches;
} exchange;

enum { CHARGE_IDLE, CHARGE_QUEUED };
static struct {
    int state;
    unsigned mask;
    unsigned total;
} charge;

static void ShopMessage(HfMod* mod, const char* text);
