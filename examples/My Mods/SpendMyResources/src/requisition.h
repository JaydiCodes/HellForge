#pragma once
#include <stddef.h>
#include <stdint.h>

/* 1: confirmed purchase, 0: refused, -1: unknown outcome; never retry automatically. */
int SmrSpendRequisition(const char* logs, uint32_t token, unsigned amount, char* error, size_t error_size);
