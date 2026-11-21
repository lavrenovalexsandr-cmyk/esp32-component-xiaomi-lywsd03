#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XIAOMI_REGISTRY_MAX_DEVICES 16
#define XIAOMI_BIND_KEY_SIZE 16

typedef struct
{
    uint8_t mac[6];
    uint8_t key[XIAOMI_BIND_KEY_SIZE];
} xiaomi_registry_entry_t;

typedef enum
{
    XIAOMI_REGISTRY_OK = 0,
    XIAOMI_REGISTRY_NOT_FOUND,
    XIAOMI_REGISTRY_ALREADY_EXISTS,
    XIAOMI_REGISTRY_FULL,
    XIAOMI_REGISTRY_INVALID_ARGUMENT,
    XIAOMI_REGISTRY_NOT_INITIALIZED,
    XIAOMI_REGISTRY_BUSY,
    XIAOMI_REGISTRY_STORAGE_ERROR
} xiaomi_registry_result_t;

xiaomi_registry_result_t xiaomi_registry_init(void);
xiaomi_registry_result_t xiaomi_registry_find_key(
    const uint8_t mac[6],
    uint8_t key[XIAOMI_BIND_KEY_SIZE]);
xiaomi_registry_result_t xiaomi_registry_add(
    const uint8_t mac[6],
    const uint8_t key[XIAOMI_BIND_KEY_SIZE]);
xiaomi_registry_result_t xiaomi_registry_remove(const uint8_t mac[6]);
xiaomi_registry_result_t xiaomi_registry_clear(void);
xiaomi_registry_result_t xiaomi_registry_count(size_t *count);
xiaomi_registry_result_t xiaomi_registry_get(
    size_t index,
    xiaomi_registry_entry_t *entry);
