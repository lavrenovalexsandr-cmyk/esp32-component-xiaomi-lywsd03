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
    bool temperature_valid;
    bool humidity_valid;
    bool battery_valid;
    float temperature;
    float humidity;
    uint8_t battery;
} xiaomi_lywsd03_data_t;

/** Called synchronously from process_adv() in BLE host callback context. */
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

/** Registry operations. NVS must have been initialized and init succeeded. */
bool xiaomi_lywsd03_add_sensor(
    const uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE],
    const uint8_t key[XIAOMI_LYWSD03_BIND_KEY_SIZE]);
bool xiaomi_lywsd03_remove_sensor(
    const uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE]);
bool xiaomi_lywsd03_has_key(
    const uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE]);
size_t xiaomi_lywsd03_sensor_count(void);

#ifdef __cplusplus
}
#endif
