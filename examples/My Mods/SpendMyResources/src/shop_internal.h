#pragma once
#include "shop.h"
#include "access_items.h"
#include "requisition.h"
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
static uint32_t token_weapon, token_attachment;
static const char* logs_folder;

enum { CHARGE_IDLE, CHARGE_QUEUED, CHARGE_RUNNING, CHARGE_SAVED, CHARGE_REFUSED, CHARGE_UNKNOWN };
static struct {
    int state;
    size_t item;
    char error[192];
} charge;

static void ShopMessage(HfMod* mod, const char* text);
