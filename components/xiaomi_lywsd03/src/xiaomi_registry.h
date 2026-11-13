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


/*
 * Initialize Xiaomi sensor registry.
 *
 * NVS must already be initialized by the application.
 */
bool xiaomi_registry_init(void);


/*
 * Find bind key by sensor MAC.
 *
 * Returns true if the sensor is registered.
 */
bool xiaomi_registry_find_key(
        const uint8_t mac[6],
        uint8_t key[XIAOMI_BIND_KEY_SIZE]);


/*
 * Add a new sensor.
 *
 * If the MAC already exists, its key is replaced.
 */
bool xiaomi_registry_add(
        const uint8_t mac[6],
        const uint8_t key[XIAOMI_BIND_KEY_SIZE]);


/*
 * Remove a sensor by MAC.
 *
 * Returns true if the sensor was found and removed.
 */
bool xiaomi_registry_remove(
        const uint8_t mac[6]);


/*
 * Return number of registered sensors.
 */
size_t xiaomi_registry_count(void);


/*
 * Get registered sensor by index.
 *
 * Returns true if the index exists.
 */
bool xiaomi_registry_get(
        size_t index,
        xiaomi_registry_entry_t *entry);