#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "platform_ble_scan.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XIAOMI_LYWSD03_MODEL "LYWSD03MMC"
#define XIAOMI_LYWSD03_MAC_SIZE 6
#define XIAOMI_LYWSD03_BIND_KEY_SIZE 16

typedef struct {
    uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE];
} xiaomi_lywsd03_sensor_info_t;

typedef struct {
    uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE];
    bool temperature_valid;
    bool humidity_valid;
    bool battery_valid;
    float temperature;
    float humidity;
    uint8_t battery;
} xiaomi_lywsd03_data_t;

/**
 * Called synchronously from xiaomi_lywsd03_process_adv().
 * The callback runs in the context of the caller, which may be the BLE host
 * callback. Keep it short and do not retain the data pointer after returning.
 */
typedef void (*xiaomi_lywsd03_data_callback_t)(
    const xiaomi_lywsd03_data_t *data,
    void *context);

/**
 * Initialize the Xiaomi registry and PSA Crypto.
 * The application must initialize NVS before calling this function.
 */
esp_err_t xiaomi_lywsd03_init(void);

/** Set the measurement callback. Pass NULL to clear it. */
void xiaomi_lywsd03_set_callback(
    xiaomi_lywsd03_data_callback_t callback,
    void *context);

/**
 * Process one advertisement received from platform_ble.
 * The packet and its data buffer are used only during this call.
 * Returns true when a supported, authenticated measurement was decoded.
 */
bool xiaomi_lywsd03_process_adv(
    const platform_ble_adv_packet_t *packet);

/** Check whether an advertisement belongs to a Xiaomi LYWSD03MMC. */
bool xiaomi_lywsd03_is_packet(
    const platform_ble_adv_packet_t *packet);

/** Extract the sensor MAC from the MiBeacon payload. */
bool xiaomi_lywsd03_get_info(
    const platform_ble_adv_packet_t *packet,
    uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE]);

/**
 * Register a sensor or replace its bind key when the MAC already exists.
 * MAC bytes use normal display order; bind key is exactly 16 bytes.
 * Returns false if the registry is not initialized or the operation fails.
 */
bool xiaomi_lywsd03_add_sensor(
    const uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE],
    const uint8_t key[XIAOMI_LYWSD03_BIND_KEY_SIZE]);

/** Remove a registered sensor. Returns false if it was not found or failed. */
bool xiaomi_lywsd03_remove_sensor(
    const uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE]);

/** Check whether a sensor has a registered bind key. */
bool xiaomi_lywsd03_has_key(
    const uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE]);

/** Return the number of registered sensors; returns 0 on registry error. */
size_t xiaomi_lywsd03_sensor_count(void);

/**
 * Get public information about a registered sensor by zero-based index.
 * Bind keys are deliberately not exposed through the public API.
 */
bool xiaomi_lywsd03_get_sensor(
    size_t index,
    xiaomi_lywsd03_sensor_info_t *sensor);

#ifdef __cplusplus
}
#endif
